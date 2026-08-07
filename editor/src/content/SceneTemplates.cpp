#include <editor/content/SceneTemplates.h>

#include <editor/content/GridMath.h>
#include <editor/content/RoomBuilder.h>

namespace game::content {
namespace {

// Adds a room and returns whether it worked, so a template reads as a list of
// rooms rather than as error plumbing.
bool addRoom(const GridConfig& grid, const KitCatalog& catalog,
             const RoomSpec& spec, SceneDocument& out, std::string& error)
{
    const std::vector<Entity> pieces =
        buildRoom(grid, catalog, spec, out, error);
    if (!error.empty())
        return false;
    for (const Entity& piece : pieces)
        out.add(piece);
    return true;
}

Entity& emit(SceneDocument& out, const std::string& stem, glm::vec3 position,
             const std::string& name)
{
    Entity entity;
    entity.id = out.allocateId(stem);
    entity.name = name;
    entity.transform.position = position;
    return out.add(std::move(entity));
}

// A torch: a warm point light with a range that reaches about two cells. Placed
// at head height rather than on the floor, where a light reads as a puddle.
void torch(SceneDocument& out, glm::vec3 at, float range = 9.0f)
{
    Entity& light = emit(out, "torch", at + glm::vec3(0.0f, 2.6f, 0.0f), "Torch");
    LightAuthor authored;
    authored.type = LightAuthor::Type::Point;
    authored.colour = {1.0f, 0.62f, 0.30f};
    authored.range = range;
    light.light = authored;
}

} // namespace

const char* sceneTemplateName(SceneTemplate which)
{
    switch (which) {
    case SceneTemplate::Empty: return "Empty";
    case SceneTemplate::Room: return "Single room";
    case SceneTemplate::TechDemo: return "Tech demo";
    case SceneTemplate::Screen: return "2D screen (menu / HUD)";
    }
    return "Empty";
}

bool buildTemplate(SceneTemplate which, const GridConfig& grid,
                   const KitCatalog& catalog, const std::string& id,
                   SceneDocument& out, std::string& error)
{
    error.clear();
    out = SceneDocument{};
    out.id = id.empty() ? "scene.untitled" : id;

    // Every template is playable from the first frame: a scene without a spawn
    // will not cook, and handing someone a new document that refuses to run is
    // a bad first thirty seconds.
    const float cell = grid.cell;
    const auto cellPoint = [&](int col, int row) {
        return cellCentre(grid, col, row, 0.0f);
    };

    // --- 2D screen ---------------------------------------------------------
    // A page, not a world: no spawn, no exit, no floor. Everything is authored
    // in virtual pixels against the page's own extent, so the three quads below
    // are at pixel coordinates and stay that size at every window resolution.
    //
    // Built before the grid helpers are used, because none of them apply -- a
    // cell is a metre of dungeon and this scene's unit is a pixel.
    if (which == SceneTemplate::Screen) {
        Entity camera;
        camera.id = out.allocateId("screen_camera");
        camera.name = "Screen Camera";
        camera.camera = CameraAuthor{};
        ScreenAuthor screen;
        // Contain rather than Height: a menu is the case where nothing authored
        // may ever be cropped, and a first screen is far more often a menu than
        // a HUD. A HUD flips this one field.
        screen.fit = eng::ecs::ScreenCamera::Contain;
        screen.origin = eng::ecs::ScreenCamera::TopLeft;
        camera.screen = screen;
        out.add(std::move(camera));

        // A backing plate and two rows, at pixel coordinates from the top-left
        // origin. Primitives rather than meshes so the template needs no art
        // and cannot dangle a reference.
        const auto plate = [&](const char* stem, const char* name, float x,
                               float y, float w, float h, float layer) {
            Entity panel;
            panel.id = out.allocateId(stem);
            panel.name = name;
            eng::ecs::PrimitiveMesh mesh;
            mesh.kind = eng::ecs::PrimitiveMesh::Kind::Plane;
            mesh.size = {w, h, 1.0f};
            panel.primitive = mesh;
            // +x right, -y down from the top-left corner, z back by whole
            // layers -- ScreenCamera::layerSpacing is what a layer index means.
            panel.transform.position = {x + w * 0.5f, -(y + h * 0.5f),
                                        -layer * 0.5f};
            panel.castShadows = false;
            out.add(std::move(panel));
        };
        plate("panel", "Panel", 24.0f, 24.0f, 380.0f, 192.0f, 0.0f);
        plate("row", "Row 1", 40.0f, 56.0f, 348.0f, 28.0f, 1.0f);
        plate("row", "Row 2", 40.0f, 96.0f, 348.0f, 28.0f, 1.0f);
        return true;
    }

    if (which == SceneTemplate::Empty) {
        Entity& spawn = emit(out, "player_spawn", cellPoint(0, 0), "Player Spawn");
        spawn.playerSpawn = true;
        Entity& exit = emit(out, "exit", cellPoint(2, 0), "Exit");
        exit.exitYawDegrees = 0.0f;
        return true;
    }

    if (which == SceneTemplate::Room) {
        RoomSpec room;
        room.col0 = 0; room.row0 = 0;
        room.col1 = 4; room.row1 = 3;
        if (!addRoom(grid, catalog, room, out, error))
            return false;

        Entity& spawn = emit(out, "player_spawn", cellPoint(1, 2), "Player Spawn");
        spawn.playerSpawn = true;
        Entity& exit = emit(out, "exit", cellPoint(3, 1), "Exit");
        exit.exitYawDegrees = 180.0f;
        torch(out, cellPoint(0, 0));
        torch(out, cellPoint(4, 3));
        return true;
    }

    // --- tech demo ---------------------------------------------------------
    // Two rooms joined by a corridor. Laid out so a first-time reader can see
    // what the editor produces without reading anything: walk out of the entry
    // hall, down the corridor, into the arena.
    //
    // Deliberately exercises EVERY authored feature the pipeline supports, so
    // it doubles as the fixture that proves the whole chain still works: kit
    // pieces, a material override, both light types, markers, an encounter, a
    // pickup, a trigger volume and a collider.

    // Entry hall, 5 x 4 cells.
    RoomSpec hall;
    hall.col0 = 0; hall.row0 = 0;
    hall.col1 = 4; hall.row1 = 3;
    if (!addRoom(grid, catalog, hall, out, error))
        return false;

    // Corridor running north out of the hall, 1 cell wide.
    RoomSpec corridor;
    corridor.col0 = 2; corridor.row0 = -3;
    corridor.col1 = 2; corridor.row1 = -1;
    if (!addRoom(grid, catalog, corridor, out, error))
        return false;

    // Arena beyond it, 7 x 6.
    RoomSpec arena;
    arena.col0 = -1; arena.row0 = -10;
    arena.col1 = 5; arena.row1 = -4;
    if (!addRoom(grid, catalog, arena, out, error))
        return false;

    // Doorways: the walls between hall/corridor and corridor/arena have to come
    // out, or the level is three sealed boxes. Removing them here rather than
    // never placing them keeps the room builder simple -- a room is a room, and
    // the openings are cut afterwards, which is also how the editor works by
    // hand.
    const auto carve = [&](int col, int row, CellPlacement::Edge edge) {
        for (std::size_t i = 0; i < out.entities.size(); ++i) {
            const Entity& entity = out.entities[i];
            if (!entity.cell || entity.prefab.empty())
                continue;
            const KitPiece* piece = catalog.find(entity.prefab);
            if (!piece || piece->socket != Socket::Wall)
                continue;
            if (entity.cell->col == col && entity.cell->row == row &&
                entity.cell->edge == edge) {
                out.remove(entity.id);
                return true;
            }
        }
        return false;
    };
    // The hall's north wall and the corridor's south wall are two different
    // pieces on the same line; both go.
    carve(2, 0, CellPlacement::Edge::North);
    carve(2, -1, CellPlacement::Edge::South);
    carve(2, -3, CellPlacement::Edge::North);
    carve(2, -4, CellPlacement::Edge::South);

    // --- gameplay ----------------------------------------------------------
    Entity& spawn = emit(out, "player_spawn", cellPoint(2, 3), "Entry Spawn");
    spawn.playerSpawn = true;

    Entity& exit = emit(out, "exit", cellPoint(2, -9), "Descent");
    exit.exitYawDegrees = 0.0f;

    Entity& boss = emit(out, "boss_spawn", cellPoint(2, -7), "Arena Adversary");
    boss.marker = "boss.spawn";

    Entity& shrine = emit(out, "shrine", cellPoint(0, 1), "Offering Shrine");
    shrine.marker = "shrine.treasure";

    Entity& goblin = emit(out, "enemy", cellPoint(4, -6), "Sentry");
    goblin.enemySpawn = "goblin";

    Entity& potion = emit(out, "pickup", cellPoint(0, -8), "Cached Potion");
    potion.pickup = "potion";

    // A trigger across the corridor mouth: the arena wakes up when the player
    // commits to it, which is the shape every boss room in this game wants.
    Entity& gate = emit(out, "trigger", cellPoint(2, -4), "Arena Gate");
    gate.trigger = TriggerAuthor{{cell * 0.5f, 2.0f, 0.6f}, "arena.begin"};

    // --- dressing -----------------------------------------------------------
    // A pillar with a collider, to show that a prop can block movement.
    Entity& obelisk = emit(out, "obelisk", cellPoint(1, -6), "Obelisk");
    obelisk.prefab = "kit.pillar";
    obelisk.collider = ColliderAuthor{{0.6f, 3.0f, 0.6f}, {0.0f, 3.0f, 0.0f}};

    // A material override on one floor tile: the pipeline supports per-entity
    // materials, and a scene that never uses one would not prove it.
    for (Entity& entity : out.entities) {
        if (entity.prefab == "kit.floor" && entity.cell &&
            entity.cell->col == 2 && entity.cell->row == -7) {
            entity.material = "Game/Kit/Stone";
            entity.name = "Ritual Floor";
            break;
        }
    }

    // --- lighting -----------------------------------------------------------
    // Torches along the walk, so the route reads without a map.
    torch(out, cellPoint(0, 0));
    torch(out, cellPoint(4, 0));
    torch(out, cellPoint(0, 3));
    torch(out, cellPoint(4, 3));
    torch(out, cellPoint(2, -2), 7.0f); // the corridor
    torch(out, cellPoint(-1, -5));
    torch(out, cellPoint(5, -5));
    torch(out, cellPoint(-1, -9));
    torch(out, cellPoint(5, -9));

    // A cold directional key over the arena: the second light type, and the
    // thing that stops the boss room reading as the same orange as the hall.
    Entity& key = emit(out, "key_light", cellPoint(2, -7) + glm::vec3(0, 9, 0),
                       "Arena Key Light");
    key.transform.rotationDegrees = {-62.0f, 24.0f, 0.0f};
    LightAuthor authored;
    authored.type = LightAuthor::Type::Directional;
    authored.colour = {0.30f, 0.38f, 0.62f};
    authored.range = 0.0f;
    authored.castShadows = true;
    key.light = authored;

    return true;
}

} // namespace game::content
