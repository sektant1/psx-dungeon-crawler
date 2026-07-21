#include "DungeonMap.h"

#include <eng/Log.h>
#include <eng/Renderer.h>

#include <glm/gtc/quaternion.hpp>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>
#include <cmath>
#include <set>

namespace {

constexpr const char* kTileMaterial = "Game/PropBauerhaus";
// Half-width of the stone_archway's walkable opening (measured: the gap
// spans 1.2..2.8 across the 4 m piece).
constexpr float kArchHalfWidth = 0.8f;

float lin(float srgb) { return std::pow(srgb, 2.2f); }

bool isWalkableChar(char c)
{
    return c == '.' || c == 'A' || c == 'L' || c == 'S' || c == 'C';
}

} // namespace

char DungeonMap::cellAt(int col, int row) const
{
    if (row < 0 || row >= int(mRows.size()))
        return ' ';
    const std::string& r = mRows[row];
    if (col < 0 || col >= int(r.size()))
        return ' ';
    return r[col];
}

bool DungeonMap::walkableCell(int col, int row) const
{
    return isWalkableChar(cellAt(col, row));
}

void DungeonMap::cellOf(float x, float z, int& col, int& row) const
{
    col = int(std::floor((x - mOrigin.x) / mCell));
    row = int(std::floor((z - mOrigin.z) / mCell));
}

int DungeonMap::roomOfCell(int col, int row) const
{
    if (row < 0 || col < 0 || row >= int(mRows.size()) || mStride == 0)
        return -1;
    const int idx = row * mStride + col;
    if (col >= mStride || idx >= int(mCellRoom.size()))
        return -1;
    return mCellRoom[size_t(idx)];
}

