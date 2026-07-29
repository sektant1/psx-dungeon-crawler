#include "LayoutToScene.h"

#include "GameComponents.h"
#include <eng/ecs/MeshSource.h>

#include "../DungeonGen.h"

#include <eng/ecs/Components.h>

#include <cstdlib>
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
    opts.kitDir = "/assets/meshes/kit/";
    opts.propDir = "/assets/meshes/props/";
    layoutToScene(layout, opts, reg);

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

    entt::entity sp = entt::null;
    reg.view<game::PlayerSpawn>().each([&](entt::entity e) {
        auto* t = reg.try_get<eng::ecs::Transform>(e);
        if (t) sp = e;
    });
    require(sp != entt::null, "spawn entity has a transform");

    std::cout << "LayoutToSceneTests OK\n";
    return 0;
}
