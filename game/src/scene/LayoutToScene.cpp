#include "LayoutToScene.h"
#include "../DungeonAssemblyGeometry.h"
#include "GameCollision.h"

#include "GameComponents.h"
#include <eng/ecs/MeshSource.h>

#include "../DungeonGen.h"

#include <eng/ecs/Components.h>
#include <eng/LightDesc.h>
#include <eng/Physics.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cmath>
#include <filesystem>

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
    reg.emplace<eng::ecs::MeshSource>(e, path);
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
    col.layer = game::layer::Static;
    reg.emplace<game::Collider>(e, col);
    return e;
}

// ---------------------------------------------------------------------------
// layoutToScene
// ---------------------------------------------------------------------------

bool layoutToScene(const gen::Layout& layout, const SceneGenOptions& opts,
                   entt::registry& reg)
{
    if (!layout.valid() || !std::isfinite(opts.cell) || opts.cell <= 0.0f ||
        !std::isfinite(opts.wallHeight) || opts.wallHeight <= 0.0f ||
        opts.kitDir.empty() || opts.propDir.empty() ||
        std::filesystem::path(opts.kitDir).is_absolute() ||
        std::filesystem::path(opts.propDir).is_absolute())
        return false;

    const float cell = opts.cell;
    const float halfCell = cell * 0.5f;
    const float wallH = opts.wallHeight;
    const float halfWallH = wallH * 0.5f;

    const int cols = layout.columnCount();
    const int rows = layout.rowCount();
    for (int row = 0; row < rows; ++row)
        for (int col = 0; col < cols; ++col)
            if (layout.cellAt(col, row) == 'A' && layout.archAt(col, row) < 0)
                return false;
    const gen::Cell anchor = layout.anchor();
    const glm::vec3 origin{-(anchor.col + 0.5f) * cell, 0.0f,
                           -(anchor.row + 0.5f) * cell};

    // Kit pieces are authored on a 20-unit grid (see game/assets/kit.toml),
    // centred on X/Z with their base at Y=0. DungeonMap bakes that into the
    // mesh at load; here there is no renderer, so the same conversion rides on
    // the entity's Transform scale instead.
    const glm::vec3 kitScale{cell / 20.f, wallH / 20.f, cell / 20.f};
    // Walls are solid slabs 5 kit units thick, so a wall sits half its
    // thickness *outside* the cell boundary it faces; otherwise it would
    // straddle the boundary and eat into the room. 5/20 * cell / 2 = 0.5 m at
    // cell 4. The collider stays a thin slab on the boundary itself.
    const float wallInset = 2.5f * cell / 20.f;

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
            const int archIndex = layout.archAt(col, row);
            const bool opening = glyph == 'A' && archIndex >= 0;
            const bool openingNorthSouth =
                opening && layout.arch(archIndex).northSouth;
            const glm::vec3 centre =
                origin + glm::vec3{(col + 0.5f) * cell, 0.f,
                                   (row + 0.5f) * cell};

            // --- Floor slab --------------------------------------------------
            makeMesh(reg,
                     opts.kitDir + "Floor_Tiles.obj",
                     "Kit/Dungeon",
                     centre,
                     glm::quat{1.f, 0.f, 0.f, 0.f},
                     kitScale);
            makeCollider(reg,
                         centre + glm::vec3{0.f, wallH + 0.05f, 0.f},
                         glm::vec3{halfCell, 0.05f, halfCell});

            // Floor collider (thin slab)
            makeCollider(reg,
                         centre + glm::vec3{0.f, -0.05f, 0.f},
                         glm::vec3{halfCell, 0.05f, halfCell});

            // --- Ceiling ------------------------------------------------------
            // The kit has no ceiling piece: reuse the floor slab at wall
            // height with the two-sided material so it reads from below.
            makeMesh(reg,
                     opts.kitDir + "Floor_Tiles.obj",
                     "Kit/DungeonTwoSided",
                     centre + glm::vec3{0.f, wallH, 0.f},
                     glm::quat{1.f, 0.f, 0.f, 0.f},
                     kitScale);

            // --- Walls per exposed edge ---------------------------------
            for (const Dir& d : dirs) {
                const int nc = col + d.dx;
                const int nr = row + d.dy;
                if (layout.walkable(nc, nr)) continue; // open edge, no wall

                // Collider sits on the cell boundary at mid-height; the mesh
                // stands on the floor (base at Y=0) and is pushed one half
                // thickness further out so its inner face lands on that
                // boundary.
                const glm::vec3 colliderPos = centre
                    + glm::vec3{d.dx * halfCell, halfWallH, d.dy * halfCell};
                const float visualInset = assembly::wallVisualInset(
                    wallInset, opening, openingNorthSouth, d.dx, d.dy);
                const glm::vec3 wallPos = centre
                    + glm::vec3{d.dx * (halfCell + visualInset), 0.f,
                                d.dy * (halfCell + visualInset)};

                const float yawRad = glm::radians(d.yawDeg);
                const glm::quat wallRot = glm::angleAxis(yawRad, glm::vec3{0.f, 1.f, 0.f});

                makeMesh(reg,
                         opts.kitDir + "Wall_01.obj",
                         "Kit/Dungeon",
                         wallPos,
                         wallRot,
                         kitScale);

                // Wall collider (thin slab perpendicular to face)
                const glm::vec3 wallHalf{
                    (d.dx != 0) ? 0.05f : halfCell,
                    halfWallH,
                    (d.dy != 0) ? 0.05f : halfCell
                };
                makeCollider(reg, colliderPos, wallHalf, wallRot);
            }

            // --- Special glyphs ----------------------------------------------
            switch (glyph) {
            case 'A': {
                const bool northSouth = layout.arch(archIndex).northSouth;
                const float yaw = northSouth ? 0.0f : 90.0f;
                const glm::quat rotation = glm::angleAxis(
                    glm::radians(yaw), glm::vec3{0.f, 1.f, 0.f});
                makeMesh(reg, opts.kitDir + "Door_Frame_01.obj",
                         "Kit/Dungeon", centre, rotation, kitScale);

                const float jamb = cell * 0.25f;
                const float thickness = cell * 0.125f;
                const float openHeight = wallH * 0.75f;
                const bool alongX = northSouth;
                const auto frameBox = [&](float across, float y,
                                          float halfAcross, float halfY) {
                    const glm::vec3 position = alongX
                        ? centre + glm::vec3{across, y, 0.0f}
                        : centre + glm::vec3{0.0f, y, across};
                    const glm::vec3 halfExtents = alongX
                        ? glm::vec3{halfAcross, halfY, thickness}
                        : glm::vec3{thickness, halfY, halfAcross};
                    makeCollider(reg, position, halfExtents);
                };
                frameBox(-(halfCell - jamb * 0.5f), wallH * 0.5f,
                         jamb * 0.5f, wallH * 0.5f);
                frameBox(+(halfCell - jamb * 0.5f), wallH * 0.5f,
                         jamb * 0.5f, wallH * 0.5f);
                frameBox(0.0f, (openHeight + wallH) * 0.5f,
                         halfCell - jamb, (wallH - openHeight) * 0.5f);
                break;
            }
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
    return true;
}

} // namespace game
