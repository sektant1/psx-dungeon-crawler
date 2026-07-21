#include "eng/Physics.h"
#include "PhysicsImpl.h"
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyType.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <algorithm>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace JPH;

namespace eng {

// ---- body record ----
struct BodyRec {
    JPH::BodyID id;
    bool dynamic  = false;
    bool isStatic = false;
    JPH::RVec3 prevPos = JPH::RVec3::sZero(), curPos  = JPH::RVec3::sZero();
    JPH::Quat  prevRot = JPH::Quat::sIdentity(), curRot = JPH::Quat::sIdentity();
    bool alive = false;
};

struct Physics::Impl {
    std::unique_ptr<TempAllocatorImpl>   temp;
    std::unique_ptr<JobSystemThreadPool> jobs;
    phys::BPLayerInterface        bp;
    phys::ObjectVsBroadPhaseFilter ovb;
    phys::ObjectPairFilter         opp;
    PhysicsSystem system;
    float alpha  = 0.0f;
    bool  inited = false;

    // slot 0 is reserved as the null/invalid handle sentinel
    std::vector<BodyRec>               bodies;
    std::vector<uint32_t>              freeList;
    // keyed on BodyID::GetIndexAndSequenceNumber()
    std::unordered_map<uint32_t, uint32_t> idToSlot;
};

// ---- helpers ----
static JPH::ObjectLayer mapLayer(BodyLayer l) {
    switch (l) {
        case BodyLayer::Static:     return phys::Layers::STATIC;
        case BodyLayer::Player:     return phys::Layers::PLAYER;
        case BodyLayer::Prop:       return phys::Layers::PROP;
        case BodyLayer::Projectile: return phys::Layers::PROJECTILE;
        case BodyLayer::Trigger:    return phys::Layers::TRIGGER;
    }
    return phys::Layers::PROP;
}

static JPH::ShapeRefC makeShape(const BodyDesc& d) {
    switch (d.kind) {
        case ShapeKind::Box:
            return new BoxShape(Vec3(d.halfExtents.x, d.halfExtents.y, d.halfExtents.z));
        case ShapeKind::Sphere:
            return new SphereShape(d.radius);
        case ShapeKind::Capsule:
            return new CapsuleShape(d.halfHeight, d.radius);
        case ShapeKind::Cylinder:
            return new CylinderShape(d.halfHeight, d.radius);
    }
    return new BoxShape(Vec3(d.halfExtents.x, d.halfExtents.y, d.halfExtents.z));
}

// ---- lifecycle ----
Physics::Physics() : mImpl(std::make_unique<Impl>()) {
    // slot 0 is the null sentinel — reserve it now
    mImpl->bodies.push_back(BodyRec{}); // slot 0: dead, invalid
}
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

    // snapshot cur -> prev for dynamic bodies
    for (auto& rec : mImpl->bodies) {
        if (!rec.alive || rec.isStatic) continue;
        rec.prevPos = rec.curPos;
        rec.prevRot = rec.curRot;
    }

    mImpl->system.Update(dt, steps, mImpl->temp.get(), mImpl->jobs.get());

    // read back updated transforms
    BodyInterface& bi = mImpl->system.GetBodyInterface();
    for (auto& rec : mImpl->bodies) {
        if (!rec.alive || rec.isStatic) continue;
        bi.GetPositionAndRotation(rec.id, rec.curPos, rec.curRot);
    }
}

float Physics::interpolationAlpha() const { return mImpl->alpha; }
void  Physics::setInterpolationAlpha(float a) { mImpl->alpha = a; }

void Physics::shutdown() {
    if (!mImpl || !mImpl->inited) return;
    // destroy all remaining bodies
    BodyInterface& bi = mImpl->system.GetBodyInterface();
    for (auto& rec : mImpl->bodies) {
        if (!rec.alive) continue;
        bi.RemoveBody(rec.id);
        bi.DestroyBody(rec.id);
        rec.alive = false;
    }
    mImpl->idToSlot.clear();
    mImpl->freeList.clear();
    mImpl->jobs.reset();
    mImpl->temp.reset();
    UnregisterTypes();
    delete Factory::sInstance; Factory::sInstance = nullptr;
    mImpl->inited = false;
}

