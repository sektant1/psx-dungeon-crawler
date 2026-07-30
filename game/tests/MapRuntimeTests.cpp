#include "MapRuntime.h"
#include "GameCollision.h"

#include "GameComponents.h"
#include <eng/ecs/MeshSource.h>
#include "MapSerializer.h"

#include <eng/Physics.h>
#include <eng/ecs/Components.h>
#include <eng/ecs/SceneBackend.h>

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
        MapRuntime rt(backend, physics);

        require(rt.load(path), "load .map");
        int meshLoads = 0;
        rt.resolveMeshes([&](const std::string&) { ++meshLoads; return eng::MeshHandle{42}; });
        require(meshLoads == 1, "resolveMeshes called once per MeshRenderer");

        const int beforeBodies = physics.bodyCount();
        rt.buildAll();
        require(backend.nodes >= 2, "buildAll created render nodes");
        require(physics.bodyCount() == beforeBodies + 1, "buildAll created the collider body");

        glm::vec3 sp = rt.playerSpawn();
        require(sp == glm::vec3(3, 1, -2), "playerSpawn returns the marker position");

        rt.step(1.0f / 60.0f);

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
    }
    require(backend.nodes == 0, "runtime teardown destroys all renderer nodes");
    require(physics.bodyCount() == 0, "runtime teardown destroys all physics bodies");
    physics.shutdown();
    std::remove(path.c_str());
    std::cout << "MapRuntimeTests OK\n";
    return 0;
}
