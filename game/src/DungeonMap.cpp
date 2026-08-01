#include "DungeonMap.h"
#include "DungeonAssemblyGeometry.h"
#include "GameCollision.h"
#include "ParticleEffects.h"

#include <eng/assets/AssetRoot.h>
#include <eng/Log.h>
#include <eng/Physics.h>
#include <eng/Renderer.h>

#include <glm/gtc/quaternion.hpp>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>
#include <unordered_map>

namespace {

// The dungeon shell is built entirely from the modular kit (meshes/kit,
// catalog in kit.toml), which is authored against the Dungeon_Map atlas, so
// one material covers floors, walls, pillars and door frames. The ceiling is
// the floor piece flipped over by the viewer rather than by the mesh, so it
// needs the two-sided variant to be visible from below.
constexpr const char* kKitMaterial = "Game/Kit/Dungeon";
constexpr const char* kKitTwoSided = "Game/Kit/DungeonTwoSided";

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
                      const std::string& kitMeshDir,
                      const std::string& propMeshDir,
                      eng::NodeHandle sceneRoot)
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
                           lampY, kitMeshDir, propMeshDir, sceneRoot);
}

bool DungeonMap::buildFromLayout(eng::Renderer& r, eng::Physics& physics,
                                 gen::Layout layout,
                                 float cell, float wallH,
                                 glm::vec3 lightColour, float lightEnergy,
                                 float lightRange, float lampY,
                                 const std::string& kitMeshDir,
                                 const std::string& propMeshDir,
                                 eng::NodeHandle sceneRoot)
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
    mPlacedProps.clear();
    mPropCatalog.clear();
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
    mExitInDoorway = false;
    for (const WallSide& side : sides) {
        if (mLayout.walkable(exitCell.col + side.dc,
                             exitCell.row + side.dr))
            continue;
        mExit += side.offset * mCell;
        mExitYawDegrees = side.yaw;
        mExitInDoorway = true;
        break;
    }

    mRooms.resize(size_t(mLayout.roomCount()));
    mArches.resize(size_t(mLayout.archCount()));
    mCurrentScratch.reserve(mRooms.size());
    mQueueScratch.reserve(mRooms.size());
    mLastCurrentRooms.reserve(mRooms.size());
    mVisibleScratch.resize(mRooms.size());

    const eng::NodeHandle roomsRoot =
        r.createNode(sceneRoot, glm::vec3(0.0f), "Rooms");
    const eng::NodeHandle archesRoot =
        r.createNode(sceneRoot, glm::vec3(0.0f), "Arches");
    const eng::NodeHandle fixturesRoot =
        r.createNode(sceneRoot, glm::vec3(0.0f), "Fixtures");
    const eng::NodeHandle torchesRoot =
        r.createNode(fixturesRoot, glm::vec3(0.0f), "Torches");

    // One big-region batch per room and per arch (a room is a few cells, so
    // one region = one draw per room material; PVS handles inter-room culling).
    for (size_t i = 0; i < mRooms.size(); ++i) {
        Room& rm = mRooms[i];
        rm.node = r.createNode(roomsRoot, glm::vec3(0.0f),
                               "Room " + std::to_string(i));
        rm.batch = r.createStaticBatch({8.0f, 8.0f, 8.0f});
    }
    for (size_t i = 0; i < mArches.size(); ++i) {
        Arch& ar = mArches[i];
        ar.node = r.createNode(archesRoot, glm::vec3(0.0f),
                               "Arch " + std::to_string(i));
        ar.batch = r.createStaticBatch({8.0f, 8.0f, 8.0f});
    }

    // ------------------------------------------------------------ meshes ---
    // The kit is authored on a 20-unit grid: every architectural piece is
    // centred on X and Z with its base at Y=0, and a wall is exactly as tall
    // as a cell is wide. This dungeon uses a 4 m cell with 3 m walls, so each
    // piece is baked to cell space at load with one non-uniform scale
    // (cell/20, wallH/20, cell/20) and then placed at a cell centre.
    const glm::mat4 kitToCell =
        glm::scale(glm::mat4(1.0f), {mCell / 20.0f, wallH / 20.0f, mCell / 20.0f});
    const eng::MeshHandle floor =
        r.loadObj(kitMeshDir + "Floor_Tiles.obj", &kitToCell);
    // The kit has no ceiling piece (kit.toml's only overhead piece, Arch_Roof,
    // is a vault that tiles its UVs outside 0..1 and needs Game/Kit/Stone). The
    // floor slab is the right shape and the right atlas cell, so the ceiling
    // is the same mesh at wall height drawn two-sided -- cheaper than a
    // mirrored copy, and it shares the floor's mesh and batch.
    const eng::MeshHandle ceiling = floor;
    // Wall_01 is 20 x 20 x 5 kit units. Wall_02 has the same 20 x 20 x 5 body
    // plus a 10-unit-wide pilaster standing 2 units proud on both faces; it
    // replaces the old plaster tile as the "sprinkle for variety" piece and,
    // sharing a body with Wall_01, it shares Wall_01's placement inset.
    const eng::MeshHandle wall = r.loadObj(kitMeshDir + "Wall_01.obj", &kitToCell);
    const eng::MeshHandle wallThick =
        r.loadObj(kitMeshDir + "Wall_02.obj", &kitToCell);
    // Archway: the kit's door frame. Its opening is 10 x 15 kit units, which
    // lands at 2.0 m wide by 2.25 m tall -- a doorway the player walks
    // through rather than the old asset's hole, whose curved jamb needed a
    // full triangle-mesh collider to stop the player clipping into it.
    const eng::MeshHandle arch =
        r.loadObj(kitMeshDir + "Door_Frame_01.obj", &kitToCell);
    // The pillar is the one piece that is not cell-sized: it is 5.65 wide by
    // 30.2 tall -- a column authored to span one and a half cells, so kitToCell
    // would push it 1.5 m through the ceiling. Take the height from the pillar
    // (wallH/30.2, floor-to-ceiling) but the cross-section from the cell
    // (cell/20, the same factor every other piece gets, 1.13 m at cell 4). A
    // uniform wallH/30.2 would be correct arithmetic and a wrong result: 0.56 m
    // across is *narrower than the 1 m wall body it stands against*, so the
    // corner posts read as toothpicks half-swallowed by the masonry.
    const glm::mat4 kitToPillar = glm::scale(
        glm::mat4(1.0f), {mCell / 20.0f, wallH / 30.2f, mCell / 20.0f});
    const eng::MeshHandle pillar =
        r.loadObj(kitMeshDir + "Pillar.obj", &kitToPillar);
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
        int catalogIndex = -1; // -1 when the prop has no tooltip metadata
    };
    std::unordered_map<char, PropDef> markerProps;
    std::vector<PropDef> ambientCatalog;
    int ambientChance = 13;
    std::vector<std::string> roomRoles;
    // Asked for by name, not derived from propMeshDir. Climbing "../.." out of
    // a mesh directory to reach a config file coupled the two layouts: moving
    // the TOMLs under config/ silently produced a path that resolved to
    // nothing, and the level failed to build with the meshes themselves fine.
    const std::string catalogPath =
        eng::assets::resolve("config/dungeon_props.toml").string();
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
            if (const std::string displayName =
                    (*table)["display_name"].value_or(std::string());
                !displayName.empty()) {
                game::PropInfo info;
                info.id = (*table)["id"].value_or(std::string());
                info.displayName = displayName;
                info.category = (*table)["category"].value_or(std::string());
                info.description =
                    (*table)["description"].value_or(std::string());
                info.rarity = (*table)["rarity"].value_or(std::string("common"));
                info.interact = (*table)["interact"].value_or(std::string());
                def.catalogIndex = int(mPropCatalog.size());
                mPropCatalog.push_back(std::move(info));
            }
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
    const auto put = [&](eng::MeshHandle m, glm::vec3 pos, float yawDeg,
                         const char* material = kKitMaterial) {
        r.addToStaticBatch(curBatch, m, material, pos, yawDeg);
    };
    // Emit a static box collider and record its handle for clearPhysics().
    const auto addBox = [&](glm::vec3 centre, glm::vec3 halfExtents) {
        eng::BodyDesc d;
        d.kind = eng::ShapeKind::Box;
        d.halfExtents = halfExtents;
        d.position = centre;
        d.layer = game::layer::Static;
        d.dynamic = false;
        mColliders.push_back(mPhysics->createBody(d));
    };
    // Archway collider: two jambs and a lintel, because the kit's door frame
    // is a rectangular opening. The old arch needed a triangle-mesh collider
    // traced from its render mesh to stop the player clipping a curved jamb;
    // three boxes are cheaper, exact, and readable in the collider overlay.
    //
    // `pos` is the cell corner the render piece is placed at, and `yawDeg`
    // matches it, so the two stay aligned by construction.
    const auto addDoorFrame = [&](glm::vec3 pos, float yawDeg) {
        const float half = mCell * 0.5f;
        const float jamb = mCell * 0.25f;   // opening is the middle half
        const float thick = mCell * 0.125f; // frame depth
        const float openH = wallH * 0.75f;  // lintel starts at 15/20 of the wall
        const bool alongX = std::fabs(yawDeg) < 45.0f;
        const auto place = [&](float across, float y, float halfAcross,
                               float halfY) {
            const glm::vec3 centre =
                alongX ? glm::vec3(pos.x + across, y, pos.z)
                       : glm::vec3(pos.x, y, pos.z + across);
            const glm::vec3 he = alongX
                ? glm::vec3(halfAcross, halfY, thick)
                : glm::vec3(thick, halfY, halfAcross);
            addBox(centre, he);
        };
        place(-(half - jamb * 0.5f), wallH * 0.5f, jamb * 0.5f, wallH * 0.5f);
        place(+(half - jamb * 0.5f), wallH * 0.5f, jamb * 0.5f, wallH * 0.5f);
        place(0.0f, (openH + wallH) * 0.5f, half - jamb, (wallH - openH) * 0.5f);
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
    warm.castShadows = false; // torches no longer drive per-light stencil volumes

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
            // Kit pieces are centred on X/Z, so every placement below is a
            // cell centre (plus, for walls, an outward inset -- see there).
            const float half = mCell * 0.5f;

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

            // Floor slab centred in the cell; the ceiling is the same slab at
            // wall height, drawn two-sided so it is visible from underneath.
            put(floor, {x0 + half, 0.0f, z0 + half}, 0.0f);
            put(ceiling, {x0 + half, wallH, z0 + half}, 0.0f, kKitTwoSided);

            // Floor and ceiling collision slabs (thin boxes centred just
            // outside the walkable volume so the physics surface aligns).
            {
                const float hc = mCell * 0.5f;
                addBox({x0 + hc, -0.05f,      z0 + hc}, {hc, 0.05f, hc});
                addBox({x0 + hc, wallH + 0.05f, z0 + hc}, {hc, 0.05f, hc});
            }

            // An arch cell is an ordinary walkable cell that additionally has
            // a door frame standing across the passage. It therefore takes the
            // same floor, ceiling and boundary walls as every other cell (it
            // used to short-circuit here, which left it with no ceiling tile
            // and no walls on its two solid flanks -- the black band above the
            // doorway and the black slots beside it). Only the frame itself is
            // special, so only the frame is emitted here.
            if (c == 'A') {
                // A north-south passage is crossed by a frame whose 20-unit
                // width runs along X (yaw 0), and vice versa. Door_Frame_01 is
                // 20 wide, so it spans the cell exactly and meets the inset
                // faces of the flanking walls with no step.
                const float archYaw = mLayout.arch(aIdx).northSouth ? 0.0f : 90.0f;
                put(arch, {x0 + half, 0.0f, z0 + half}, archYaw);
                // Door-frame collider: jambs and lintel, placed from the same
                // centre and yaw as the render call above so the collider
                // overlay and the visual agree by construction.
                addDoorFrame({x0 + half, 0.0f, z0 + half}, archYaw);
            }

            // Wall segments on solid/void boundaries, normals facing the
            // cell. Sprinkle the plaster-and-stone-base variant for variety.
            bool wallN = !walkableCell(col, row - 1);
            bool wallS = !walkableCell(col, row + 1);
            bool wallW = !walkableCell(col - 1, row);
            bool wallE = !walkableCell(col + 1, row);
            // The portal face keeps its collider: the membrane is an opaque
            // threshold the player interacts with, not a hole to walk through.
            const bool collideN = wallN;
            const bool collideS = wallS;
            const bool collideW = wallW;
            const bool collideE = wallE;
            // The portal stands *in* a doorway, so the X cell's portal face is
            // built from the kit's door frame rather than a plain wall. Deleting
            // the wall outright (what this used to do, on the assumption that
            // the portal prop carried its own masonry) left a 4 x 3 m hole with
            // a 2.9 m generated surround inside it: a black void ring around
            // the frame, since the cell behind it is solid rock with no floor
            // or ceiling. Door_Frame_01 shares Wall_01's 20 x 20 x 5 body, so
            // it takes the same inset and the same atlas UVs as its neighbours.
            const bool portalCell = c == 'X' && mExitInDoorway;
            const bool portalN = portalCell && mExitYawDegrees == 0.0f;
            const bool portalS = portalCell && mExitYawDegrees == 180.0f;
            const bool portalW = portalCell && mExitYawDegrees == 90.0f;
            const bool portalE = portalCell && mExitYawDegrees == -90.0f;
            // Kit walls are solid slabs, not the zero-thickness planes the old
            // tiles were. Both wall pieces share the same 5-unit-thick body
            // spanning the full 20 x 20 cell face (measured from the .obj:
            // Wall_01 and Wall_02 are both x -10..10, y 0..20, z -2.5..2.5);
            // Wall_02 only adds a 10-unit-wide pilaster that stands proud of
            // that body to z +-4.5 on both sides. So the wall *body* is the
            // same for both, and a single inset serves both pieces: pushed
            // outward (into the solid rock the wall backs onto) by half the
            // body thickness, 5/20*cell/2 = 0.50 m at cell 4, which lands the
            // inner face exactly on the cell boundary where the old plane and
            // the existing 0.05 m collider slab both sit. Insetting Wall_02 by
            // half its *pilaster* depth instead would recess its face 0.40 m
            // behind the boundary, opening a slot at every neighbour and an
            // unfloored, unceilinged pocket that reads as a black void.
            const float wallInset = 2.5f * mCell / 20.0f;
            const bool opening = c == 'A' && aIdx >= 0;
            const bool openingNorthSouth =
                opening && mLayout.arch(aIdx).northSouth;
            const auto visualInset = [&](int dc, int dr) {
                return game::assembly::wallVisualInset(
                    wallInset, opening, openingNorthSouth, dc, dr);
            };
            const auto pick = [&](int salt, bool portalFace) {
                if (portalFace) return arch;
                return (col * 7 + row * 13 + salt) % 4 == 0 ? wallThick : wall;
            };
            if (wallN)
                put(pick(0, portalN), {x0 + half, 0.0f, z0 - visualInset(0, -1)},
                    0.0f);
            if (wallS)
                put(pick(1, portalS),
                    {x0 + half, 0.0f, z0 + mCell + visualInset(0, 1)},
                    180.0f);
            if (wallW)
                put(pick(2, portalW), {x0 - visualInset(-1, 0), 0.0f, z0 + half},
                    90.0f);
            if (wallE)
                put(pick(3, portalE),
                    {x0 + mCell + visualInset(1, 0), 0.0f, z0 + half},
                    -90.0f);

            // Wall collision boxes (thin slabs at each solid boundary face).
            // wallN: -z face of cell (z = z0); wallS: +z face (z = z0+mCell).
            // wallW: -x face (x = x0);          wallE: +x face (x = x0+mCell).
            {
                const float hc  = mCell * 0.5f;
                const float hwH = wallH * 0.5f;
                if (collideN)
                    addBox({x0 + hc,    hwH, z0},         {hc,   hwH, 0.05f});
                if (collideS)
                    addBox({x0 + hc,    hwH, z0 + mCell}, {hc,   hwH, 0.05f});
                if (collideW)
                    addBox({x0,         hwH, z0 + hc},    {0.05f, hwH, hc});
                if (collideE)
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
                if (def.catalogIndex >= 0)
                    mPlacedProps.push_back(
                        {{centre.x, def.height * 1.2f, centre.z},
                         std::max(def.radius, 0.35f), def.catalogIndex});
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
                eng::NodeHandle n = r.createNode(
                    torchesRoot, mount,
                    "Torch (" + std::to_string(col) + "," +
                        std::to_string(row) + ")");
                r.setOrientation(
                    n, glm::angleAxis(glm::radians(yaw + 180.0f),
                                      glm::vec3(0, 1, 0)));
                r.attachMesh(n, torch, "Game/Torch");
                // Flame seat is at the mesh top (0.55 m); light and
                // particles hang off a child there so the tilt carries
                // them along.
                eng::NodeHandle tip = r.createNode(n, {0.0f, 0.55f, 0.0f});
                particlefx::spawnFlame(r, tip);
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

    // Deliberately low-poly shell: pin per-pixel lighting on the kit materials
    // so torch pools stay smooth regardless of preset (vertex-lit presets would
    // sample the sparse grid too coarsely). Higher-poly props keep preset look.
    for (const char* mat : {kKitMaterial, kKitTwoSided})
        r.setMaterialParam(mat, "perPixelLighting", 1.0f);

    eng::log::info("DungeonMap: %zu rows, %zu rooms, %zu arches, %zu torches, "
                   "%zu ambient props, %zu pillar posts, cell %.1f m",
                   mLayout.rows().size(), mRooms.size(), mArches.size(),
                   mTorches.size(), ambientProps, pillarSpots.size(), mCell);
    return true;
}

bool DungeonMap::loadFromRows(eng::Renderer& r, eng::Physics& physics,
                              gen::Layout layout,
                              const std::string& kitMeshDir,
                              const std::string& propMeshDir,
                              eng::NodeHandle sceneRoot)
{
    // Generator grids use the same tile scale and warm-torch defaults as the
    // TOML fallback (assets/game/dungeon.toml [dungeon.light]).
    return buildFromLayout(r, physics, std::move(layout), 4.0f, 3.0f,
                         {lin(1.0f), lin(0.68f), lin(0.34f)}, 4.4f, 6.5f, 1.9f,
                         kitMeshDir, propMeshDir, sceneRoot);
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

void DungeonMap::appendPropTargets(std::vector<GameplayTarget>& targets) const
{
    targets.reserve(targets.size() + mPlacedProps.size());
    for (size_t i = 0; i < mPlacedProps.size(); ++i) {
        const PlacedProp& prop = mPlacedProps[i];
        targets.push_back({TargetKind::Prop, int(i), prop.aimPos, 2.6f,
                           prop.radius, prop.catalogIndex});
    }
}

const game::PropInfo* DungeonMap::propInfo(int catalogIndex) const
{
    if (catalogIndex < 0 || catalogIndex >= int(mPropCatalog.size()))
        return nullptr;
    return &mPropCatalog[size_t(catalogIndex)];
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
