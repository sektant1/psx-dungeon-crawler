#include <eng/ecs/Scene.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

namespace eng::ecs {

entt::entity Scene::create(std::string name)
{
    const entt::entity e = mReg.create();
    mReg.emplace<Transform>(e);
    mReg.emplace<Dirty>(e);
    if (!name.empty())
        mReg.emplace<Name>(e, std::move(name));
    return e;
}

void Scene::destroy(entt::entity e)
{
    if (!mReg.valid(e))
        return;
    if (auto* parent = mReg.try_get<Parent>(e); parent && parent->value != entt::null) {
        if (auto* pc = mReg.try_get<Children>(parent->value)) {
            auto& v = pc->value;
            v.erase(std::remove(v.begin(), v.end(), e), v.end());
        }
    }
    if (auto* ch = mReg.try_get<Children>(e)) {
        for (entt::entity c : ch->value)
            if (auto* cp = mReg.try_get<Parent>(c))
                cp->value = entt::null;
    }
    mReg.destroy(e);
}

void Scene::setLocalTransform(entt::entity, const Transform&) {}
void Scene::setParent(entt::entity, entt::entity) {}
void Scene::updateWorldTransforms() {}
void Scene::markSubtreeDirty(entt::entity) {}
void Scene::resolveWorld(entt::entity) {}

} // namespace eng::ecs
