#pragma once
#include <eng/Handles.h>

#include <entt/entt.hpp>

#include <utility>
#include <vector>

namespace eng::ecs {

class Scene;
class SceneBackend;

// Drives the backend (renderer) from the registry each frame: allocates a
// backing node when an entity first needs one, pushes changed transforms, and
// frees nodes when entities are destroyed. Renderer is a view; the registry is
// the source of truth.
class SceneSync {
public:
    SceneSync(Scene& scene, SceneBackend& backend);

    // Run once per frame, after gameplay mutated the registry: reconciles the
    // backend with the registry (also calls Scene::updateWorldTransforms).
    void sync();

private:
    Scene& mScene;
    SceneBackend& mBackend;
    std::vector<std::pair<entt::entity, NodeHandle>> mTracked;
    std::vector<entt::entity> mPushThisFrame;
};

} // namespace eng::ecs
