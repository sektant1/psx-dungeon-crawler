#include "eng/Physics.h"
#include "eng/Log.h"
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
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace JPH;

namespace eng {

// Shared data that the contact listener writes to during Jolt's Update.
// Plain struct — no private visibility issues — passed by pointer to the listener.
struct ContactSharedData {
    Physics::HitCallback*                     contactCb = nullptr;
    std::mutex*                               contactMtx = nullptr;
    std::vector<HitEvent>*                    pendingContacts = nullptr;
    std::unordered_map<uint32_t, uint32_t>*   idToSlot = nullptr;
};

// Forward-declared so Physics::Impl can hold a unique_ptr to it;
// full definition follows after Impl.
class EngContactListener;

// ---- body record ----
struct BodyRec {
    JPH::BodyID id;
    bool dynamic  = false;
    bool isStatic = false;
    JPH::RVec3 prevPos = JPH::RVec3::sZero(), curPos  = JPH::RVec3::sZero();
    JPH::Quat  prevRot = JPH::Quat::sIdentity(), curRot = JPH::Quat::sIdentity();
    bool alive = false;
    BodyLayer layer = BodyLayer::Prop;
};

// ---- character record ----
struct CharacterRec {
    JPH::Ref<JPH::CharacterVirtual> ch;
    JPH::Vec3 desiredVelocity = JPH::Vec3::sZero();
    float radius = 0.3f, height = 1.7f, maxSlope = 0.8f, stepHeight = 0.4f;
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
    int   liveBodies = 0; // total bodies created and not yet removed

    // slot 0 is reserved as the null/invalid handle sentinel
    std::vector<BodyRec>               bodies;
    std::vector<uint32_t>              freeList;
    // keyed on BodyID::GetIndexAndSequenceNumber()
    std::unordered_map<uint32_t, uint32_t> idToSlot;

    // character table; slot 0 = null sentinel
    std::vector<CharacterRec> characters;
    std::vector<uint32_t>     charFreeList;
    phys::CharacterPushListener charPushListener;

    // contact seam
    Physics::HitCallback contactCb;
    std::mutex  contactMtx;
    std::vector<HitEvent> pendingContacts;
    ContactSharedData contactShared;
    std::unique_ptr<EngContactListener> listener;
};

// ContactListener: collects HitEvents into pendingContacts during Update;
// they are flushed (and the callback called) after Update returns.
class EngContactListener final : public JPH::ContactListener {
public:
    explicit EngContactListener(ContactSharedData* shared) : mShared(shared) {}

    void OnContactAdded(const JPH::Body& b1, const JPH::Body& b2,
                        const JPH::ContactManifold& manifold,
                        JPH::ContactSettings&) override
    {
        if (!mShared->contactCb || !*mShared->contactCb) return;

        // Map body IDs to BodyHandles via the id->slot table
        BodyHandle h1{}, h2{};
        {
            auto it = mShared->idToSlot->find(b1.GetID().GetIndexAndSequenceNumber());
            if (it != mShared->idToSlot->end()) h1 = BodyHandle{ it->second };
        }
        {
            auto it = mShared->idToSlot->find(b2.GetID().GetIndexAndSequenceNumber());
            if (it != mShared->idToSlot->end()) h2 = BodyHandle{ it->second };
        }

        JPH::RVec3 cp = manifold.GetWorldSpaceContactPointOn1(0);
        JPH::Vec3  n  = manifold.mWorldSpaceNormal;

        HitEvent ev;
        ev.self   = h1;
        ev.other  = h2;
        ev.point  = glm::vec3(float(cp.GetX()), float(cp.GetY()), float(cp.GetZ()));
        ev.normal = glm::vec3(-n.GetX(), -n.GetY(), -n.GetZ());
        ev.impulse = 0.0f;

        std::lock_guard<std::mutex> lock(*mShared->contactMtx);
        mShared->pendingContacts->push_back(ev);
    }

private:
    ContactSharedData* mShared;
};

// ---- helpers ----
static JPH::RefConst<JPH::Shape> makeCharShape(float radius, float height) {
    float cyl = std::max(0.0f, height - 2.0f * radius);
    JPH::RefConst<JPH::Shape> capsule = new JPH::CapsuleShape(cyl * 0.5f, radius);
    return JPH::RotatedTranslatedShapeSettings(
        JPH::Vec3(0, height * 0.5f, 0), JPH::Quat::sIdentity(), capsule).Create().Get();
}

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
    mImpl->characters.push_back(CharacterRec{}); // slot 0: dead, invalid
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
    // Register the contact listener so we can forward HitEvents to game code.
    // The listener is owned by the Impl; it must outlive the PhysicsSystem.
    mImpl->contactShared.contactCb      = &mImpl->contactCb;
    mImpl->contactShared.contactMtx     = &mImpl->contactMtx;
    mImpl->contactShared.pendingContacts = &mImpl->pendingContacts;
    mImpl->contactShared.idToSlot       = &mImpl->idToSlot;
    mImpl->listener = std::make_unique<EngContactListener>(&mImpl->contactShared);
    mImpl->system.SetContactListener(mImpl->listener.get());
    mImpl->charPushListener.system = &mImpl->system;
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

