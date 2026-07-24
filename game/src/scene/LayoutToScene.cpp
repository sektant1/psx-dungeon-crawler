#include "LayoutToScene.h"

#include "GameComponents.h"
#include "MeshSource.h"

#include "../DungeonGen.h"

#include <eng/ecs/Components.h>
#include <eng/LightDesc.h>
#include <eng/Physics.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>

namespace game {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static entt::entity makeMesh(entt::registry& reg,
                              const std::string& path,
                              const std::string& material,
                              glm::vec3 pos,
                              glm::quat rot = glm::quat{1.f, 0.f, 0.f, 0.f},
                              glm::vec3 scale = glm::vec3{1.f})
{
    entt::entity e = reg.create();
    // Name from the mesh filename stem so generated entities list in the editor.
    std::string name = path;
    if (const auto slash = name.find_last_of('/'); slash != std::string::npos)
        name = name.substr(slash + 1);
    if (const auto dot = name.find_last_of('.'); dot != std::string::npos)
        name = name.substr(0, dot);
    reg.emplace<eng::ecs::Name>(e, eng::ecs::Name{name});
    reg.emplace<mapio::MeshSource>(e, path);
    reg.emplace<eng::ecs::MeshRenderer>(e, eng::MeshHandle{}, material, false);
    reg.emplace<eng::ecs::Transform>(e, pos, rot, scale);
    return e;
}

static entt::entity makeCollider(entt::registry& reg,
                                 glm::vec3 pos,
                                 glm::vec3 halfExtents,
                                 glm::quat rot = glm::quat{1.f, 0.f, 0.f, 0.f})
{
    entt::entity e = reg.create();
    reg.emplace<eng::ecs::Transform>(e, pos, rot, glm::vec3{1.f});
    game::Collider col;
    col.shape = eng::ShapeKind::Box;
    col.size  = halfExtents;
    col.layer = eng::BodyLayer::Static;
    reg.emplace<game::Collider>(e, col);
    return e;
}

// ---------------------------------------------------------------------------
// layoutToScene
// ---------------------------------------------------------------------------

void layoutToScene(const gen::Layout& layout, const SceneGenOptions& opts,
                   entt::registry& reg)
{
    const float cell = opts.cell;
    const float halfCell = cell * 0.5f;
    const float wallH = opts.wallHeight;
    const float halfWallH = wallH * 0.5f;

    const int cols = layout.columnCount();
    const int rows = layout.rowCount();

    // Neighbour offsets: N, S, W, E (dx, dy, wallRotYdeg)
    struct Dir { int dx; int dy; float yawDeg; };
    const std::array<Dir, 4> dirs{{
        { 0, -1,   0.f},  // North  (row-1)
        { 0,  1, 180.f},  // South  (row+1)
        {-1,  0, -90.f},  // West   (col-1)
        { 1,  0,  90.f},  // East   (col+1)
    }};

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            if (!layout.walkable(col, row)) continue;

            const char glyph = layout.cellAt(col, row);
            const glm::vec3 centre{col * cell, 0.f, row * cell};

            // --- Floor tile --------------------------------------------------
            // Kit tiles are already cell-sized .obj meshes (DungeonMap places
            // them at scale 1); do not rescale.
            makeMesh(reg,
                     opts.tileDir + "tile_floor.obj",
                     "Game/DungeonFloor",
                     centre);

            // Floor collider (thin slab)
            makeCollider(reg,
                         centre + glm::vec3{0.f, -0.05f, 0.f},
                         glm::vec3{halfCell, 0.05f, halfCell});

            // --- Ceiling tile ------------------------------------------------
            makeMesh(reg,
                     opts.tileDir + "tile_ceiling.obj",
                     "Game/DungeonCeiling",
                     centre + glm::vec3{0.f, wallH, 0.f});

            // --- Wall tiles per exposed edge ---------------------------------
            for (const Dir& d : dirs) {
                const int nc = col + d.dx;
                const int nr = row + d.dy;
                if (layout.walkable(nc, nr)) continue; // open edge, no wall

                // World position of the wall quad (centred at mid-height,
                // offset to the cell boundary).
                const glm::vec3 wallPos = centre
                    + glm::vec3{d.dx * halfCell, halfWallH, d.dy * halfCell};

                const float yawRad = glm::radians(d.yawDeg);
                const glm::quat wallRot = glm::angleAxis(yawRad, glm::vec3{0.f, 1.f, 0.f});

                makeMesh(reg,
                         opts.tileDir + "tile_wall.obj",
                         "Game/DungeonWall",
                         wallPos,
                         wallRot);

                // Wall collider (thin slab perpendicular to face)
                const glm::vec3 wallHalf{
                    (d.dx != 0) ? 0.05f : halfCell,
                    halfWallH,
                    (d.dy != 0) ? 0.05f : halfCell
                };
                makeCollider(reg, wallPos, wallHalf, wallRot);
            }

            // --- Special glyphs ----------------------------------------------
            switch (glyph) {
            case 'S': {
                // Player spawn marker
                entt::entity e = reg.create();
                reg.emplace<eng::ecs::Name>(e, eng::ecs::Name{"PlayerSpawn"});
                reg.emplace<eng::ecs::Transform>(e,
                    centre + glm::vec3{0.f, 0.f, 0.f},
                    glm::quat{1.f, 0.f, 0.f, 0.f},
                    glm::vec3{1.f});
                reg.emplace<game::PlayerSpawn>(e);
                break;
            }
            case 'X': {
                // Level exit
                entt::entity e = reg.create();
                reg.emplace<eng::ecs::Name>(e, eng::ecs::Name{"Exit"});
                reg.emplace<eng::ecs::Transform>(e,
                    centre,
                    glm::quat{1.f, 0.f, 0.f, 0.f},
                    glm::vec3{1.f});
                reg.emplace<game::Exit>(e);
                break;
            }
            case 'L': {
                // Torch light
                entt::entity e = reg.create();
                reg.emplace<eng::ecs::Name>(e, eng::ecs::Name{"Torch"});
                reg.emplace<eng::ecs::Transform>(e,
                    centre + glm::vec3{0.f, wallH * 0.75f, 0.f},
                    glm::quat{1.f, 0.f, 0.f, 0.f},
                    glm::vec3{1.f});
                eng::ecs::LightRef lr;
                lr.desc.type = eng::LightDesc::Type::Point;
                lr.desc.colour = glm::vec3{1.0f, 0.7f, 0.3f};
                lr.desc.range = cell * 2.f;
                reg.emplace<eng::ecs::LightRef>(e, lr);
                // Torch prop mesh
                makeMesh(reg,
                         opts.propDir + "prop_torch.obj",
                         "Game/Torch",
                         centre + glm::vec3{0.f, wallH * 0.6f, 0.f});
                break;
            }
            case 'H': // chest
            case 'B': // barrel
            case 'R': // crate
            case 'V': { // urn
                const char* propMesh =
                    (glyph == 'H') ? "prop_chest.obj" :
                    (glyph == 'B') ? "prop_barrel_p0.obj" :
                    (glyph == 'R') ? "prop_crate.obj" : "prop_vase_p0.obj";
                const char* propMat =
                    (glyph == 'H') ? "Game/PropChest" :
                    (glyph == 'B') ? "Game/PropPlanks" :
                    (glyph == 'R') ? "Game/PropMarket" : "Game/PropTerracotta";
                makeMesh(reg, opts.propDir + propMesh, propMat, centre);
                // Prop collider
                makeCollider(reg, centre + glm::vec3{0.f, 0.5f, 0.f},
                             glm::vec3{0.4f, 0.5f, 0.4f});
                break;
            }
            default:
                break;
            }
        }
    }
}

} // namespace game
