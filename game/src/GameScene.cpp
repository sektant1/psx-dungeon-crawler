#include "GameScene.h"

#include <eng/ecs/Components.h>

namespace game {

GameScene::GameScene(eng::Renderer& r)
    : mBackend(r), mSync(mScene, mBackend) {}

entt::entity GameScene::spawnStatic(eng::MeshHandle mesh,
                                    const std::string& material,
                                    glm::vec3 position, glm::quat orientation,
                                    glm::vec3 scale, bool castShadows,
                                    const std::string& name)
{
    const entt::entity e = mScene.create(name);
    eng::ecs::Transform t;
    t.position = position;
    t.rotation = orientation;
    t.scale = scale;
    mScene.setLocalTransform(e, t);
    mScene.registry().emplace<eng::ecs::MeshRenderer>(e, mesh, material,
                                                      castShadows);
    return e;
}

entt::entity GameScene::spawnLight(const eng::LightDesc& desc,
                                   glm::vec3 position, const std::string& name)
{
    const entt::entity e = mScene.create(name);
    eng::ecs::Transform t;
    t.position = position;
    mScene.setLocalTransform(e, t);
    auto& reg = mScene.registry();
    reg.emplace<eng::ecs::LightRef>(e).desc = desc;
    reg.emplace<eng::ecs::LightColour>(e, desc.colour);
    return e;
}

} // namespace game