    JPH::EPhysicsUpdateError updateErr =
        mImpl->system.Update(dt, steps, mImpl->temp.get(), mImpl->jobs.get());
    if (updateErr != JPH::EPhysicsUpdateError::None) {
        unsigned bits = (unsigned)updateErr;
        const char* manifold = (bits & (unsigned)JPH::EPhysicsUpdateError::ManifoldCacheFull)   ? " ManifoldCacheFull"   : "";
        const char* bodypair = (bits & (unsigned)JPH::EPhysicsUpdateError::BodyPairCacheFull)    ? " BodyPairCacheFull"   : "";
        const char* contacts = (bits & (unsigned)JPH::EPhysicsUpdateError::ContactConstraintsFull) ? " ContactConstraintsFull" : "";
        eng::log::error("Physics update error bits: %u%s%s%s", bits, manifold, bodypair, contacts);
    }

    // read back updated transforms
    BodyInterface& bi = mImpl->system.GetBodyInterface();
    for (auto& rec : mImpl->bodies) {
        if (!rec.alive || rec.isStatic) continue;
        bi.GetPositionAndRotation(rec.id, rec.curPos, rec.curRot);
    }

    // Flush deferred contact events collected during Update (called from job threads).
    // We hold the lock just long enough to swap out the vector, then call the
    // callback from the main thread with no lock held.
    if (mImpl->contactCb) {
        std::vector<HitEvent> batch;
        {
            std::lock_guard<std::mutex> lock(mImpl->contactMtx);
            batch.swap(mImpl->pendingContacts);
        }
        for (const HitEvent& ev : batch)
            mImpl->contactCb(ev);
    }
}

float Physics::interpolationAlpha() const { return mImpl->alpha; }
void  Physics::setInterpolationAlpha(float a) { mImpl->alpha = a; }

void Physics::shutdown() {
    if (!mImpl || !mImpl->inited) return;
    // release all characters first (they may hold inner body IDs)
    for (auto& rec : mImpl->characters) {
        if (!rec.alive) continue;
        rec.ch = nullptr;
        rec.alive = false;
    }
    mImpl->charFreeList.clear();
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
    rec.layer     = desc.layer;

    mImpl->idToSlot[bid.GetIndexAndSequenceNumber()] = slot;
    ++mImpl->liveBodies;
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

int Physics::bodyCount() const {
    return mImpl->liveBodies;
}

void Physics::setGravity(float y) { mImpl->system.SetGravity(JPH::Vec3(0, y, 0)); }
float Physics::gravityY() const { return mImpl->system.GetGravity().GetY(); }

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
    --mImpl->liveBodies;
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
    rec.layer    = layer;

    mImpl->idToSlot[bid.GetIndexAndSequenceNumber()] = slot;
    ++mImpl->liveBodies;
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

// ---- character management ----
CharacterHandle Physics::createCharacter(const CharacterDesc& desc) {
    if (!mImpl->inited) return {};

    JPH::CharacterVirtualSettings settings;
    settings.mShape = makeCharShape(desc.radius, desc.height);
    settings.mMaxSlopeAngle = glm::radians(desc.maxSlopeDeg);
    settings.mMass = desc.mass;
    settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -desc.radius);
    settings.mInnerBodyLayer = phys::Layers::PLAYER;
    settings.mInnerBodyShape = settings.mShape;

    JPH::Ref<JPH::CharacterVirtual> cv = new JPH::CharacterVirtual(
        &settings,
        JPH::RVec3(desc.position.x, desc.position.y, desc.position.z),
        JPH::Quat::sIdentity(),
        0,
        &mImpl->system);
    cv->SetListener(&mImpl->charPushListener);

    uint32_t slot;
    if (!mImpl->charFreeList.empty()) {
        slot = mImpl->charFreeList.back();
        mImpl->charFreeList.pop_back();
    } else {
        slot = uint32_t(mImpl->characters.size());
        mImpl->characters.push_back(CharacterRec{});
    }

    CharacterRec& rec = mImpl->characters[slot];
    rec.ch = cv;
    rec.desiredVelocity = JPH::Vec3::sZero();
    rec.radius = desc.radius;
    rec.height = desc.height;
    rec.maxSlope = glm::radians(desc.maxSlopeDeg);
    rec.stepHeight = desc.stepHeight;
    rec.alive = true;

    return CharacterHandle{ slot };
}