bool DungeonMap::load(eng::Renderer& r, const std::string& tomlPath,
                      const std::string& tileMeshDir,
                      const std::string& propMeshDir)
{
    toml::parse_result result = toml::parse_file(tomlPath);
    if (!result) {
        eng::log::error("DungeonMap: failed to parse %s: %s", tomlPath.c_str(),
                        std::string(result.error().description()).c_str());
        return false;
    }
    const toml::table& root = result.table();
    const toml::table* dungeon = root["dungeon"].as_table();
    if (!dungeon) {
        eng::log::error("DungeonMap: missing [dungeon] table");
        return false;
    }
    mCell = float((*dungeon)["cell_size"].value_or(4.0));
    const float wallH = float((*dungeon)["wall_height"].value_or(3.0));

    const toml::array* rows = (*dungeon)["rows"].as_array();
    if (!rows || rows->empty()) {
        eng::log::error("DungeonMap: missing 'rows' array");
        return false;
    }
    mRows.clear();
    for (const auto& e : *rows)
        mRows.push_back(e.value_or(std::string()));

    // The 'C' cell's centre becomes the world origin (the shared DemoScene
    // loads at fixed positions around the origin).
    int cCol = -1, cRow = -1, sCol = -1, sRow = -1;
    for (int row = 0; row < int(mRows.size()); ++row) {
        for (int col = 0; col < int(mRows[row].size()); ++col) {
            if (mRows[row][col] == 'C') { cCol = col; cRow = row; }
            if (mRows[row][col] == 'S') { sCol = col; sRow = row; }
        }
    }
    if (cCol < 0 || sCol < 0) {
        eng::log::error("DungeonMap: map needs both a 'C' and an 'S' marker");
        return false;
    }
    mOrigin = {-(cCol + 0.5f) * mCell, 0.0f, -(cRow + 0.5f) * mCell};
    mSpawn = {mOrigin.x + (sCol + 0.5f) * mCell, 0.0f,
              mOrigin.z + (sRow + 0.5f) * mCell};

    // --- room segmentation (occlusion-culling units) ---
    // Flood-fill walkable cells into rooms; 'A' arch cells are boundaries
    // (each arch is its own batch joining <=2 rooms). Runs before geometry so
    // the grid loop can route each tile into its room/arch batch.
    mStride = 0;
    for (const std::string& s : mRows)
        mStride = std::max(mStride, int(s.size()));
    const int rowsN = int(mRows.size());
    mCellRoom.assign(size_t(rowsN * mStride), -1);
    mCellArch.assign(size_t(rowsN * mStride), -1);

    const auto idxOf = [&](int col, int row) { return row * mStride + col; };
    // Assign arch indices first (each 'A' cell = one arch).
    for (int row = 0; row < rowsN; ++row)
        for (int col = 0; col < int(mRows[row].size()); ++col)
            if (cellAt(col, row) == 'A') {
                mCellArch[size_t(idxOf(col, row))] = int(mArches.size());
                mArches.push_back({});
            }

    // Flood-fill non-arch walkable cells into rooms.
    for (int row = 0; row < rowsN; ++row) {
        for (int col = 0; col < int(mRows[row].size()); ++col) {
            const char c = cellAt(col, row);
            if (!isWalkableChar(c) || c == 'A')
                continue;
            if (mCellRoom[size_t(idxOf(col, row))] != -1)
                continue;
            const int room = int(mRooms.size());
            mRooms.push_back({});
            std::vector<std::pair<int, int>> stack{{col, row}};
            mCellRoom[size_t(idxOf(col, row))] = room;
            while (!stack.empty()) {
                auto [cc, cr] = stack.back();
                stack.pop_back();
                const std::pair<int, int> nb[4] = {
                    {cc + 1, cr}, {cc - 1, cr}, {cc, cr + 1}, {cc, cr - 1}};
                for (auto [nc, nr] : nb) {
                    if (nc < 0 || nr < 0 || nr >= rowsN || nc >= mStride)
                        continue;
                    const char nch = cellAt(nc, nr);
                    if (!isWalkableChar(nch) || nch == 'A')
                        continue;
                    int& slot = mCellRoom[size_t(idxOf(nc, nr))];
                    if (slot != -1)
                        continue;
                    slot = room;
                    stack.push_back({nc, nr});
                }
            }
        }
    }

    // Tag each arch with the rooms of its walkable non-arch neighbours.
    for (int row = 0; row < rowsN; ++row) {
        for (int col = 0; col < int(mRows[row].size()); ++col) {
            const int ai = mCellArch[size_t(idxOf(col, row))];
            if (ai < 0)
                continue;
            const std::pair<int, int> nb[4] = {
                {col + 1, row}, {col - 1, row}, {col, row + 1}, {col, row - 1}};
            for (auto [nc, nr] : nb) {
                const int rm = roomOfCell(nc, nr);
                if (rm < 0)
                    continue;
                if (mArches[size_t(ai)].roomA < 0)
                    mArches[size_t(ai)].roomA = rm;
                else if (mArches[size_t(ai)].roomA != rm &&
                         mArches[size_t(ai)].roomB < 0)
                    mArches[size_t(ai)].roomB = rm;
            }
        }
    }

    // One big-region batch per room and per arch (a room is a few cells, so
    // one region = one draw per room material; PVS handles inter-room culling).
    for (auto& rm : mRooms)
        rm.batch = r.createStaticBatch({8.0f, 8.0f, 8.0f});
    for (auto& ar : mArches)
        ar.batch = r.createStaticBatch({8.0f, 8.0f, 8.0f});

    // Lamp light parameters ([dungeon.light], warm torch defaults).
    glm::vec3 lightColour{lin(1.0f), lin(0.62f), lin(0.32f)};
    float lightEnergy = 4.0f, lightRange = 6.5f, lampY = 2.55f;
    if (const toml::table* light = (*dungeon)["light"].as_table()) {
        if (const toml::array* c = (*light)["colour_srgb"].as_array();
            c && c->size() == 3)
            lightColour = {lin(float((*c)[0].value_or(1.0))),
                           lin(float((*c)[1].value_or(1.0))),
                           lin(float((*c)[2].value_or(1.0)))};
        lightEnergy = float((*light)["energy"].value_or(4.0));
        lightRange = float((*light)["range"].value_or(6.5));
        lampY = float((*light)["y"].value_or(2.55));
    }

    // ------------------------------------------------------------ meshes ---
    const eng::MeshHandle floor = r.loadObj(tileMeshDir + "tile_floor.obj");
    const eng::MeshHandle ceiling = r.loadObj(tileMeshDir + "tile_ceiling.obj");
    const eng::MeshHandle wall = r.loadObj(tileMeshDir + "tile_wall.obj");
    const eng::MeshHandle wallPlaster =
        r.loadObj(tileMeshDir + "tile_wall_plaster.obj");
    const eng::MeshHandle arch = r.loadObj(tileMeshDir + "tile_arch.obj");
    const eng::MeshHandle pillar = r.loadObj(tileMeshDir + "tile_pillar.obj");
    const eng::MeshHandle torch = r.loadObj(propMeshDir + "prop_torch.obj");

    eng::StaticBatchHandle curBatch{}; // set per cell in the grid loop
    const auto put = [&](eng::MeshHandle m, glm::vec3 pos, float yawDeg) {
        r.addToStaticBatch(curBatch, m, kTileMaterial, pos, yawDeg);
    };
    const auto growRoomAabb = [&](int room, glm::vec3 cellMin, glm::vec3 cellMax) {
        Room& rm = mRooms[size_t(room)];
        if (rm.aabbMin == rm.aabbMax && rm.aabbMin == glm::vec3(0.0f)) {
            rm.aabbMin = cellMin;
            rm.aabbMax = cellMax;
        } else {
            rm.aabbMin = glm::min(rm.aabbMin, cellMin);
            rm.aabbMax = glm::max(rm.aabbMax, cellMax);
        }
    };

    eng::LightDesc warm;
    warm.colour = lightColour * lightEnergy;
    warm.range = lightRange;
    warm.castShadows = true; // torches throw hard prop shadows

    mArchNS.assign(mRows.size() * 64, false); // 64 >= any sane row length
    std::set<std::pair<int, int>> pillarSpots; // corner keys, de-duplicated

    for (int row = 0; row < int(mRows.size()); ++row) {
        for (int col = 0; col < int(mRows[row].size()); ++col) {
            const char c = mRows[row][col];
            if (!isWalkableChar(c))
                continue;
            // Cell rect: x in [x0, x0+cell], z in [z0, z0+cell].
            const float x0 = mOrigin.x + col * mCell;
            const float z0 = mOrigin.z + row * mCell;

            // Route this cell's tiles into its room (or arch) batch.
            const int aIdx = mCellArch[size_t(row * mStride + col)];
            const int rIdx = mCellRoom[size_t(row * mStride + col)];
            if (aIdx >= 0) {
                curBatch = mArches[size_t(aIdx)].batch;
            } else if (rIdx >= 0) {
                curBatch = mRooms[size_t(rIdx)].batch;
                growRoomAabb(rIdx, {x0, 0.0f, z0},
                             {x0 + mCell, wallH, z0 + mCell});
            }

            // Floor spans x[0,cell] z[-cell,0] from its node; ceiling is the
            // same footprint mirrored downward, raised to wall height.
            put(floor, {x0, 0.0f, z0 + mCell}, 0.0f);
            put(ceiling, {x0, wallH, z0 + mCell}, 0.0f);

            if (c == 'A') {
                // Arch tunnel runs toward its walkable neighbours; the
                // piece's own side walls replace this cell's wall segments.
                const bool ns = walkableCell(col, row - 1) ||
                                walkableCell(col, row + 1);
                mArchNS[row * 64 + col] = ns;
                if (ns)
                    put(arch, {x0, 0.0f, z0 + mCell}, 0.0f);
                else
                    put(arch, {x0 + mCell, 0.0f, z0 + mCell}, 90.0f);
                continue;
            }

            // Wall segments on solid/void boundaries, normals facing the
            // cell. Sprinkle the plaster-and-stone-base variant for variety.
            const bool wallN = !walkableCell(col, row - 1);
            const bool wallS = !walkableCell(col, row + 1);
            const bool wallW = !walkableCell(col - 1, row);
            const bool wallE = !walkableCell(col + 1, row);
            const auto pick = [&](int salt) {
                return (col * 7 + row * 13 + salt) % 4 == 0 ? wallPlaster
                                                            : wall;
            };
            if (wallN)
                put(pick(0), {x0, 0.0f, z0}, 0.0f);
            if (wallS)
                put(pick(1), {x0 + mCell, 0.0f, z0 + mCell}, 180.0f);
            if (wallW)
                put(pick(2), {x0, 0.0f, z0 + mCell}, 90.0f);
            if (wallE)
                put(pick(3), {x0 + mCell, 0.0f, z0}, -90.0f);

            // Wooden posts on inner wall corners.
            if (wallN && wallW) pillarSpots.insert({col, row});
            if (wallN && wallE) pillarSpots.insert({col + 1, row});
            if (wallS && wallW) pillarSpots.insert({col, row + 1});
            if (wallS && wallE) pillarSpots.insert({col + 1, row + 1});

            if (c == 'L') {
                // Wall torch. The mesh is a purpose-built vertical wall
                // torch: its mounting plate sits at local +z (z 0.026..
                // 0.060 after baking), so it hangs straight -- no lean.
                // Orientation = yaw to the wall * 180 about y, which turns
                // the plate toward the wall; the offset puts the plate
                // face flush against the wall plane. 'L' cells with no
                // solid neighbour get no torch: torches are wall fixtures.
                glm::vec3 mount{x0 + mCell / 2.0f, lampY, z0 + mCell / 2.0f};
                float yaw = 0.0f;
                const float in = 0.06f; // wall plane -> torch origin
                if (wallN) {
                    mount.z = z0 + in;
                } else if (wallS) {
                    mount.z = z0 + mCell - in;
                    yaw = 180.0f;
                } else if (wallW) {
                    mount.x = x0 + in;
                    yaw = -90.0f;
                } else if (wallE) {
                    mount.x = x0 + mCell - in;
                    yaw = 90.0f;
                } else {
                    eng::log::error("DungeonMap: 'L' at col %d row %d has no "
                                    "wall; torch skipped", col, row);
                    continue;
                }
                eng::NodeHandle n = r.createNode(eng::kRootNode, mount);
                r.setOrientation(
                    n, glm::angleAxis(glm::radians(yaw + 180.0f),
                                      glm::vec3(0, 1, 0)));
                r.attachMesh(n, torch, "Game/Torch");
                // Flame seat is at the mesh top (0.55 m); light and
                // particles hang off a child there so the tilt carries
                // them along.
                eng::NodeHandle tip = r.createNode(n, {0.0f, 0.55f, 0.0f});
                r.attachParticles(tip, "Game/TorchFire");
                r.attachParticles(tip, "Game/TorchAsh");
                // Grid position seeds the phase so torches flicker
                // out of step with each other.
                mTorches.push_back({r.attachLight(tip, warm), tip,
                                    mount + glm::vec3(0.0f, 0.55f, 0.0f),
                                    warm.colour,
                                    float(col * 5 + row * 11), true});
            }
        }
    }
    for (const auto& [pc, pr] : pillarSpots) {
        // Corner (pc,pr) borders cells (pc-1..pc, pr-1..pr): use the first
        // room neighbour's batch, else the first arch's.
        curBatch = {};
        const std::pair<int, int> corners[4] = {
            {pc - 1, pr - 1}, {pc, pr - 1}, {pc - 1, pr}, {pc, pr}};
        for (auto [cc, cr] : corners) {
            const int rm = roomOfCell(cc, cr);
            if (rm >= 0) { curBatch = mRooms[size_t(rm)].batch; break; }
        }
        if (!curBatch.valid())
            for (auto [cc, cr] : corners) {
                if (cr < 0 || cc < 0 || cr >= int(mRows.size()) || cc >= mStride)
                    continue;
                const int ai = mCellArch[size_t(cr * mStride + cc)];
                if (ai >= 0) { curBatch = mArches[size_t(ai)].batch; break; }
            }
        if (curBatch.valid())
            put(pillar, {mOrigin.x + pc * mCell, 0.0f, mOrigin.z + pr * mCell},
                0.0f);
    }

    for (const auto& rm : mRooms)
        r.buildStaticBatch(rm.batch);
    for (const auto& ar : mArches)
        r.buildStaticBatch(ar.batch);

    eng::log::info("DungeonMap: %zu rows, %zu rooms, %zu arches, %zu pillar posts, cell %.1f m",
                   mRows.size(), mRooms.size(), mArches.size(), pillarSpots.size(), mCell);
    return true;
}

