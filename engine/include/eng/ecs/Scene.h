#pragma once
#include <eng/ecs/Components.h>

#include <entt/entt.hpp>

#include <string>

namespace eng::ecs {

// Registry-backed scene: the source of truth for scene-object data. Owns
// entity lifetime and the parent/child + transform bookkeeping. Holds no
// renderer/physics types; SceneSync bridges those.
class Scene {
public:
    entt::entity create(std::string name = {});
    void destroy(entt::entity e);
    void setLocalTransform(entt::entity e, const Transform& t);
    void setParent(entt::entity e, entt::entity parent);
    void updateWorldTransforms();

    entt::registry& registry() { return mReg; }
    const entt::registry& registry() const { return mReg; }

private:
    void markSubtreeDirty(entt::entity e);
    void resolveWorld(entt::entity e);

    entt::registry mReg;
};

} // namespace eng::ecs