void Physics::removeCharacter(CharacterHandle h) {
    if (!h.valid() || h.id >= uint32_t(mImpl->characters.size())) return;
    CharacterRec& rec = mImpl->characters[h.id];
    if (!rec.alive) return;
    rec.ch = nullptr;
    rec.alive = false;
    mImpl->charFreeList.push_back(h.id);
}

void Physics::characterSetVelocity(CharacterHandle h, glm::vec3 velocity) {
    if (!h.valid() || h.id >= uint32_t(mImpl->characters.size())) return;
    CharacterRec& rec = mImpl->characters[h.id];
    if (!rec.alive) return;
    rec.desiredVelocity = JPH::Vec3(velocity.x, velocity.y, velocity.z);
}

void Physics::characterUpdate(CharacterHandle h, float dt) {
    if (!h.valid() || h.id >= uint32_t(mImpl->characters.size())) return;
    CharacterRec& rec = mImpl->characters[h.id];
    if (!rec.alive) return;

    rec.ch->SetLinearVelocity(rec.desiredVelocity);

    JPH::CharacterVirtual::ExtendedUpdateSettings us;
    us.mWalkStairsStepUp = JPH::Vec3(0, rec.stepHeight, 0);

    rec.ch->ExtendedUpdate(
        dt,
        mImpl->system.GetGravity(),
        us,
        mImpl->system.GetDefaultBroadPhaseLayerFilter(phys::Layers::PLAYER),
        mImpl->system.GetDefaultLayerFilter(phys::Layers::PLAYER),
        {},
        {},
        *mImpl->temp);
}

CharacterState Physics::characterState(CharacterHandle h) const {
    if (!h.valid() || h.id >= uint32_t(mImpl->characters.size())) return {};
    const CharacterRec& rec = mImpl->characters[h.id];
    if (!rec.alive) return {};

    CharacterState st;
    JPH::RVec3 p = rec.ch->GetPosition();
    st.position = { float(p.GetX()), float(p.GetY()), float(p.GetZ()) };
    JPH::Vec3 v = rec.ch->GetLinearVelocity();
    st.velocity = { v.GetX(), v.GetY(), v.GetZ() };
    JPH::Vec3 n = rec.ch->GetGroundNormal();
    st.groundNormal = { n.GetX(), n.GetY(), n.GetZ() };
    switch (rec.ch->GetGroundState()) {
        case JPH::CharacterBase::EGroundState::OnGround:
            st.ground = GroundState::OnGround; break;
        case JPH::CharacterBase::EGroundState::OnSteepGround:
            st.ground = GroundState::OnSteepSlope; break;
        default:
            st.ground = GroundState::InAir; break;
    }
    return st;
}

void Physics::characterSetShape(CharacterHandle h, float radius, float height) {
    if (!h.valid() || h.id >= uint32_t(mImpl->characters.size())) return;
    CharacterRec& rec = mImpl->characters[h.id];
    if (!rec.alive) return;

    JPH::RefConst<JPH::Shape> newShape = makeCharShape(radius, height);
    bool ok = rec.ch->SetShape(
        newShape,
        FLT_MAX,
        mImpl->system.GetDefaultBroadPhaseLayerFilter(phys::Layers::PLAYER),
        mImpl->system.GetDefaultLayerFilter(phys::Layers::PLAYER),
        {},
        {},
        *mImpl->temp);
    if (ok) {
        rec.radius = radius;
        rec.height = height;
    }
}

