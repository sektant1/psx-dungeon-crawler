#include <eng/ecs/PhysicsSync.h>

#include <eng/Physics.h>
#include <eng/ecs/Components.h>

#include <algorithm>

namespace eng::ecs {

// Defined here, next to the reconciler it builds, so World.cpp stays free of
// Jolt: a headless World links neither this TU nor the physics library.
void World::attachPhysics(Physics& physics)
{
    mPhysics = std::make_unique<PhysicsSync>(mReg, physics);
}

namespace {
// The entity's pose in world space. Composed when it has been resolved, local
// otherwise -- a body created in the same frame as its entity has no
// WorldTransform yet, and its local transform is the best that exists.
//
// Rotation comes out of the same matrix as the position, not off the local
// Transform: a collider under a rotated parent was previously placed correctly
// and oriented wrongly, which in a dungeon of parented wall pieces is a
// doorway you cannot walk through.
Transform poseOf(const entt::registry& r, entt::entity e)
{
    if (const auto* w = r.try_get<WorldTransform>(e))
        return decompose(w->matrix);
    if (const auto* t = r.try_get<Transform>(e)) return *t;
    return Transform{};
}

} // namespace

bool PhysicsSync::sameBuild(const RigidBody& a, const RigidBody& b)
{
    return a.mass == b.mass && a.gravityFactor == b.gravityFactor &&
           a.friction == b.friction && a.restitution == b.restitution &&
           a.kinematic == b.kinematic && a.continuous == b.continuous;
}

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
                // A RigidBody added or removed changes what kind of body this
                // is, not just its parameters, so it is a rebuild like a shape
                // change -- Jolt has no "become dynamic" for an existing body.
                const auto* rigid = mReg.try_get<RigidBody>(tracked.entity);
                const bool dynamic = rigid != nullptr;
                stale = !collider || !ref || ref->handle != tracked.body ||
                        collider->shape != tracked.shape ||
                        collider->size != tracked.size ||
                        collider->layer != tracked.layer ||
                        collider->sensor != tracked.sensor ||
                        dynamic != tracked.dynamic ||
                        // Mass, friction, restitution and gravity factor are
                        // baked into the body at creation, so an edited one is
                        // a rebuild too. It costs the body's velocity, which is
                        // the right trade for a value only an author changes.
                        (rigid && !sameBuild(*rigid, tracked.params));
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
        const Transform pose = poseOf(mReg, e);
        BodyDesc d;
        d.kind = c.shape;
        d.halfExtents = c.size;
        d.radius = c.size.x;
        d.halfHeight = c.size.y;
        d.position = pose.position;
        d.orientation = pose.rotation;
        d.layer = c.layer;
        d.sensor = c.sensor;
        // A Collider alone is static world geometry; a RigidBody beside it is
        // what hands the transform to the simulation. Authoring a prop that
        // falls over is then adding one component to a thing that already
        // collides, rather than a different kind of object.
        const RigidBody* body = mReg.try_get<RigidBody>(e);
        d.dynamic = body != nullptr;
        if (body) {
            d.mass = body->mass;
            d.gravityFactor = body->gravityFactor;
            d.friction = body->friction;
            d.restitution = body->restitution;
            d.continuousCast = body->continuous;
        }
        const BodyHandle h = mPhysics.createBody(d);
        if (!h.valid())
            continue;
        if (body && body->kinematic)
            mPhysics.setBodyKinematic(h, true);
        mReg.emplace<BodyRef>(e, BodyRef{h});
        mTracked.push_back(Tracked{e, h, c.shape, c.size, c.layer, c.sensor,
                                   d.dynamic, body ? *body : RigidBody{},
                                   d.position, d.orientation});
    }

    // Authoring transforms can change while an entity remains alive. Move the
    // backing body only when its source pose actually changed.
    //
    // Dynamic bodies are skipped: the simulation owns their pose, and writing
    // the component's transform back onto them every frame would pin them in
    // place -- a prop that "falls" and never moves. Their transforms flow the
    // other way, through Physics::getRenderTransform.
    for (Tracked& tracked : mTracked) {
        if (tracked.dynamic && !mReg.all_of<KinematicControl>(tracked.entity))
            continue;
        const Transform pose = poseOf(mReg, tracked.entity);
        if (pose.position == tracked.position &&
            pose.rotation == tracked.orientation)
            continue;
        mPhysics.setBodyTransform(tracked.body, pose.position, pose.rotation);
        tracked.position = pose.position;
        tracked.orientation = pose.rotation;
    }
}

void PhysicsSync::readback()
{
    for (Tracked& tracked : mTracked) {
        // Only bodies the simulation owns: a static body has nothing to say,
        // and a kinematic one is being steered by gameplay, so reading its pose
        // back would overwrite the command with the result of the last one.
        if (!tracked.dynamic || !mReg.valid(tracked.entity) ||
            mReg.all_of<KinematicControl>(tracked.entity))
            continue;

        glm::vec3 position{0.0f};
        glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
        mPhysics.getRenderTransform(tracked.body, position, orientation);
        if (position == tracked.position && orientation == tracked.orientation)
            continue; // asleep: not worth dirtying a subtree for

        // Written as the LOCAL transform. A simulated body's pose is in world
        // space, so this is only the same thing while the entity is a root --
        // which every dynamic prop is, and has to be: a body cannot be parented
        // to something the simulation does not know about. Reparenting one is
        // therefore not a transform bug to find later but a modelling mistake,
        // and it is stated here because this is where it would first show.
        Transform& local = mReg.get_or_emplace<Transform>(tracked.entity);
        local.position = position;
        local.rotation = orientation;
        if (!mReg.all_of<Dirty>(tracked.entity))
            mReg.emplace<Dirty>(tracked.entity);

        // Recorded as pushed: the pose now on the component came *from* the
        // body, and sync() must not spend a setBodyTransform putting it back.
        tracked.position = position;
        tracked.orientation = orientation;
    }
}

} // namespace eng::ecs
