#include "EditorScene.h"

#include "GameComponents.h"
#include "MeshSource.h"

#include <eng/ecs/Components.h>
#include <eng/ecs/SceneBackend.h>

#include <cstdlib>
#include <iostream>

using namespace editor;

static void require(bool c, const char* m)
{
    if (!c) { std::cerr << "EditorSceneTests: " << m << '\n'; std::exit(1); }
}

struct MockBackend : eng::ecs::SceneBackend {
    int nodes = 0, meshes = 0, lights = 0;
    uint32_t next = 1;
    eng::NodeHandle createNode(eng::NodeHandle, glm::vec3, const std::string&) override
    { ++nodes; return eng::NodeHandle{next++}; }
    void setPosition(eng::NodeHandle, glm::vec3) override {}
    void setOrientation(eng::NodeHandle, glm::quat) override {}
    void setScale(eng::NodeHandle, glm::vec3) override {}
    void destroyNode(eng::NodeHandle) override {}
    void attachMesh(eng::NodeHandle, eng::MeshHandle, const std::string&, bool) override
    { ++meshes; }
    eng::LightHandle attachLight(eng::NodeHandle, const eng::LightDesc&) override
    { ++lights; return eng::LightHandle{next++}; }
};

int main()
{
    MockBackend backend;
    EditorScene scene(backend);

    entt::entity e = scene.spawnMesh("meshes/tiles/floor.obj", "Game/DungeonTile",
                                     glm::vec3(1, 0, 2));
    require(scene.registry().all_of<eng::ecs::Transform>(e), "mesh entity has a transform");
    require(scene.registry().all_of<mapio::MeshSource>(e), "mesh entity records its source path");
    require(scene.registry().get<mapio::MeshSource>(e).path == "meshes/tiles/floor.obj",
            "source path stored");

    entt::entity l = scene.spawnLight(eng::LightDesc{}, glm::vec3(0, 3, 0));
    require(scene.registry().all_of<eng::ecs::LightRef>(l), "light entity has a light ref");

    scene.sync();
    require(backend.nodes >= 2, "sync created a node per entity");
    require(backend.meshes == 1 && backend.lights == 1, "sync attached mesh + light once");

    glm::vec3 mn, mx;
    require(scene.entityBounds(e, mn, mx), "mesh entity has bounds");
    require(mn.x < 1.0f && mx.x > 1.0f, "bounds straddle the entity position x=1");

    std::cout << "EditorSceneTests OK\n";
    return 0;
}