// ---- stubs for later tasks (must exist so the header links) ----
void Physics::setBodyTransform(BodyHandle, glm::vec3, glm::quat) {}

void Physics::applyImpulse(BodyHandle h, glm::vec3 impulse, glm::vec3 atPoint) {
    if (!h.valid() || h.id >= uint32_t(mImpl->bodies.size())) return;
    BodyRec& rec = mImpl->bodies[h.id];
    if (!rec.alive) return;
    BodyInterface& bi = mImpl->system.GetBodyInterface();
    bi.AddImpulse(rec.id,
                  JPH::Vec3(impulse.x, impulse.y, impulse.z),
                  JPH::RVec3(atPoint.x, atPoint.y, atPoint.z));
    bi.ActivateBody(rec.id);
}

void Physics::setBodyKinematic(BodyHandle h, bool kinematic) {
    if (!h.valid() || h.id >= uint32_t(mImpl->bodies.size())) return;
    BodyRec& rec = mImpl->bodies[h.id];
    if (!rec.alive) return;
    BodyInterface& bi = mImpl->system.GetBodyInterface();
    bi.SetMotionType(rec.id,
                     kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic,
                     JPH::EActivation::Activate);
}
int Physics::shapeCast(const BodyDesc& shape, glm::vec3 from, glm::vec3 to,
                       std::vector<ShapeHit>& out, BodyLayer mask) const {
    if (!mImpl->inited) return 0;

    JPH::ShapeRefC shapeRef = makeShape(shape);

    // Layer filter: only collide with the requested layer.
    struct LayerFilter final : public JPH::ObjectLayerFilter {
        JPH::ObjectLayer want;
        bool ShouldCollide(JPH::ObjectLayer l) const override { return l == want; }
    } layerFilter;
    layerFilter.want = mapLayer(mask);

    JPH::Vec3 dir(to.x - from.x, to.y - from.y, to.z - from.z);
    JPH::RShapeCast cast(
        shapeRef.GetPtr(),
        JPH::Vec3::sReplicate(1.0f),
        JPH::RMat44::sTranslation(JPH::RVec3(from.x, from.y, from.z)),
        dir);

    JPH::AllHitCollisionCollector<JPH::CastShapeCollector> collector;
    JPH::ShapeCastSettings settings;
    mImpl->system.GetNarrowPhaseQuery().CastShape(
        cast, settings, JPH::RVec3::sZero(),
        collector, {}, layerFilter);

    // De-dupe: one ShapeHit per body.
    std::unordered_map<uint32_t, bool> seen;
    for (const auto& hit : collector.mHits) {
        uint32_t key = hit.mBodyID2.GetIndexAndSequenceNumber();
        if (seen.count(key)) continue;
        seen[key] = true;

        auto it = mImpl->idToSlot.find(key);
        BodyHandle bh = (it != mImpl->idToSlot.end()) ? BodyHandle{ it->second } : BodyHandle{};

        JPH::RVec3 pt = cast.GetPointOnRay(hit.mFraction);
        JPH::Vec3  n  = -hit.mPenetrationAxis.Normalized();

        ShapeHit sh;
        sh.body        = bh;
        sh.point       = glm::vec3(float(pt.GetX()), float(pt.GetY()), float(pt.GetZ()));
        sh.normal      = glm::vec3(n.GetX(), n.GetY(), n.GetZ());
        sh.penetration = hit.mPenetrationDepth;
        out.push_back(sh);
    }
    return int(out.size());
}