void DungeonMap::update(eng::Renderer& r, float t) const
{
    // Torch flicker: two incommensurate sines + a fast spike term give an
    // irregular waver, ~+-15% around the base energy. Cheap (one
    // setDiffuseColour per torch) and deterministic.
    for (const Torch& torch : mTorches) {
        if (!torch.lit)
            continue;
        const float p = t * 2.0f * 3.14159265f + torch.phase;
        const float flicker = 1.0f + 0.10f * std::sin(p * 1.7f) +
                              0.05f * std::sin(p * 4.3f + 1.3f);
        r.setLightColour(torch.light, torch.baseColour * flicker);
    }
}

void DungeonMap::updateVisibility(eng::Renderer& r, glm::vec3 cameraPos,
                                  float farDist)
{
    // Inspector mode: wireframe shows every room regardless of culling.
    if (r.envState().wireframe) {
        for (const auto& rm : mRooms)
            r.setStaticBatchVisible(rm.batch, true);
        for (const auto& ar : mArches)
            r.setStaticBatchVisible(ar.batch, true);
        mVisibleRooms.clear(); // force a recompute when culling resumes
        return;
    }

    // Current room(s): the room under the camera, or (over an arch/void) the
    // arch's joined rooms.
    int col, row;
    cellOf(cameraPos.x, cameraPos.z, col, row);
    std::vector<int> current;
    const int rm = roomOfCell(col, row);
    if (rm >= 0) {
        current.push_back(rm);
    } else if (row >= 0 && col >= 0 && row < int(mRows.size()) &&
               col < mStride) {
        const int ai = mCellArch[size_t(row * mStride + col)];
        if (ai >= 0) {
            if (mArches[size_t(ai)].roomA >= 0)
                current.push_back(mArches[size_t(ai)].roomA);
            if (mArches[size_t(ai)].roomB >= 0)
                current.push_back(mArches[size_t(ai)].roomB);
        }
    }
    if (current.empty()) {
        // Camera outside all cells: show everything (never hide on-screen).
        for (const auto& room : mRooms)
            r.setStaticBatchVisible(room.batch, true);
        for (const auto& ar : mArches)
            r.setStaticBatchVisible(ar.batch, true);
        mVisibleRooms.clear();
        return;
    }

    // Recompute only when the current-room set changes.
    std::sort(current.begin(), current.end());
    if (current == mVisibleRooms)
        return;
    mVisibleRooms = current;

    // BFS the portal graph: a room is visible if current, or reachable and its
    // AABB is within farDist of the camera. Arch visible if either room is.
    std::vector<char> vis(mRooms.size(), 0);
    std::vector<int> queue = current;
    for (int c : current)
        vis[size_t(c)] = 1;
    const auto aabbDist = [&](const Room& room) {
        const glm::vec3 p = glm::clamp(cameraPos, room.aabbMin, room.aabbMax);
        return glm::length(p - cameraPos);
    };
    while (!queue.empty()) {
        const int room = queue.back();
        queue.pop_back();
        for (const auto& ar : mArches) {
            int other = -1;
            if (ar.roomA == room) other = ar.roomB;
            else if (ar.roomB == room) other = ar.roomA;
            if (other < 0 || vis[size_t(other)])
                continue;
            if (aabbDist(mRooms[size_t(other)]) <= farDist) {
                vis[size_t(other)] = 1;
                queue.push_back(other);
            }
        }
    }

    for (size_t i = 0; i < mRooms.size(); ++i)
        r.setStaticBatchVisible(mRooms[i].batch, vis[i] != 0);
    for (const auto& ar : mArches) {
        const bool show = (ar.roomA >= 0 && vis[size_t(ar.roomA)]) ||
                          (ar.roomB >= 0 && vis[size_t(ar.roomB)]);
        r.setStaticBatchVisible(ar.batch, show);
    }
}

