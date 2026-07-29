#include <eng/ecs/PhysicsSync.h>

#include <eng/Physics.h>
#include <eng/ecs/Components.h>

#include <algorithm>

namespace eng::ecs {

namespace {
glm::vec3 posOf(const entt::registry& r, entt::entity e)
{
    if (const auto* w = r.try_get<WorldTransform>(e))
        return glm::vec3(w->matrix[3]);
    if (const auto* t = r.try_get<Transform>(e)) return t->position;
    return glm::vec3(0.0f);
}
glm::quat rotOf(const entt::registry& r, entt::entity e)
{
    if (const auto* t = r.try_get<Transform>(e)) return t->rotation;
    return glm::quat(1, 0, 0, 0);
}

} // namespace

PhysicsSync::~PhysicsSync()
{
    clear();
}

void PhysicsSync::clear()
{
    for (const Tracked& tracked : mTracked) {
        mPhysics.removeBody(tracked.body);
        if (mReg.valid(tracked.entity)) {
            if (const auto* ref = mReg.try_get<BodyRef>(tracked.entity);
                ref && ref->handle == tracked.body)
                mReg.remove<BodyRef>(tracked.entity);
        }
    }
    mTracked.clear();
}

void PhysicsSync::sync()
{
    // Tear down stale bindings first so reloads do not briefly need capacity
    // for both the old and new worlds.
    mTracked.erase(std::remove_if(mTracked.begin(), mTracked.end(),
        [&](Tracked& tracked) {
            bool stale = !mReg.valid(tracked.entity);
            if (!stale) {
                const auto* collider = mReg.try_get<Collider>(tracked.entity);
                const auto* ref = mReg.try_get<BodyRef>(tracked.entity);
                stale = !collider || !ref || ref->handle != tracked.body ||
                        collider->shape != tracked.shape ||
                        collider->size != tracked.size ||
                        collider->layer != tracked.layer ||
                        collider->sensor != tracked.sensor;
            }
            if (!stale)
                return false;

            mPhysics.removeBody(tracked.body);
            if (mReg.valid(tracked.entity)) {
                if (const auto* ref = mReg.try_get<BodyRef>(tracked.entity);
                    ref && ref->handle == tracked.body)
                    mReg.remove<BodyRef>(tracked.entity);
            }
            return true;
        }), mTracked.end());

    for (auto e : mReg.view<Collider>(entt::exclude<BodyRef>)) {
        const Collider& c = mReg.get<Collider>(e);
        BodyDesc d;
        d.kind = c.shape;
        d.halfExtents = c.size;
        d.radius = c.size.x;
        d.halfHeight = c.size.y;
        d.position = posOf(mReg, e);
        d.orientation = rotOf(mReg, e);
        d.layer = c.layer;
        d.dynamic = false;
        d.sensor = c.sensor;
        const BodyHandle h = mPhysics.createBody(d);
        if (!h.valid())
            continue;
        mReg.emplace<BodyRef>(e, BodyRef{h});
        mTracked.push_back(
            Tracked{e, h, c.shape, c.size, c.layer, c.sensor, d.position,
                    d.orientation});
    }

    // Authoring transforms can change while an entity remains alive. Move the
    // backing static body only when its source pose actually changed.
    for (Tracked& tracked : mTracked) {
        const glm::vec3 position = posOf(mReg, tracked.entity);
        const glm::quat orientation = rotOf(mReg, tracked.entity);
        if (position == tracked.position && orientation == tracked.orientation)
            continue;
        mPhysics.setBodyTransform(tracked.body, position, orientation);
        tracked.position = position;
        tracked.orientation = orientation;
    }
}

} // namespace eng::ecs
