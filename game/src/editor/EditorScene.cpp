#include "EditorScene.h"

#include "GameComponents.h"
#include "MeshSource.h"

#include <eng/ecs/Components.h>

namespace editor {

EditorScene::EditorScene(eng::ecs::SceneBackend& backend)
    : mSync(mScene, backend)
{}

entt::entity EditorScene::spawnMesh(const std::string& objPath,
                                    const std::string& material, glm::vec3 pos)
{
    entt::registry& r = registry();
    entt::entity e = mScene.create(objPath);
    eng::ecs::Transform t;
    t.position = pos;
    mScene.setLocalTransform(e, t);
    r.emplace<mapio::MeshSource>(e, mapio::MeshSource{objPath});
    eng::ecs::MeshRenderer mr;
    mr.material = material;
    r.emplace<eng::ecs::MeshRenderer>(e, mr);
    return e;
}

entt::entity EditorScene::spawnLight(const eng::LightDesc& desc, glm::vec3 pos)
{
    entt::registry& r = registry();
    entt::entity e = mScene.create("Light");
    eng::ecs::Transform t;
    t.position = pos;
    mScene.setLocalTransform(e, t);
    r.emplace<eng::ecs::LightRef>(e, eng::ecs::LightRef{desc, {}});
    return e;
}

entt::entity EditorScene::spawnMarker(glm::vec3 pos, const char* name)
{
    entt::entity e = mScene.create(name);
    eng::ecs::Transform t;
    t.position = pos;
    mScene.setLocalTransform(e, t);
    return e;
}

bool EditorScene::entityBounds(entt::entity e, glm::vec3& mn, glm::vec3& mx,
                               float halfExtent) const
{
    const entt::registry& r = mScene.registry();
    if (!r.all_of<eng::ecs::Transform>(e)) return false;
    const glm::vec3 p = r.get<eng::ecs::Transform>(e).position;
    mn = p - glm::vec3(halfExtent);
    mx = p + glm::vec3(halfExtent);
    return true;
}

} // namespace editor