// ---- body management ----
BodyHandle Physics::createBody(const BodyDesc& desc) {
    if (!mImpl->inited) return {};

    ShapeRefC shape = makeShape(desc);

    EMotionType motionType = desc.dynamic ? EMotionType::Dynamic : EMotionType::Static;
    ObjectLayer layer      = mapLayer(desc.layer);

    RVec3 pos(desc.position.x, desc.position.y, desc.position.z);
    Quat  rot(desc.orientation.x, desc.orientation.y, desc.orientation.z, desc.orientation.w);

    BodyCreationSettings bcs(shape, pos, rot, motionType, layer);
    bcs.mFriction    = desc.friction;
    bcs.mRestitution = desc.restitution;
    bcs.mIsSensor    = desc.sensor;
    if (desc.continuousCast)
        bcs.mMotionQuality = EMotionQuality::LinearCast;
    if (desc.dynamic) {
        bcs.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
        bcs.mMassPropertiesOverride.mMass = desc.mass;
    }

    EActivation activation = desc.dynamic ? EActivation::Activate : EActivation::DontActivate;
    BodyID bid = mImpl->system.GetBodyInterface().CreateAndAddBody(bcs, activation);
    if (bid.IsInvalid()) return {};

    // allocate a slot (reuse free list or push_back)
    uint32_t slot;
    if (!mImpl->freeList.empty()) {
        slot = mImpl->freeList.back();
        mImpl->freeList.pop_back();
    } else {
        slot = uint32_t(mImpl->bodies.size());
        mImpl->bodies.push_back(BodyRec{});
    }

    BodyRec& rec  = mImpl->bodies[slot];
    rec.id        = bid;
    rec.dynamic   = desc.dynamic;
    rec.isStatic  = !desc.dynamic;
    rec.curPos    = pos;
    rec.curRot    = rot;
    rec.prevPos   = pos;
    rec.prevRot   = rot;
    rec.alive     = true;

    mImpl->idToSlot[bid.GetIndexAndSequenceNumber()] = slot;
    return BodyHandle{ slot };
}

void Physics::getRenderTransform(BodyHandle h, glm::vec3& pos, glm::quat& rot) const {
    if (!h.valid() || h.id >= uint32_t(mImpl->bodies.size())) return;
    const BodyRec& rec = mImpl->bodies[h.id];
    if (!rec.alive) return;

    JPH::RVec3 p;
    JPH::Quat  r;
    if (rec.isStatic) {
        p = rec.curPos;
        r = rec.curRot;
    } else {
        float a = mImpl->alpha;
        // lerp position
        p = rec.prevPos + (rec.curPos - rec.prevPos) * a;
        // slerp rotation
        r = rec.prevRot.SLERP(rec.curRot, a);
    }

    pos = glm::vec3(float(p.GetX()), float(p.GetY()), float(p.GetZ()));
    rot = glm::quat(r.GetW(), r.GetX(), r.GetY(), r.GetZ());
}

int Physics::activeBodyCount() const {
    if (!mImpl->inited) return 0;
    return int(mImpl->system.GetNumActiveBodies(EBodyType::RigidBody));
}

void Physics::removeBody(BodyHandle h) {
    if (!h.valid() || h.id >= uint32_t(mImpl->bodies.size())) return;
    BodyRec& rec = mImpl->bodies[h.id];
    if (!rec.alive) return;
    BodyInterface& bi = mImpl->system.GetBodyInterface();
    bi.RemoveBody(rec.id);
    bi.DestroyBody(rec.id);
    mImpl->idToSlot.erase(rec.id.GetIndexAndSequenceNumber());
    rec.alive = false;
    rec.id    = BodyID();
    mImpl->freeList.push_back(h.id);
}

