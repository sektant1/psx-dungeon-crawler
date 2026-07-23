#include <eng/ecs/Scene.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <vector>

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

void Scene::setLocalTransform(entt::entity e, const Transform& t)
{
    if (!mReg.valid(e))
        return;
    mReg.get_or_emplace<Transform>(e) = t;
    markSubtreeDirty(e);
}

void Scene::markSubtreeDirty(entt::entity e)
{
    if (!mReg.valid(e))
        return;
    if (!mReg.all_of<Dirty>(e))
        mReg.emplace<Dirty>(e);
    if (auto* ch = mReg.try_get<Children>(e))
        for (entt::entity c : ch->value)
            markSubtreeDirty(c);
}

void Scene::resolveWorld(entt::entity e)
{
    if (!mReg.all_of<Dirty>(e))
        return; // already resolved this pass (or never dirty)

    const Transform& t = mReg.get<Transform>(e);
    glm::mat4 local = glm::translate(glm::mat4(1.0f), t.position);
    local *= glm::mat4_cast(t.rotation);
    local = glm::scale(local, t.scale);

    glm::mat4 world = local;
    if (auto* p = mReg.try_get<Parent>(e); p && p->value != entt::null &&
                                           mReg.valid(p->value)) {
        resolveWorld(p->value); // ensure parent world is up to date first
        world = mReg.get<WorldTransform>(p->value).matrix * local;
    }
    mReg.get_or_emplace<WorldTransform>(e).matrix = world;
    mReg.remove<Dirty>(e);
}

void Scene::updateWorldTransforms()
{
    // Snapshot the dirty set first: resolveWorld removes Dirty as it goes.
    std::vector<entt::entity> dirty;
    for (auto e : mReg.view<Dirty>())
        dirty.push_back(e);
    for (entt::entity e : dirty)
        if (mReg.valid(e))
            resolveWorld(e);
}

void Scene::setParent(entt::entity e, entt::entity parent)
{
    if (!mReg.valid(e))
        return;
    if (e == parent)
        return; // self-parenting would infinitely recurse in resolveWorld
    // Reject cycles: walking up from `parent` must not reach `e`.
    for (entt::entity a = parent; a != entt::null && mReg.valid(a);) {
        if (a == e)
            return;
        auto* ap = mReg.try_get<Parent>(a);
        a = ap ? ap->value : entt::null;
    }
    auto& link = mReg.get_or_emplace<Parent>(e);
    // Remove from the previous parent's Children.
    if (link.value != entt::null && mReg.valid(link.value)) {
        if (auto* oc = mReg.try_get<Children>(link.value)) {
            auto& v = oc->value;
            v.erase(std::remove(v.begin(), v.end(), e), v.end());
        }
    }
    link.value = parent;
    if (parent != entt::null && mReg.valid(parent))
        mReg.get_or_emplace<Children>(parent).value.push_back(e);
    markSubtreeDirty(e);
}

} // namespace eng::ecs
