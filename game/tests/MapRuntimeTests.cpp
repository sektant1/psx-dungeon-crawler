#include "MapRuntime.h"
#include "GameCollision.h"

#include "GameComponents.h"
#include <eng/ecs/components/MeshSource.h>
#include "MapSerializer.h"

#include <eng/Physics.h>
#include <eng/ecs/Components.h>
#include <eng/ecs/SceneBackend.h>
#include <eng/ecs/World.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

using namespace game;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "MapRuntimeTests: " << m << '\n'; std::exit(1); }
}

struct MockBackend : eng::ecs::SceneBackend {
    int nodes = 0;
    uint32_t next = 1;
    eng::NodeHandle createNode(eng::NodeHandle, glm::vec3, const std::string&) override
    { ++nodes; return eng::NodeHandle{next++}; }
    void setPosition(eng::NodeHandle, glm::vec3) override {}
    void setOrientation(eng::NodeHandle, glm::quat) override {}
    void setScale(eng::NodeHandle, glm::vec3) override {}
    void destroyNode(eng::NodeHandle) override { --nodes; }
    void attachMesh(eng::NodeHandle, eng::MeshHandle, const std::string&, bool) override {}
    eng::LightHandle attachLight(eng::NodeHandle, const eng::LightDesc&) override
    { return eng::LightHandle{next++}; }
    void setLightColour(eng::LightHandle, glm::vec3) override {}
};

int main()
{
    const std::string path = "map_runtime_test.map";
    {
        entt::registry reg;
        entt::entity floor = reg.create();
        reg.emplace<eng::ecs::Transform>(floor, eng::ecs::Transform{});
        reg.emplace<eng::ecs::MeshSource>(floor, eng::ecs::MeshSource{"meshes/tiles/floor.obj"});
        reg.emplace<eng::ecs::MeshRenderer>(floor, eng::ecs::MeshRenderer{});
        reg.emplace<Collider>(floor, Collider{eng::ShapeKind::Box, glm::vec3(4, 0.5f, 4),
                                              game::layer::Static});
        entt::entity spawn = reg.create();
        eng::ecs::Transform st; st.position = glm::vec3(3, 1, -2);
        reg.emplace<eng::ecs::Transform>(spawn, st);
        reg.emplace<PlayerSpawn>(spawn);
        require(mapio::writeMap(path, reg, mapio::coreRegistry()), "author map");
    }

    eng::Physics physics;
    physics.init(game::layer::physicsSetup());
    MockBackend backend;
    {
        eng::ecs::World world;
        world.attachRenderer(backend);
        world.attachPhysics(physics);

        // An entity the map knows nothing about, spawned before the load. It is
        // the whole reason load() merges instead of replacing the registry: it
        // stands for the player, the enemies and every other live actor that
        // now shares the level's world with the authored geometry.
        const entt::entity resident = world.create("resident");

        constexpr uint32_t kMapGroup = 7;
        MapRuntime rt(world, kMapGroup);

        require(rt.load(path), "load .map");
        require(world.registry().valid(resident),
                "loading a map does not evict entities already in the world");
        int meshLoads = 0;
        rt.resolveMeshes([&](const std::string&) { ++meshLoads; return eng::MeshHandle{42}; });
        require(meshLoads == 1, "resolveMeshes called once per MeshRenderer");

        const int beforeBodies = physics.bodyCount();
        rt.buildAll();
        // One node: the floor. The spawn marker and the resident have a
        // position and nothing to draw, and no longer cost a renderer node --
        // the rule that makes a single shared world affordable.
        require(backend.nodes == 1, "only entities with a visual get a node");
        require(physics.bodyCount() == beforeBodies + 1, "buildAll created the collider body");

        glm::vec3 sp = rt.playerSpawn();
        require(sp == glm::vec3(3, 1, -2), "playerSpawn returns the marker position");

        physics.update(1.0f / 60.0f);
        world.sync();

        std::ifstream source(path, std::ios::binary);
        std::vector<char> truncated{std::istreambuf_iterator<char>(source),
                                    std::istreambuf_iterator<char>()};
        truncated.pop_back();
        const std::string corrupt = "map_runtime_corrupt.map";
        std::ofstream broken(corrupt, std::ios::binary | std::ios::trunc);
        broken.write(truncated.data(), std::streamsize(truncated.size()));
        broken.close();

        const int liveNodes = backend.nodes;
        const int liveBodies = physics.bodyCount();
        require(!rt.load(corrupt), "corrupt replacement is rejected");
        require(backend.nodes == liveNodes && physics.bodyCount() == liveBodies,
                "failed replacement preserves the live renderer and physics scene");
        require(rt.playerSpawn() == glm::vec3(3, 1, -2),
                "failed replacement preserves registry state");
        std::remove(corrupt.c_str());

        // A second load merges rather than replacing, so the level accumulates
        // -- what a map streamed in beside an existing one has to do.
        require(rt.load(path), "a map can be merged into a populated world");
        int spawns = 0;
        for (auto e : world.registry().view<PlayerSpawn>()) { (void)e; ++spawns; }
        require(spawns == 2, "the second load added its own entities");

        // A transition destroys exactly the map's entities. The resident was
        // created outside the group and survives -- the whole reason groups
        // exist, since it stands for the player.
        const std::size_t removed = world.destroyGroup(kMapGroup);
        require(removed == 4, "destroyGroup removes both merged batches");
        require(world.registry().valid(resident),
                "an ungrouped entity survives a level teardown");
        require(world.destroyGroup(0) == 0,
                "group 0 is refused: it would take the survivors too");
        world.sync();
        require(backend.nodes == 0, "the group's nodes went with it");
        require(physics.bodyCount() == 0, "and so did its bodies");
    }
    require(backend.nodes == 0, "runtime teardown destroys all renderer nodes");
    require(physics.bodyCount() == 0, "runtime teardown destroys all physics bodies");
    physics.shutdown();
    std::remove(path.c_str());
    std::cout << "MapRuntimeTests OK\n";
    return 0;
}
