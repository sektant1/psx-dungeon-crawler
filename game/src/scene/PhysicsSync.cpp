#include "PhysicsSync.h"
#include "GameCollision.h"

#include "GameComponents.h"
#include "RuntimeComponents.h"

#include <eng/Physics.h>
#include <eng/ecs/Components.h>

#include <algorithm>

namespace game {

namespace {
glm::vec3 posOf(const entt::registry& r, entt::entity e)
{
    if (const auto* t = r.try_get<eng::ecs::Transform>(e)) return t->position;
    return glm::vec3(0.0f);
}
glm::quat rotOf(const entt::registry& r, entt::entity e)
{
    if (const auto* t = r.try_get<eng::ecs::Transform>(e)) return t->rotation;
    return glm::quat(1, 0, 0, 0);
}
} // namespace

void PhysicsSync::sync()
{
    for (auto e : mReg.view<Collider>(entt::exclude<BodyRef>)) {
        const Collider& c = mReg.get<Collider>(e);
        eng::BodyDesc d;
        d.kind = c.shape;
        d.halfExtents = c.size;
        d.radius = c.size.x;
        d.halfHeight = c.size.y;
        d.position = posOf(mReg, e);
        d.orientation = rotOf(mReg, e);
        d.layer = c.layer;
        d.dynamic = false;
        const eng::BodyHandle h = mPhysics.createBody(d);
        mReg.emplace<BodyRef>(e, BodyRef{h});
        mTracked.emplace_back(e, h);
    }

    for (auto e : mReg.view<Trigger>(entt::exclude<BodyRef>)) {
        const Trigger& tr = mReg.get<Trigger>(e);
        eng::BodyDesc d;
        d.kind = tr.shape;
        d.halfExtents = tr.size;
        d.radius = tr.size.x;
        d.halfHeight = tr.size.y;
        d.position = posOf(mReg, e);
        d.orientation = rotOf(mReg, e);
        d.layer = game::layer::Trigger;
        d.dynamic = false;
        d.sensor = true;
        const eng::BodyHandle h = mPhysics.createBody(d);
        mReg.emplace<BodyRef>(e, BodyRef{h});
        mTracked.emplace_back(e, h);
    }

    mTracked.erase(std::remove_if(mTracked.begin(), mTracked.end(),
        [&](auto& pair) {
            if (mReg.valid(pair.first)) return false;
            mPhysics.removeBody(pair.second);
            return true;
        }), mTracked.end());
}

} // namespace game
