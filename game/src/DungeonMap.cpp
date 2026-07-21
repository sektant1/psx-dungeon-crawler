#include "DungeonMap.h"

#include <eng/Log.h>
#include <eng/Physics.h>
#include <eng/Renderer.h>

#include <glm/gtc/quaternion.hpp>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <unordered_map>

namespace {

constexpr const char* kTileMaterial = "Game/DungeonTile";
// Half-width of the stone_archway's walkable opening (measured: the gap
// spans 1.2..2.8 across the 4 m piece).
constexpr float kArchHalfWidth = 0.8f;

float lin(float srgb) { return std::pow(srgb, 2.2f); }

} // namespace

char DungeonMap::cellAt(int col, int row) const
{
    return mLayout.cellAt(col, row);
}

bool DungeonMap::walkableCell(int col, int row) const
{
    return mLayout.walkable(col, row);
}

void DungeonMap::cellOf(float x, float z, int& col, int& row) const
{
    col = int(std::floor((x - mOrigin.x) / mCell));
    row = int(std::floor((z - mOrigin.z) / mCell));
}

int DungeonMap::roomOfCell(int col, int row) const
{
    return mLayout.roomAt(col, row);
}

bool DungeonMap::debugArchNorthSouth(int col, int row) const
{
    const int index = mLayout.archAt(col, row);
    return index >= 0 && mLayout.arch(index).northSouth;
}

bool DungeonMap::load(eng::Renderer& r, eng::Physics& physics,
                      const std::string& tomlPath,
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

    const toml::array* rowsArr = (*dungeon)["rows"].as_array();
    if (!rowsArr || rowsArr->empty()) {
        eng::log::error("DungeonMap: missing 'rows' array");
        return false;
    }
    std::vector<std::string> rows;
    for (const auto& e : *rowsArr)
        rows.push_back(e.value_or(std::string()));

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

    return buildFromLayout(r, physics, gen::Layout::fromRows(std::move(rows)),
                           mCell, wallH, lightColour, lightEnergy, lightRange,
                           lampY, tileMeshDir, propMeshDir);
}

