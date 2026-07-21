#include "eng/Physics.h"
#include "PhysicsImpl.h"
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <algorithm>
#include <thread>

using namespace JPH;

namespace eng {

struct Physics::Impl {
    std::unique_ptr<TempAllocatorImpl> temp;
    std::unique_ptr<JobSystemThreadPool> jobs;
    phys::BPLayerInterface bp;
    phys::ObjectVsBroadPhaseFilter ovb;
    phys::ObjectPairFilter opp;
    PhysicsSystem system;
    float alpha = 0.0f;
    bool inited = false;
};

Physics::Physics() : mImpl(std::make_unique<Impl>()) {}
Physics::~Physics() { shutdown(); }

void Physics::init() {
    RegisterDefaultAllocator();
    if (!Factory::sInstance) Factory::sInstance = new Factory();
    RegisterTypes();
    mImpl->temp = std::make_unique<TempAllocatorImpl>(16 * 1024 * 1024);
    unsigned threads = std::max(1u, std::thread::hardware_concurrency() - 1u);
    mImpl->jobs = std::make_unique<JobSystemThreadPool>(cMaxPhysicsJobs, cMaxPhysicsBarriers, int(threads));
    mImpl->system.Init(4096, 0, 4096, 4096, mImpl->bp, mImpl->ovb, mImpl->opp);
    mImpl->system.SetGravity(Vec3(0, -18.0f, 0));
    mImpl->inited = true;
}

void Physics::update(float dt, int steps) {
    if (!mImpl->inited) return;
    mImpl->system.Update(dt, steps, mImpl->temp.get(), mImpl->jobs.get());
}

float Physics::interpolationAlpha() const { return mImpl->alpha; }
void Physics::setInterpolationAlpha(float a) { mImpl->alpha = a; }

void Physics::shutdown() {
    if (!mImpl || !mImpl->inited) return;
    mImpl->jobs.reset();
    mImpl->temp.reset();
    UnregisterTypes();
    delete Factory::sInstance; Factory::sInstance = nullptr;
    mImpl->inited = false;
}

// ---- stubs for later tasks (must exist so the header links) ----
BodyHandle Physics::createBody(const BodyDesc&) { return {}; }
BodyHandle Physics::createMeshBody(const std::vector<glm::vec3>&, const std::vector<uint32_t>&, glm::vec3, glm::quat, BodyLayer) { return {}; }
void Physics::removeBody(BodyHandle) {}
void Physics::setBodyTransform(BodyHandle, glm::vec3, glm::quat) {}
void Physics::getRenderTransform(BodyHandle, glm::vec3&, glm::quat&) const {}
void Physics::applyImpulse(BodyHandle, glm::vec3, glm::vec3) {}
void Physics::setBodyKinematic(BodyHandle, bool) {}
int  Physics::activeBodyCount() const { return 0; }
CharacterHandle Physics::createCharacter(const CharacterDesc&) { return {}; }
void Physics::removeCharacter(CharacterHandle) {}
void Physics::characterSetVelocity(CharacterHandle, glm::vec3) {}
void Physics::characterUpdate(CharacterHandle, float) {}
CharacterState Physics::characterState(CharacterHandle) const { return {}; }
void Physics::characterSetShape(CharacterHandle, float, float) {}
bool Physics::rayCast(glm::vec3, glm::vec3, float, RayHit&, BodyLayer) const { return false; }
int  Physics::shapeCast(const BodyDesc&, glm::vec3, glm::vec3, std::vector<ShapeHit>&, BodyLayer) const { return 0; }
int  Physics::overlap(const BodyDesc&, glm::vec3, std::vector<ShapeHit>&, BodyLayer) const { return 0; }
void Physics::setContactCallback(HitCallback) {}

} // namespace eng
