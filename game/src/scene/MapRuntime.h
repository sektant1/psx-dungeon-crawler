#pragma once

#include "PhysicsSync.h"

#include <eng/ecs/Scene.h>
#include <eng/ecs/SceneSync.h>
#include <eng/Handles.h>

#include <entt/entt.hpp>
#include <functional>
#include <glm/glm.hpp>
#include <string>

namespace eng { class Physics; }
namespace eng::ecs { class SceneBackend; }

namespace game {

class MapRuntime {
public:
    MapRuntime(eng::ecs::SceneBackend& backend, eng::Physics& physics);

    bool load(const std::string& path);
    using LoadMeshFn = std::function<eng::MeshHandle(const std::string& path)>;
    void resolveMeshes(const LoadMeshFn& loadFn);
    void buildAll();
    void step(float dt);
    glm::vec3 playerSpawn() const;

    entt::registry& registry() { return mScene.registry(); }

private:
    eng::ecs::Scene mScene;
    eng::ecs::SceneSync mSceneSync;
    PhysicsSync mPhysicsSync;
    eng::Physics& mPhysics;
};

} // namespace game