bool DungeonMap::buildFromLayout(eng::Renderer& r, eng::Physics& physics,
                                 gen::Layout layout,
                                 float cell, float wallH,
                                 glm::vec3 lightColour, float lightEnergy,
                                 float lightRange, float lampY,
                                 const std::string& tileMeshDir,
                                 const std::string& propMeshDir)
{
    // A DungeonMap can be rebuilt in place (editor/reload and future level
    // previews). None of its previous segmentation, portals or torch state
    // may leak into the new tile grid.
    clearPhysics(); // free any colliders from a previous build
    mPhysics = &physics;
    mRooms.clear();
    mArches.clear();
    mTorches.clear();
    mPropBlockers.clear();
    mLastCurrentRooms.clear();
    mCurrentScratch.clear();
    mQueueScratch.clear();
    mVisibleScratch.clear();

    mCell = cell;
    mLayout = std::move(layout);
    if (!mLayout.valid()) {
        eng::log::error("DungeonMap: invalid layout: %s", mLayout.error().c_str());
        return false;
    }
    const gen::Cell anchorCell = mLayout.anchor();
    const gen::Cell spawnCell = mLayout.spawn();
    const gen::Cell exitCell = mLayout.exit();
    mOrigin = {-(anchorCell.col + 0.5f) * mCell, 0.0f,
               -(anchorCell.row + 0.5f) * mCell};
    const auto worldCell = [&](gen::Cell at) {
        return glm::vec3{mOrigin.x + (at.col + 0.5f) * mCell, 0.0f,
                         mOrigin.z + (at.row + 0.5f) * mCell};
    };
    mSpawn = worldCell(spawnCell);
    mExit = worldCell(exitCell);
    // X marks the walkable cell inside a portal wall. Snap the visual root to
    // the nearest solid boundary and face it into the room; this makes the
    // portal replace a wall section instead of floating at the cell centre.
    struct WallSide { int dc, dr; glm::vec3 offset; float yaw; };
    const WallSide sides[] = {
        {0, -1, {0.0f, 0.0f, -0.5f},   0.0f},
        {1,  0, {0.5f, 0.0f,  0.0f}, -90.0f},
        {0,  1, {0.0f, 0.0f,  0.5f}, 180.0f},
        {-1, 0, {-0.5f,0.0f,  0.0f},  90.0f},
    };
    for (const WallSide& side : sides) {
        if (mLayout.walkable(exitCell.col + side.dc,
                             exitCell.row + side.dr))
            continue;
        mExit += side.offset * mCell;
        mExitYawDegrees = side.yaw;
        break;
    }

    mRooms.resize(size_t(mLayout.roomCount()));
    mArches.resize(size_t(mLayout.archCount()));
    mCurrentScratch.reserve(mRooms.size());
    mQueueScratch.reserve(mRooms.size());
    mLastCurrentRooms.reserve(mRooms.size());
    mVisibleScratch.resize(mRooms.size());

    // One big-region batch per room and per arch (a room is a few cells, so
    // one region = one draw per room material; PVS handles inter-room culling).
    for (auto& rm : mRooms)
        rm.batch = r.createStaticBatch({8.0f, 8.0f, 8.0f});
    for (auto& ar : mArches)
        ar.batch = r.createStaticBatch({8.0f, 8.0f, 8.0f});

    // ------------------------------------------------------------ meshes ---
    const eng::MeshHandle floor = r.loadObj(tileMeshDir + "tile_floor.obj");
    const eng::MeshHandle ceiling = r.loadObj(tileMeshDir + "tile_ceiling.obj");
    const eng::MeshHandle wall = r.loadObj(tileMeshDir + "tile_wall.obj");
    const eng::MeshHandle wallPlaster =
        r.loadObj(tileMeshDir + "tile_wall_plaster.obj");
    const eng::MeshHandle arch = r.loadObj(tileMeshDir + "tile_arch.obj");
    const eng::MeshHandle pillar = r.loadObj(tileMeshDir + "tile_pillar.obj");
    const eng::MeshHandle torch = r.loadObj(propMeshDir + "prop_torch.obj");
    struct PropPart {
        eng::MeshHandle mesh;
        std::string material;
        float y = 0.0f;
    };
    struct PropDef {
        std::vector<PropPart> parts;
        std::vector<std::string> roles;
        float radius = 0.4f;
        float height = 0.5f;
    };
    std::unordered_map<char, PropDef> markerProps;
    std::vector<PropDef> ambientCatalog;
    int ambientChance = 13;
    std::vector<std::string> roomRoles;
    const std::string catalogPath = propMeshDir + "../../dungeon_props.toml";
    toml::parse_result catalogResult = toml::parse_file(catalogPath);
    if (!catalogResult) {
        eng::log::error("DungeonMap: prop catalog failed: %s",
                        std::string(catalogResult.error().description()).c_str());
        return false;
    }
    const toml::table& catalog = catalogResult.table();
    ambientChance = int(catalog["ambient"]["wall_edge_chance"].value_or(13));
    if (const toml::array* roles = catalog["ambient"]["room_roles"].as_array())
        for (const toml::node& role : *roles)
            if (auto name = role.value<std::string>()) roomRoles.push_back(*name);
    if (const toml::array* definitions = catalog["prop"].as_array()) {
        for (const toml::node& node : *definitions) {
            const toml::table* table = node.as_table();
            if (!table) continue;
            const toml::array* meshes = (*table)["meshes"].as_array();
            const toml::array* materials = (*table)["materials"].as_array();
            if (!meshes || !materials || meshes->size() != materials->size())
                continue;
            const toml::array* yValues = (*table)["y"].as_array();
            PropDef def;
            def.radius = float((*table)["radius"].value_or(0.4));
            def.height = float((*table)["height"].value_or(0.5));
            if (const toml::array* roles = (*table)["roles"].as_array())
                for (const toml::node& role : *roles)
                    if (auto name = role.value<std::string>())
                        def.roles.push_back(*name);
            for (size_t i = 0; i < meshes->size(); ++i) {
                const std::string mesh = (*meshes)[i].value_or(std::string());
                const std::string material =
                    (*materials)[i].value_or(std::string());
                const float y = yValues && i < yValues->size()
                                    ? float((*yValues)[i].value_or(0.0)) : 0.0f;
                if (!mesh.empty() && !material.empty())
                    def.parts.push_back({r.loadObj(propMeshDir + mesh), material, y});
            }
            if (def.parts.empty()) continue;
            const std::string marker = (*table)["marker"].value_or(std::string());
            if (marker.size() == 1)
                markerProps[marker[0]] = def;
            if ((*table)["ambient"].value_or(false))
                ambientCatalog.push_back(std::move(def));
        }
    }
    if (markerProps.empty() || ambientCatalog.empty()) {
        eng::log::error("DungeonMap: prop catalog has no marker or ambient props");
        return false;
    }

    eng::StaticBatchHandle curBatch{}; // set per cell in the grid loop
    const auto put = [&](eng::MeshHandle m, glm::vec3 pos, float yawDeg) {
        r.addToStaticBatch(curBatch, m, kTileMaterial, pos, yawDeg);
    };
    // Emit a static box collider and record its handle for clearPhysics().
    const auto addBox = [&](glm::vec3 centre, glm::vec3 halfExtents) {
        eng::BodyDesc d;
        d.kind = eng::ShapeKind::Box;
        d.halfExtents = halfExtents;
        d.position = centre;
        d.layer = eng::BodyLayer::Static;
        d.dynamic = false;
        mColliders.push_back(mPhysics->createBody(d));
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

    std::set<std::pair<int, int>> pillarSpots; // corner keys, de-duplicated
    size_t ambientProps = 0;

    for (int row = 0; row < mLayout.rowCount(); ++row) {
        for (int col = 0; col < mLayout.columnCount(); ++col) {
            const char c = mLayout.cellAt(col, row);
            if (!mLayout.walkable(col, row))
                continue;
            // Cell rect: x in [x0, x0+cell], z in [z0, z0+cell].
            const float x0 = mOrigin.x + col * mCell;
            const float z0 = mOrigin.z + row * mCell;

            // Route this cell's tiles into its room (or arch) batch.
            const int aIdx = mLayout.archAt(col, row);
            const int rIdx = mLayout.roomAt(col, row);
            if (aIdx >= 0) {
                curBatch = mArches[size_t(aIdx)].batch;
            } else if (rIdx >= 0) {
                curBatch = mRooms[size_t(rIdx)].batch;
                growRoomAabb(rIdx, {x0, 0.0f, z0},
                             {x0 + mCell, wallH, z0 + mCell});
            }

            if (c == 'A') {
                // The arch asset provides the side blocks and roof but its
                // base has holes in the walkable opening. Supply the same
                // floor slab as ordinary cells, raised a hair above any
                // coincident asset faces to prevent z-fighting.
                put(floor, {x0, 0.002f, z0 + mCell}, 0.0f);
                if (mLayout.arch(aIdx).northSouth)
                    put(arch, {x0, 0.0f, z0 + mCell}, 0.0f);
                else
                    put(arch, {x0 + mCell, 0.0f, z0 + mCell}, 90.0f);

                // Arch colliders: two solid side-blocks flanking the opening,
                // mirroring the circleFits() arch logic. No full-wall boxes.
                // TODO: replace with createMeshBody for mesh-accurate arches
                //       (e.g. stairs/ramps) once that seam is wired up.
                {
                    const bool ns = mLayout.arch(aIdx).northSouth;
                    const float x1 = x0 + mCell;
                    const float z1 = z0 + mCell;
                    const float mid  = ns ? x0 + mCell * 0.5f : z0 + mCell * 0.5f;
                    const float lo   = mid - kArchHalfWidth;
                    const float hi   = mid + kArchHalfWidth;
                    if (ns) {
                        // Arch runs N–S: side blocks span [x0,lo] and [hi,x1].
                        const float hw0 = (lo - x0) * 0.5f;
                        const float hw1 = (x1 - hi) * 0.5f;
                        addBox({x0 + hw0, wallH * 0.5f, z0 + mCell * 0.5f},
                               {hw0, wallH * 0.5f, mCell * 0.5f});
                        addBox({hi + hw1, wallH * 0.5f, z0 + mCell * 0.5f},
                               {hw1, wallH * 0.5f, mCell * 0.5f});
                    } else {
                        // Arch runs E–W: side blocks span [z0,lo] and [hi,z1].
                        const float hz0 = (lo - z0) * 0.5f;
                        const float hz1 = (z1 - hi) * 0.5f;
                        addBox({x0 + mCell * 0.5f, wallH * 0.5f, z0 + hz0},
                               {mCell * 0.5f, wallH * 0.5f, hz0});
                        addBox({x0 + mCell * 0.5f, wallH * 0.5f, hi + hz1},
                               {mCell * 0.5f, wallH * 0.5f, hz1});
                    }
                }
                continue;
            }

            // Floor spans x[0,cell] z[-cell,0] from its node; ceiling is the
            // same footprint mirrored downward, raised to wall height.
            put(floor, {x0, 0.0f, z0 + mCell}, 0.0f);
            put(ceiling, {x0, wallH, z0 + mCell}, 0.0f);

            // Floor and ceiling collision slabs (thin boxes centred just
            // outside the walkable volume so the physics surface aligns).
            {
                const float hc = mCell * 0.5f;
                addBox({x0 + hc, -0.05f,      z0 + hc}, {hc, 0.05f, hc});
                addBox({x0 + hc, wallH + 0.05f, z0 + hc}, {hc, 0.05f, hc});
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

            // Wall collision boxes (thin slabs at each solid boundary face).
            // wallN: -z face of cell (z = z0); wallS: +z face (z = z0+mCell).
            // wallW: -x face (x = x0);          wallE: +x face (x = x0+mCell).
            {
                const float hc  = mCell * 0.5f;
                const float hwH = wallH * 0.5f;
                if (wallN)
                    addBox({x0 + hc,    hwH, z0},         {hc,   hwH, 0.05f});
                if (wallS)
                    addBox({x0 + hc,    hwH, z0 + mCell}, {hc,   hwH, 0.05f});
                if (wallW)
                    addBox({x0,         hwH, z0 + hc},    {0.05f, hwH, hc});
                if (wallE)
                    addBox({x0 + mCell, hwH, z0 + hc},    {0.05f, hwH, hc});
            }

            // Wooden posts on inner wall corners.
            if (wallN && wallW) pillarSpots.insert({col, row});
            if (wallN && wallE) pillarSpots.insert({col + 1, row});
            if (wallS && wallW) pillarSpots.insert({col, row + 1});
            if (wallS && wallE) pillarSpots.insert({col + 1, row + 1});

            // Authored and generated dressing uses one cell marker per prop.
            // A stable grid hash selects one of four cardinal rotations, so
            // the same layout always has the same readable silhouette.
            if (const auto found = markerProps.find(c); found != markerProps.end()) {
                const PropDef& def = found->second;
                const float yaw = float((col * 17 + row * 31) & 3) * 90.0f;
                const glm::vec3 centre{x0 + mCell * 0.5f, 0.0f,
                                       z0 + mCell * 0.5f};
                for (const PropPart& part : def.parts)
                    r.addToStaticBatch(curBatch, part.mesh, part.material,
                                       centre + glm::vec3(0, part.y, 0), yaw);
                mPropBlockers.push_back({centre.x - def.radius,
                                         centre.z - def.radius,
                                         centre.x + def.radius,
                                         centre.z + def.radius});
                addBox({centre.x, def.height, centre.z},
                       {def.radius, def.height, def.radius});
            }

            // Ambient dressing is deliberately not encoded in the layout.
            // It is derived from stable cell coordinates, so BSP and hand-
            // authored rooms gain atmosphere without polluting gameplay data.
            // Only ordinary wall-edge floor cells qualify; the centre lane,
            // arches, markers and explicitly placed props remain untouched.
            const uint32_t decorHash = uint32_t(col * 73856093) ^
                                       uint32_t(row * 19349663) ^
                                       uint32_t(mLayout.rowCount() * 83492791);
            if (c == '.' && (wallN || wallS || wallW || wallE) &&
                !ambientCatalog.empty() &&
                decorHash % uint32_t(std::max(1, ambientChance)) == 0u) {
                glm::vec3 pos{x0 + mCell * 0.5f, 0.0f,
                              z0 + mCell * 0.5f};
                float yaw = 0.0f;
                const float edge = mCell * 0.34f;
                if (wallN) { pos.z -= edge; yaw = 0.0f; }
                else if (wallS) { pos.z += edge; yaw = 180.0f; }
                else if (wallW) { pos.x -= edge; yaw = 90.0f; }
                else { pos.x += edge; yaw = -90.0f; }

                const std::string role = roomRoles.empty() || rIdx < 0
                    ? std::string()
                    : roomRoles[size_t(rIdx) % roomRoles.size()];
                std::vector<size_t> eligible;
                eligible.reserve(ambientCatalog.size());
                for (size_t i = 0; i < ambientCatalog.size(); ++i)
                    if (ambientCatalog[i].roles.empty() || role.empty() ||
                        std::find(ambientCatalog[i].roles.begin(),
                                  ambientCatalog[i].roles.end(), role) !=
                            ambientCatalog[i].roles.end())
                        eligible.push_back(i);
                if (eligible.empty())
                    for (size_t i = 0; i < ambientCatalog.size(); ++i)
                        eligible.push_back(i);
                const size_t choice = size_t(
                    decorHash / uint32_t(std::max(1, ambientChance))) % eligible.size();
                const PropDef& def = ambientCatalog[eligible[choice]];
                for (const PropPart& part : def.parts)
                    r.addToStaticBatch(curBatch, part.mesh, part.material,
                                       pos + glm::vec3(0, part.y, 0), yaw);
                mPropBlockers.push_back({pos.x - def.radius, pos.z - def.radius,
                                         pos.x + def.radius, pos.z + def.radius});
                addBox({pos.x, def.height, pos.z},
                       {def.radius, def.height, def.radius});
                ++ambientProps;
            }

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
                    yaw = 90.0f;
                } else if (wallE) {
                    mount.x = x0 + mCell - in;
                    yaw = -90.0f;
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
                r.attachParticles(tip, "Game/TorchGlow");
                r.attachParticles(tip, "Game/TorchFire");
                r.attachParticles(tip, "Game/TorchAsh");
                eng::NodeHandle smoke =
                    r.createNode(tip, {0.0f, 0.12f, 0.0f});
                r.attachParticles(smoke, "Game/FireSmoke");
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
                if (cr < 0 || cc < 0 || cr >= mLayout.rowCount() ||
                    cc >= mLayout.columnCount())
                    continue;
                const int ai = mLayout.archAt(cc, cr);
                if (ai >= 0) { curBatch = mArches[size_t(ai)].batch; break; }
            }
        if (curBatch.valid())
            put(pillar, {mOrigin.x + pc * mCell, 0.0f, mOrigin.z + pr * mCell},
                0.0f);
        else
            eng::log::warn("DungeonMap: pillar at col %d row %d has no room/arch"
                           " neighbour; skipped", pc, pr);
    }

    for (const auto& rm : mRooms)
        r.buildStaticBatch(rm.batch);
    for (const auto& ar : mArches)
        r.buildStaticBatch(ar.batch);

    eng::log::info("DungeonMap: %zu rows, %zu rooms, %zu arches, %zu torches, "
                   "%zu ambient props, %zu pillar posts, cell %.1f m",
                   mLayout.rows().size(), mRooms.size(), mArches.size(),
                   mTorches.size(), ambientProps, pillarSpots.size(), mCell);
    return true;
}

bool DungeonMap::loadFromRows(eng::Renderer& r, eng::Physics& physics,
                              gen::Layout layout,
                              const std::string& tileMeshDir,
                              const std::string& propMeshDir)
{
    // Generator grids use the same tile scale and warm-torch defaults as the
    // TOML fallback (game/assets/dungeon.toml [dungeon.light]).
    return buildFromLayout(r, physics, std::move(layout), 4.0f, 3.0f,
                         {lin(1.0f), lin(0.68f), lin(0.34f)}, 4.4f, 6.5f, 1.9f,
                         tileMeshDir, propMeshDir);
}

void DungeonMap::clearPhysics()
{
    if (!mPhysics)
        return;
    for (eng::BodyHandle h : mColliders)
        mPhysics->removeBody(h);
    mColliders.clear();
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
        mLastCurrentRooms.clear(); // force a recompute when culling resumes
        return;
    }

    // Current room(s): the room under the camera, or (over an arch/void) the
    // arch's joined rooms.
    int col, row;
    cellOf(cameraPos.x, cameraPos.z, col, row);
    mCurrentScratch.clear();
    std::vector<int>& current = mCurrentScratch;
    const int rm = roomOfCell(col, row);
    if (rm >= 0) {
        current.push_back(rm);
    } else if (row >= 0 && col >= 0 && row < mLayout.rowCount() &&
               col < mLayout.columnCount()) {
        const int ai = mLayout.archAt(col, row);
        if (ai >= 0) {
            const gen::Arch& arch = mLayout.arch(ai);
            if (arch.roomA >= 0)
                current.push_back(arch.roomA);
            if (arch.roomB >= 0)
                current.push_back(arch.roomB);
        }
    }
    if (current.empty()) {
        // Camera outside all cells: show everything (never hide on-screen).
        for (const auto& room : mRooms)
            r.setStaticBatchVisible(room.batch, true);
        for (const auto& ar : mArches)
            r.setStaticBatchVisible(ar.batch, true);
        mLastCurrentRooms.clear();
        return;
    }

    // Recompute only when the current-room set changes.
    std::sort(current.begin(), current.end());
    if (current == mLastCurrentRooms)
        return;
    mLastCurrentRooms = current;

    // BFS the portal graph: a room is visible if current, or reachable and its
    // AABB is within farDist of the camera. Arch visible if either room is.
    std::fill(mVisibleScratch.begin(), mVisibleScratch.end(), 0);
    std::vector<char>& vis = mVisibleScratch;
    mQueueScratch.assign(current.begin(), current.end());
    std::vector<int>& queue = mQueueScratch;
    for (int c : current)
        vis[size_t(c)] = 1;
    const auto aabbDist = [&](const Room& room) {
        const glm::vec3 p = glm::clamp(cameraPos, room.aabbMin, room.aabbMax);
        return glm::length(p - cameraPos);
    };
    while (!queue.empty()) {
        const int room = queue.back();
        queue.pop_back();
        for (size_t ai = 0; ai < mArches.size(); ++ai) {
            const gen::Arch& ar = mLayout.arch(int(ai));
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
    for (size_t ai = 0; ai < mArches.size(); ++ai) {
        const gen::Arch& ar = mLayout.arch(int(ai));
        const bool show = (ar.roomA >= 0 && vis[size_t(ar.roomA)]) ||
                          (ar.roomB >= 0 && vis[size_t(ar.roomB)]);
        r.setStaticBatchVisible(mArches[ai].batch, show);
    }
}

void DungeonMap::appendTorchTargets(std::vector<GameplayTarget>& targets) const
{
    targets.reserve(targets.size() + mTorches.size());
    for (size_t i = 0; i < mTorches.size(); ++i)
        targets.push_back({TargetKind::Torch, int(i), mTorches[i].tipPos, 2.5f});
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
