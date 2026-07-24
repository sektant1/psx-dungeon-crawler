#pragma once

#include <eng/ecs/Scene.h>
#include <eng/ecs/SceneSync.h>
#include <eng/LightDesc.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <string>

namespace eng::ecs { class SceneBackend; }

namespace editor {

class EditorScene {
public:
    explicit EditorScene(eng::ecs::SceneBackend& backend);

    eng::ecs::Scene& scene() { return mScene; }
    entt::registry& registry() { return mScene.registry(); }

    entt::entity spawnMesh(const std::string& objPath, const std::string& material,
                           glm::vec3 pos);
    entt::entity spawnLight(const eng::LightDesc& desc, glm::vec3 pos);
    entt::entity spawnMarker(glm::vec3 pos, const char* name);

    void sync() { mSync.sync(); }

    bool entityBounds(entt::entity e, glm::vec3& mn, glm::vec3& mx,
                      float halfExtent = 0.5f) const;

private:
    eng::ecs::Scene mScene;
    eng::ecs::SceneSync mSync;
};

} // namespace editor