// ---- mesh body ----
BodyHandle Physics::createMeshBody(const std::vector<glm::vec3>& verts,
                                   const std::vector<uint32_t>& indices,
                                   glm::vec3 pos, glm::quat rot, BodyLayer layer) {
    if (!mImpl->inited) return {};

    VertexList jverts;
    jverts.reserve(verts.size());
    for (const auto& v : verts)
        jverts.push_back(Float3(v.x, v.y, v.z));

    IndexedTriangleList tris;
    tris.reserve(indices.size() / 3);
    for (size_t i = 0; i + 2 < indices.size(); i += 3)
        tris.push_back(IndexedTriangle(indices[i], indices[i+1], indices[i+2], 0));

    MeshShapeSettings settings(jverts, tris);
    ShapeSettings::ShapeResult res = settings.Create();
    if (res.HasError()) {
        std::fprintf(stderr, "[Physics] createMeshBody: %s\n", res.GetError().c_str());
        return {};
    }
    ShapeRefC shape = res.Get();

    RVec3 jpos(pos.x, pos.y, pos.z);
    Quat  jrot(rot.x, rot.y, rot.z, rot.w);
    BodyCreationSettings bcs(shape, jpos, jrot, EMotionType::Static, mapLayer(layer));

    BodyID bid = mImpl->system.GetBodyInterface().CreateAndAddBody(bcs, EActivation::DontActivate);
    if (bid.IsInvalid()) return {};

    uint32_t slot;
    if (!mImpl->freeList.empty()) {
        slot = mImpl->freeList.back();
        mImpl->freeList.pop_back();
    } else {
        slot = uint32_t(mImpl->bodies.size());
        mImpl->bodies.push_back(BodyRec{});
    }

    BodyRec& rec = mImpl->bodies[slot];
    rec.id       = bid;
    rec.dynamic  = false;
    rec.isStatic = true;
    rec.curPos   = jpos;
    rec.curRot   = jrot;
    rec.prevPos  = jpos;
    rec.prevRot  = jrot;
    rec.alive    = true;

    mImpl->idToSlot[bid.GetIndexAndSequenceNumber()] = slot;
    return BodyHandle{ slot };
}

// ---- ray cast ----
bool Physics::rayCast(glm::vec3 from, glm::vec3 dir, float dist, RayHit& outHit, BodyLayer mask) const {
    if (!mImpl->inited) return false;

    glm::vec3 normDir = glm::normalize(dir);
    RRayCast ray{ RVec3(from.x, from.y, from.z),
                  Vec3(normDir.x * dist, normDir.y * dist, normDir.z * dist) };
    RayCastResult result;

    // Layer filter: only collide with the requested layer
    struct LayerFilter final : public ObjectLayerFilter {
        ObjectLayer want;
        bool ShouldCollide(ObjectLayer l) const override { return l == want; }
    } layerFilter;
    layerFilter.want = mapLayer(mask);

    bool hit = mImpl->system.GetNarrowPhaseQuery().CastRay(ray, result, {}, layerFilter);
    if (!hit) return false;

    outHit.fraction = result.mFraction;
    glm::vec3 hitPoint = from + normDir * dist * result.mFraction;
    outHit.point = hitPoint;

    // Map BodyID -> BodyHandle via slot table
    auto it = mImpl->idToSlot.find(result.mBodyID.GetIndexAndSequenceNumber());
    outHit.body = (it != mImpl->idToSlot.end()) ? BodyHandle{ it->second } : BodyHandle{};

    // Get surface normal via body lock
    BodyLockRead lock(mImpl->system.GetBodyLockInterface(), result.mBodyID);
    if (lock.Succeeded()) {
        Vec3 n = lock.GetBody().GetWorldSpaceSurfaceNormal(
            result.mSubShapeID2, RVec3(hitPoint.x, hitPoint.y, hitPoint.z));
        outHit.normal = glm::vec3(n.GetX(), n.GetY(), n.GetZ());
    }

    return true;
}

// ---- stubs for later tasks (must exist so the header links) ----
void Physics::setBodyTransform(BodyHandle, glm::vec3, glm::quat) {}
void Physics::applyImpulse(BodyHandle, glm::vec3, glm::vec3) {}
void Physics::setBodyKinematic(BodyHandle, bool) {}
CharacterHandle Physics::createCharacter(const CharacterDesc&) { return {}; }
void Physics::removeCharacter(CharacterHandle) {}
void Physics::characterSetVelocity(CharacterHandle, glm::vec3) {}
void Physics::characterUpdate(CharacterHandle, float) {}
CharacterState Physics::characterState(CharacterHandle) const { return {}; }
void Physics::characterSetShape(CharacterHandle, float, float) {}
int  Physics::shapeCast(const BodyDesc&, glm::vec3, glm::vec3, std::vector<ShapeHit>&, BodyLayer) const { return 0; }
int  Physics::overlap(const BodyDesc&, glm::vec3, std::vector<ShapeHit>&, BodyLayer) const { return 0; }
void Physics::setContactCallback(HitCallback) {}

} // namespace eng
