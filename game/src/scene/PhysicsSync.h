#pragma once

#include <eng/Handles.h>

#include <entt/entt.hpp>

#include <utility>
#include <vector>

namespace eng { class Physics; }

namespace game {

class PhysicsSync {
public:
    PhysicsSync(entt::registry& reg, eng::Physics& physics)
        : mReg(reg), mPhysics(physics) {}

    void sync();

private:
    entt::registry& mReg;
    eng::Physics& mPhysics;
    std::vector<std::pair<entt::entity, eng::BodyHandle>> mTracked;
};

} // namespace game