int DungeonMap::findTorch(glm::vec3 eye, glm::vec3 forward,
                          float maxDist) const
{
    int best = -1;
    float bestDist = maxDist;
    for (size_t i = 0; i < mTorches.size(); ++i) {
        const glm::vec3 to = mTorches[i].tipPos - eye;
        const float dist = glm::length(to);
        if (dist > bestDist || dist < 1e-3f)
            continue;
        // Within ~25 degrees of the view axis counts as "looking at it".
        if (glm::dot(to / dist, forward) < 0.9f)
            continue;
        best = int(i);
        bestDist = dist;
    }
    return best;
}

void DungeonMap::toggleTorch(eng::Renderer& r, int index)
{
    Torch& torch = mTorches[size_t(index)];
    torch.lit = !torch.lit;
    r.setNodeVisible(torch.tip, torch.lit);
    // A snuffed light keeps shining unless zeroed: hidden lights still
    // register with Ogre's light queries in some paths, so kill the
    // colour too (flicker skips unlit torches and won't fight this).
    if (!torch.lit)
        r.setLightColour(torch.light, glm::vec3(0.0f));
}

bool DungeonMap::walkableAt(float x, float z) const
{
    const int col = int(std::floor((x - mOrigin.x) / mCell));
    const int row = int(std::floor((z - mOrigin.z) / mCell));
    const char c = cellAt(col, row);
    if (!isWalkableChar(c))
        return false;
    if (c == 'A') {
        // Only the arch's opening is passable, a band across the cell.
        const float cx = mOrigin.x + (col + 0.5f) * mCell;
        const float cz = mOrigin.z + (row + 0.5f) * mCell;
        return mArchNS[row * 64 + col] ? std::abs(x - cx) < kArchHalfWidth
                                       : std::abs(z - cz) < kArchHalfWidth;
    }
    return true;
}

glm::vec3 DungeonMap::resolveMove(glm::vec3 from, glm::vec3 to,
                                  float radius) const
{
    // Axis-separated slide: accept each axis only if the body's four
    // corner samples stay walkable.
    const auto fits = [&](float x, float z) {
        return walkableAt(x - radius, z - radius) &&
               walkableAt(x + radius, z - radius) &&
               walkableAt(x - radius, z + radius) &&
               walkableAt(x + radius, z + radius);
    };
    glm::vec3 out = from;
    if (fits(to.x, out.z))
        out.x = to.x;
    if (fits(out.x, to.z))
        out.z = to.z;
    out.y = to.y;
    return out;
}