int Physics::overlap(const BodyDesc& shape, glm::vec3 at,
                     std::vector<ShapeHit>& out, BodyLayer mask) const {
    if (!mImpl->inited) return 0;

    JPH::ShapeRefC shapeRef = makeShape(shape);

    struct LayerFilter final : public JPH::ObjectLayerFilter {
        JPH::ObjectLayer want;
        bool ShouldCollide(JPH::ObjectLayer l) const override { return l == want; }
    } layerFilter;
    layerFilter.want = mapLayer(mask);

    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    mImpl->system.GetNarrowPhaseQuery().CollideShape(
        shapeRef.GetPtr(),
        JPH::Vec3::sReplicate(1.0f),
        JPH::RMat44::sTranslation(JPH::RVec3(at.x, at.y, at.z)),
        JPH::CollideShapeSettings{},
        JPH::RVec3::sZero(),
        collector, {}, layerFilter);

    std::unordered_map<uint32_t, bool> seen;
    for (const auto& hit : collector.mHits) {
        uint32_t key = hit.mBodyID2.GetIndexAndSequenceNumber();
        if (seen.count(key)) continue;
        seen[key] = true;

        auto it = mImpl->idToSlot.find(key);
        BodyHandle bh = (it != mImpl->idToSlot.end()) ? BodyHandle{ it->second } : BodyHandle{};

        JPH::Vec3 n = -hit.mPenetrationAxis.Normalized();
        ShapeHit sh;
        sh.body        = bh;
        sh.point       = glm::vec3(hit.mContactPointOn2.GetX(),
                                   hit.mContactPointOn2.GetY(),
                                   hit.mContactPointOn2.GetZ());
        sh.normal      = glm::vec3(n.GetX(), n.GetY(), n.GetZ());
        sh.penetration = hit.mPenetrationDepth;
        out.push_back(sh);
    }
    return int(out.size());
}
void Physics::setContactCallback(HitCallback cb) {
    mImpl->contactCb = std::move(cb);
}

// ---- debug draw ----
static void pushBoxEdges(std::vector<Physics::DebugLine>& out,
                         const glm::vec3& mn, const glm::vec3& mx,
                         const glm::vec3& col)
{
    // 8 corners of the AABB
    glm::vec3 c[8] = {
        {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z},
        {mx.x, mn.y, mx.z}, {mn.x, mn.y, mx.z},
        {mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z},
        {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z},
    };
    // 12 edges: 4 bottom, 4 top, 4 vertical
    static const int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0}, // bottom
        {4,5},{5,6},{6,7},{7,4}, // top
        {0,4},{1,5},{2,6},{3,7}, // verticals
    };
    for (auto& e : edges)
        out.push_back({c[e[0]], c[e[1]], col});
}

void Physics::debugDraw(std::vector<DebugLine>& out) const
{
    if (!mImpl->inited) return;

    BodyInterface& bi = mImpl->system.GetBodyInterface();

    for (const auto& rec : mImpl->bodies) {
        if (!rec.alive) continue;

        // Colour by layer
        glm::vec3 col{1.0f, 1.0f, 1.0f};
        switch (rec.layer) {
            case BodyLayer::Static:     col = {0.5f, 0.5f, 0.5f}; break;
            case BodyLayer::Prop:       col = {0.2f, 1.0f, 0.2f}; break;
            case BodyLayer::Projectile: col = {1.0f, 1.0f, 0.2f}; break;
            case BodyLayer::Player:     col = {0.2f, 0.8f, 1.0f}; break;
            case BodyLayer::Trigger:    col = {1.0f, 0.4f, 1.0f}; break;
        }

        // Read the body's world-space AABB under a lock to avoid races.
        JPH::BodyLockRead lock(mImpl->system.GetBodyLockInterface(), rec.id);
        if (!lock.Succeeded()) continue;
        const JPH::AABox& box = lock.GetBody().GetWorldSpaceBounds();
        const JPH::Vec3& mn = box.mMin;
        const JPH::Vec3& mx = box.mMax;
        pushBoxEdges(out,
                     {mn.GetX(), mn.GetY(), mn.GetZ()},
                     {mx.GetX(), mx.GetY(), mx.GetZ()},
                     col);
    }

    // Characters: draw a box from their radius/height
    const glm::vec3 charCol{0.2f, 0.8f, 1.0f};
    for (const auto& rec : mImpl->characters) {
        if (!rec.alive || !rec.ch) continue;
        JPH::RVec3 p = rec.ch->GetPosition();
        const float r = rec.radius;
        const float h = rec.height;
        glm::vec3 centre(float(p.GetX()), float(p.GetY()) + h * 0.5f, float(p.GetZ()));
        glm::vec3 half{r, h * 0.5f, r};
        pushBoxEdges(out, centre - half, centre + half, charCol);
    }
}

} // namespace eng
