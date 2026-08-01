#include "LayoutToScene.h"
#include "../src/DungeonAssemblyGeometry.h"

#include "GameComponents.h"
#include <eng/ecs/components/MeshSource.h>

#include "../DungeonGen.h"

#include <eng/ecs/Components.h>

#include <cstdlib>
#include <cmath>
#include <iostream>

using namespace game;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "LayoutToSceneTests: " << m << '\n'; std::exit(1); }
}

int main()
{
    // Minimal known-good grid (same shape as LevelDocumentTests fixture).
    gen::Layout layout = gen::Layout::fromRows(
        {"#######", "#S.C.X#", "#######"}, /*requireExit=*/true);
    require(layout.valid(), "fixture layout validates");

    entt::registry reg;
    SceneGenOptions opts;
    opts.cell = 4.0f;
    require(layoutToScene(layout, opts, reg), "valid layout converts to scene");

    int slabs = 0, spawns = 0, exits = 0, colliders = 0, meshes = 0;
    reg.view<eng::ecs::MeshSource>().each([&](entt::entity, const eng::ecs::MeshSource& s) {
        ++meshes;
        if (s.path.find("Floor_Tiles") != std::string::npos) ++slabs;
    });
    reg.view<game::PlayerSpawn>().each([&](auto...) { ++spawns; });
    reg.view<game::Exit>().each([&](auto...) { ++exits; });
    reg.view<game::Collider>().each([&](auto...) { ++colliders; });

    require(slabs >= 8, "kit floor slab + ceiling slab per walkable cell");
    require(spawns == 1, "exactly one PlayerSpawn");
    require(exits == 1, "exactly one Exit");
    require(colliders > 0, "colliders emitted (floor + walls)");
    require(meshes >= slabs, "mesh entities include floors + walls + ceilings");
    reg.view<eng::ecs::MeshSource>().each(
        [&](entt::entity, const eng::ecs::MeshSource& source) {
            require(!source.path.empty() && source.path.front() != '/',
                    "generated mesh paths are asset-root-relative");
        });

    entt::entity sp = entt::null;
    reg.view<game::PlayerSpawn>().each([&](entt::entity e) {
        auto* t = reg.try_get<eng::ecs::Transform>(e);
        if (t) sp = e;
    });
    require(sp != entt::null, "spawn entity has a transform");
    require(reg.get<eng::ecs::Transform>(sp).position == glm::vec3(-8, 0, 0),
            "the layout anchor is placed at world origin");

    const std::size_t beforeInvalid = reg.storage<entt::entity>().size();
    SceneGenOptions invalid = opts;
    invalid.cell = 0.0f;
    require(!layoutToScene(layout, invalid, reg),
            "invalid scene options are rejected");
    require(reg.storage<entt::entity>().size() == beforeInvalid,
            "invalid conversion leaves the destination unchanged");

    const gen::Layout generated = gen::generate(7);
    entt::registry generatedScene;
    require(layoutToScene(generated, opts, generatedScene),
            "generated layout converts to scene");
    int doorFrames = 0;
    generatedScene.view<eng::ecs::MeshSource>().each(
        [&](entt::entity, const eng::ecs::MeshSource& source) {
            if (source.path.find("Door_Frame_01.obj") != std::string::npos)
                ++doorFrames;
        });
    require(doorFrames == generated.archCount(),
            "every generated arch emits one visible door frame");

    gen::Cell archCell;
    for (int row = 0; row < generated.rowCount() && !archCell.valid(); ++row)
        for (int col = 0; col < generated.columnCount(); ++col)
            if (generated.cellAt(col, row) == 'A') {
                archCell = {col, row};
                break;
            }
    require(archCell.valid(), "generated fixture contains an arch");
    const gen::Cell anchor = generated.anchor();
    const glm::vec3 archCentre{
        float(archCell.col - anchor.col) * opts.cell, 0.0f,
        float(archCell.row - anchor.row) * opts.cell};
    const bool northSouth =
        generated.arch(generated.archAt(archCell.col, archCell.row)).northSouth;
    const float expectedOffset = opts.cell * 0.5f +
        (2.5f * opts.cell / 20.0f - game::assembly::kOpeningFlankOverlap);
    int overlappingFlanks = 0;
    generatedScene.view<eng::ecs::MeshSource, eng::ecs::Transform>().each(
        [&](entt::entity, const eng::ecs::MeshSource& source,
            const eng::ecs::Transform& transform) {
            if (source.path.find("Wall_01.obj") == std::string::npos)
                return;
            const glm::vec3 delta = transform.position - archCentre;
            const float along = northSouth ? std::fabs(delta.x)
                                           : std::fabs(delta.z);
            const float across = northSouth ? std::fabs(delta.z)
                                            : std::fabs(delta.x);
            if (std::fabs(along - expectedOffset) < 0.0001f &&
                across < 0.0001f)
                ++overlappingFlanks;
        });
    require(overlappingFlanks == 2,
            "arch flanking walls overlap the frame instead of sharing a plane");

    std::cout << "LayoutToSceneTests OK\n";
    return 0;
}
