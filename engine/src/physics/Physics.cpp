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
#include <cstdlib>
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
    CollisionLayer layer = 0;
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
    // The application's layer table, held for the lifetime of the world: the
    // Jolt filters below keep a pointer into it.
    PhysicsSetup                   setup;
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

// Narrow-phase query filter: collide with every layer in the caller's mask.
// Shared by rayCast/shapeCast/overlap.
struct MaskLayerFilter final : public JPH::ObjectLayerFilter {
    CollisionMask mask;
    explicit MaskLayerFilter(CollisionMask m) : mask(m) {}
    bool ShouldCollide(JPH::ObjectLayer l) const override {
        return l < kMaxCollisionLayers &&
               any(mask & layerMask(CollisionLayer(l)));
    }
};

// Character sweep filter: the collision matrix row for the character's own
// layer, further narrowed by the caller's mask. With the default kAllLayers
// this is exactly Jolt's GetDefaultLayerFilter, i.e. unchanged behaviour;
// clearing a bit is how gameplay makes something pass-through for the player
// (a ghost phase, a one-way gate) without touching the world matrix, which
// every other body still shares.
struct CharacterSweepFilter final : public JPH::ObjectLayerFilter {
    CollisionMask allowed;
    CharacterSweepFilter(const PhysicsSetup& setup, CollisionMask mask)
        : allowed(setup.collides[setup.characterLayer] & mask) {}
    bool ShouldCollide(JPH::ObjectLayer l) const override {
        return l < kMaxCollisionLayers &&
               any(allowed & layerMask(CollisionLayer(l)));
    }
};

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

// Allocate a body-table slot: reuse the free list or grow. Shared by
// createBody/createMeshBody (identical push-back logic in both).
static uint32_t allocBodySlot(std::vector<BodyRec>& bodies,
                              std::vector<uint32_t>& freeList) {
    if (!freeList.empty()) {
        uint32_t slot = freeList.back();
        freeList.pop_back();
        return slot;
    }
    uint32_t slot = uint32_t(bodies.size());
    bodies.push_back(BodyRec{});
    return slot;
}

// ---- lifecycle ----
Physics::Physics() : mImpl(std::make_unique<Impl>()) {
    // slot 0 is the null sentinel — reserve it now
    mImpl->bodies.push_back(BodyRec{}); // slot 0: dead, invalid
    mImpl->characters.push_back(CharacterRec{}); // slot 0: dead, invalid
}
Physics::~Physics() { shutdown(); }

PhysicsSetup PhysicsSetup::generic()
{
    PhysicsSetup s;
    s.layers = {
        {"static", /*moving=*/false, {0.5f, 0.5f, 0.5f}},
        {"dynamic", /*moving=*/true, {0.2f, 1.0f, 0.2f}},
    };
    s.collideAll();
    s.setPair(0, 0, false); // static geometry against itself is dead work
    s.characterLayer = 1;
    return s;
}

void Physics::init(const PhysicsSetup& setup) {
    mImpl->setup = setup;
    if (mImpl->setup.layers.empty()) {
        // Supply only the missing collision taxonomy. Keep caller-provided
        // tuning such as gravity and worker policy intact.
        PhysicsSetup defaults = PhysicsSetup::generic();
        mImpl->setup.layers = std::move(defaults.layers);
        mImpl->setup.collides = defaults.collides;
        mImpl->setup.characterLayer = defaults.characterLayer;
    }
    if (mImpl->setup.layers.size() > kMaxCollisionLayers) {
        log::error("Physics: %zu layers requested, %d supported; extra layers "
                   "are ignored",
                   mImpl->setup.layers.size(), kMaxCollisionLayers);
        mImpl->setup.layers.resize(kMaxCollisionLayers);
    }
    if (mImpl->setup.characterLayer >= mImpl->setup.layers.size()) {
        log::error("Physics: character layer %u is outside the %zu configured "
                   "layers; using layer 0",
                   unsigned(mImpl->setup.characterLayer),
                   mImpl->setup.layers.size());
        mImpl->setup.characterLayer = 0;
    }
    mImpl->bp.setup  = &mImpl->setup;
    mImpl->opp.setup = &mImpl->setup;
    RegisterDefaultAllocator();
    if (!Factory::sInstance) Factory::sInstance = new Factory();
    RegisterTypes();
    mImpl->temp = std::make_unique<TempAllocatorImpl>(16 * 1024 * 1024);
    // Determinism by default: multi-threaded Jolt resolves contacts in a
    // thread-race order, so dynamic props settle differently every run. A
    // single worker is reproducible frame for frame, which is what boss-fight
    // retries and deterministic capture both need, so that is the default and
    // PhysicsSetup::multithreaded is the explicit opt-out. Capture mode
    // (PSX_SCREENSHOT/PSX_FIXED_DT, matching Engine's fixed-timestep capture)
    // still forces a single worker even if the application opted in, so a
    // capture is reproducible regardless of how the game is configured.
    const bool forceDeterministic =
        std::getenv("PSX_SCREENSHOT") || std::getenv("PSX_FIXED_DT");
    unsigned threads = 1u;
    if (mImpl->setup.multithreaded && !forceDeterministic) {
        const unsigned hardwareThreads = std::thread::hardware_concurrency();
        threads = mImpl->setup.workerThreads > 0
                      ? unsigned(mImpl->setup.workerThreads)
                      : (hardwareThreads > 1u ? hardwareThreads - 1u : 1u);
    }
    mImpl->jobs = std::make_unique<JobSystemThreadPool>(cMaxPhysicsJobs, cMaxPhysicsBarriers, int(threads));
    mImpl->system.Init(4096, 0, 4096, 4096, mImpl->bp, mImpl->ovb, mImpl->opp);
    mImpl->system.SetGravity(Vec3(mImpl->setup.gravity.x, mImpl->setup.gravity.y,
                                  mImpl->setup.gravity.z));
    // Register the contact listener so we can forward HitEvents to game code.
    // The listener is owned by the Impl; it must outlive the PhysicsSystem.
    mImpl->contactShared.contactCb      = &mImpl->contactCb;
    mImpl->contactShared.contactMtx     = &mImpl->contactMtx;
    mImpl->contactShared.pendingContacts = &mImpl->pendingContacts;
    mImpl->contactShared.idToSlot       = &mImpl->idToSlot;
    mImpl->listener = std::make_unique<EngContactListener>(&mImpl->contactShared);
    mImpl->system.SetContactListener(mImpl->listener.get());
    mImpl->charPushListener.system = &mImpl->system;
    mImpl->charPushListener.pushImpulse = mImpl->setup.characterPushImpulse;
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
    mImpl->liveBodies = 0;
    mImpl->idToSlot.clear();
    mImpl->freeList.clear();
    mImpl->pendingContacts.clear();
    mImpl->system.SetContactListener(nullptr);
    mImpl->listener.reset();
    mImpl->jobs.reset();
    mImpl->temp.reset();
    UnregisterTypes();
    delete Factory::sInstance; Factory::sInstance = nullptr;
    mImpl->inited = false;
}

// ---- body management ----
BodyHandle Physics::createBody(const BodyDesc& desc) {
    if (!mImpl->inited) return {};
    if (desc.layer >= mImpl->setup.layers.size()) {
        log::error("Physics: body layer %u is outside the %zu configured layers",
                   unsigned(desc.layer), mImpl->setup.layers.size());
        return {};
    }

    ShapeRefC shape = makeShape(desc);

    EMotionType motionType = desc.dynamic ? EMotionType::Dynamic : EMotionType::Static;
    ObjectLayer layer      = ObjectLayer(desc.layer);

    RVec3 pos(desc.position.x, desc.position.y, desc.position.z);
    Quat  rot(desc.orientation.x, desc.orientation.y, desc.orientation.z, desc.orientation.w);

    BodyCreationSettings bcs(shape, pos, rot, motionType, layer);
    bcs.mFriction    = desc.friction;
    bcs.mRestitution = desc.restitution;
    bcs.mIsSensor    = desc.sensor;
    bcs.mGravityFactor = std::max(0.0f, desc.gravityFactor);
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
    uint32_t slot = allocBodySlot(mImpl->bodies, mImpl->freeList);

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
                                   glm::vec3 pos, glm::quat rot, CollisionLayer layer) {
    if (!mImpl->inited) return {};
    if (layer >= mImpl->setup.layers.size()) {
        log::error("Physics: mesh body layer %u is outside the %zu configured "
                   "layers",
                   unsigned(layer), mImpl->setup.layers.size());
        return {};
    }

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
    BodyCreationSettings bcs(shape, jpos, jrot, EMotionType::Static, ObjectLayer(layer));

    BodyID bid = mImpl->system.GetBodyInterface().CreateAndAddBody(bcs, EActivation::DontActivate);
    if (bid.IsInvalid()) return {};

    uint32_t slot = allocBodySlot(mImpl->bodies, mImpl->freeList);

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
bool Physics::rayCast(glm::vec3 from, glm::vec3 dir, float dist, RayHit& outHit,
                      CollisionMask mask) const {
    if (!mImpl->inited) return false;

    glm::vec3 normDir = glm::normalize(dir);
    RRayCast ray{ RVec3(from.x, from.y, from.z),
                  Vec3(normDir.x * dist, normDir.y * dist, normDir.z * dist) };
    RayCastResult result;

    MaskLayerFilter layerFilter(mask);
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
    settings.mInnerBodyLayer = ObjectLayer(mImpl->setup.characterLayer);
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

void Physics::characterUpdate(CharacterHandle h, float dt, CollisionMask mask) {
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
        mImpl->system.GetDefaultBroadPhaseLayerFilter(
            ObjectLayer(mImpl->setup.characterLayer)),
        CharacterSweepFilter(mImpl->setup, mask),
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
        mImpl->system.GetDefaultBroadPhaseLayerFilter(
            ObjectLayer(mImpl->setup.characterLayer)),
        mImpl->system.GetDefaultLayerFilter(
            ObjectLayer(mImpl->setup.characterLayer)),
        {},
        {},
        *mImpl->temp);
    if (ok) {
        rec.radius = radius;
        rec.height = height;
    }
}

void Physics::setBodyTransform(BodyHandle h, glm::vec3 position,
                               glm::quat orientation) {
    if (!h.valid() || h.id >= uint32_t(mImpl->bodies.size())) return;
    BodyRec& rec = mImpl->bodies[h.id];
    if (!rec.alive) return;

    const JPH::RVec3 p(position.x, position.y, position.z);
    const JPH::Quat q(orientation.x, orientation.y, orientation.z,
                      orientation.w);
    mImpl->system.GetBodyInterface().SetPositionAndRotation(
        rec.id, p, q,
        rec.isStatic ? JPH::EActivation::DontActivate
                     : JPH::EActivation::Activate);

    // A transform assignment is a teleport, not a simulated step. Reset both
    // interpolation endpoints so presentation cannot smear from the old pose.
    rec.prevPos = rec.curPos = p;
    rec.prevRot = rec.curRot = q;
}

void Physics::applyImpulse(BodyHandle h, glm::vec3 impulse, glm::vec3 atPoint) {
    if (!h.valid() || h.id >= uint32_t(mImpl->bodies.size())) return;
    BodyRec& rec = mImpl->bodies[h.id];
    if (!rec.alive) return;
    BodyInterface& bi = mImpl->system.GetBodyInterface();
    // Activate FIRST. Jolt drops an impulse aimed at a sleeping body, so
    // activating afterwards woke it with nothing applied -- which is a knockback
    // that does nothing to a prop that had settled, and everything to one that
    // happened to still be moving. The order is the whole fix.
    bi.ActivateBody(rec.id);
    bi.AddImpulse(rec.id,
                  JPH::Vec3(impulse.x, impulse.y, impulse.z),
                  JPH::RVec3(atPoint.x, atPoint.y, atPoint.z));
}

void Physics::setBodyKinematic(BodyHandle h, bool kinematic) {
    if (!h.valid() || h.id >= uint32_t(mImpl->bodies.size())) return;
    BodyRec& rec = mImpl->bodies[h.id];
    if (!rec.alive) return;
    BodyInterface& bi = mImpl->system.GetBodyInterface();
    bi.SetMotionType(rec.id,
                     kinematic ? JPH::EMotionType::Kinematic : JPH::EMotionType::Dynamic,
                     JPH::EActivation::Activate);
    // Kinematic and dynamic bodies both participate in transform readback and
    // interpolation. Without this, a body created static and promoted to
    // kinematic remains frozen in the render-facing record.
    rec.dynamic = true;
    rec.isStatic = false;
}
int Physics::shapeCast(const BodyDesc& shape, glm::vec3 from, glm::vec3 to,
                       std::vector<ShapeHit>& out, CollisionMask mask) const {
    if (!mImpl->inited) return 0;

    JPH::ShapeRefC shapeRef = makeShape(shape);

    MaskLayerFilter layerFilter(mask);

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
                     std::vector<ShapeHit>& out, CollisionMask mask) const {
    if (!mImpl->inited) return 0;

    JPH::ShapeRefC shapeRef = makeShape(shape);

    MaskLayerFilter layerFilter(mask);

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

// ---- debug draw ----------------------------------------------------------
// A proper collider debugger draws each body's ACTUAL collision shape, oriented
// by the body transform and at its true dimensions (not a world-axis AABB): an
// oriented box, sphere great-circles, a capsule, a cylinder. That is what makes
// the overlay reveal that a collider is rotated, or larger than the visual mesh.
namespace {

constexpr int kCircleSegs = 20; // segments per debug circle

// Transform a shape-local point by a Jolt body transform into glm world space.
inline glm::vec3 xf(const JPH::RMat44& m, float x, float y, float z)
{
    JPH::RVec3 w = m * JPH::Vec3(x, y, z);
    return glm::vec3(float(w.GetX()), float(w.GetY()), float(w.GetZ()));
}

void pushOrientedBox(std::vector<Physics::DebugLine>& out, const JPH::RMat44& m,
                     JPH::Vec3Arg he, const glm::vec3& col)
{
    const float x = he.GetX(), y = he.GetY(), z = he.GetZ();
    const glm::vec3 c[8] = {
        xf(m, -x, -y, -z), xf(m, x, -y, -z), xf(m, x, -y, z), xf(m, -x, -y, z),
        xf(m, -x, y, -z),  xf(m, x, y, -z),  xf(m, x, y, z),  xf(m, -x, y, z),
    };
    static const int e[12][2] = {{0,1},{1,2},{2,3},{3,0}, {4,5},{5,6},{6,7},{7,4},
                                 {0,4},{1,5},{2,6},{3,7}};
    for (auto& ed : e)
        out.push_back({c[ed[0]], c[ed[1]], col});
}

// A circle of radius r in the plane spanned by local axes (axisA, axisB),
// centred at local `centre`. axis chars: 0=X,1=Y,2=Z.
void pushCircle(std::vector<Physics::DebugLine>& out, const JPH::RMat44& m,
                const glm::vec3& centre, float r, int axisA, int axisB,
                const glm::vec3& col)
{
    glm::vec3 prev;
    for (int i = 0; i <= kCircleSegs; ++i) {
        const float t = float(i) / kCircleSegs * 6.2831853f;
        float p[3] = {centre.x, centre.y, centre.z};
        p[axisA] += r * std::cos(t);
        p[axisB] += r * std::sin(t);
        const glm::vec3 cur = xf(m, p[0], p[1], p[2]);
        if (i > 0)
            out.push_back({prev, cur, col});
        prev = cur;
    }
}

void pushSphere(std::vector<Physics::DebugLine>& out, const JPH::RMat44& m,
                float r, const glm::vec3& col)
{
    pushCircle(out, m, {0,0,0}, r, 0, 1, col); // XY
    pushCircle(out, m, {0,0,0}, r, 0, 2, col); // XZ
    pushCircle(out, m, {0,0,0}, r, 1, 2, col); // YZ
}

// Capsule / cylinder along the local Y axis (Jolt convention). hh = half height
// of the cylindrical section; capped = hemispherical end caps (capsule).
void pushCapsuleOrCylinder(std::vector<Physics::DebugLine>& out,
                           const JPH::RMat44& m, float hh, float r, bool capped,
                           const glm::vec3& col)
{
    pushCircle(out, m, {0,  hh, 0}, r, 0, 2, col); // top ring
    pushCircle(out, m, {0, -hh, 0}, r, 0, 2, col); // bottom ring
    // 4 side lines connecting the rings.
    const float ang[4] = {0.0f, 1.5707963f, 3.1415927f, 4.712389f};
    for (float a : ang) {
        const float dx = r * std::cos(a), dz = r * std::sin(a);
        out.push_back({xf(m, dx, hh, dz), xf(m, dx, -hh, dz), col});
    }
    if (capped) {
        // Two half-circles per vertical plane. Keeping the top and bottom arcs
        // separate avoids connector segments jumping between hemisphere
        // centres at the equator.
        const int arcSegs = kCircleSegs / 2;
        for (int plane = 0; plane < 2; ++plane) {
            for (int cap = 0; cap < 2; ++cap) {
                const float begin = cap == 0 ? 0.0f : 3.1415927f;
                const float centreY = cap == 0 ? hh : -hh;
                for (int i = 0; i < arcSegs; ++i) {
                    const float t0 = begin + float(i) / arcSegs * 3.1415927f;
                    const float t1 = begin + float(i + 1) / arcSegs * 3.1415927f;
                    auto point = [&](float t) {
                        const float horizontal = r * std::cos(t);
                        const float y = centreY + r * std::sin(t);
                        return plane == 0 ? xf(m, horizontal, y, 0.0f)
                                          : xf(m, 0.0f, y, horizontal);
                    };
                    out.push_back({point(t0), point(t1), col});
                }
            }
        }
    }
}

// The colour is the application's, from its layer table; the engine has no
// opinion on which layer is which.
glm::vec3 layerColour(const PhysicsSetup& setup, CollisionLayer layer)
{
    if (size_t(layer) < setup.layers.size())
        return setup.layers[size_t(layer)].debugColour;
    return {1.0f, 1.0f, 1.0f};
}

// Distinct colour per collision-shape kind (Unity-debug-display style).
glm::vec3 shapeColour(JPH::EShapeSubType sub)
{
    using JPH::EShapeSubType;
    switch (sub) {
        case EShapeSubType::Box:      return {0.30f, 1.00f, 0.45f}; // green
        case EShapeSubType::Sphere:   return {0.30f, 0.85f, 1.00f}; // cyan
        case EShapeSubType::Capsule:  return {1.00f, 0.90f, 0.30f}; // yellow
        case EShapeSubType::Cylinder: return {1.00f, 0.60f, 0.20f}; // orange
        default:                      return {1.00f, 0.30f, 0.90f}; // magenta (mesh/hull)
    }
}

// Draw one shape's wireframe under transform `m`. Handles the primitive shapes
// props/characters use; anything else (mesh/hull/compound) falls back to its
// oriented local bounding box.
void pushShape(std::vector<Physics::DebugLine>& out, const JPH::Shape* shape,
               const JPH::RMat44& m, const glm::vec3& col)
{
    using JPH::EShapeSubType;
    switch (shape->GetSubType()) {
        case EShapeSubType::Box:
            pushOrientedBox(out, m,
                static_cast<const JPH::BoxShape*>(shape)->GetHalfExtent(), col);
            return;
        case EShapeSubType::Sphere:
            pushSphere(out, m,
                static_cast<const JPH::SphereShape*>(shape)->GetRadius(), col);
            return;
        case EShapeSubType::Capsule: {
            auto* c = static_cast<const JPH::CapsuleShape*>(shape);
            pushCapsuleOrCylinder(out, m, c->GetHalfHeightOfCylinder(),
                                  c->GetRadius(), true, col);
            return;
        }
        case EShapeSubType::Cylinder: {
            auto* c = static_cast<const JPH::CylinderShape*>(shape);
            pushCapsuleOrCylinder(out, m, c->GetHalfHeight(), c->GetRadius(),
                                  false, col);
            return;
        }
        default: {
            // Mesh / convex hull / compound: oriented local-bounds box.
            const JPH::AABox b = shape->GetLocalBounds();
            const JPH::Vec3 he = b.GetExtent();
            const JPH::Vec3 ctr = b.GetCenter();
            pushOrientedBox(out, m * JPH::RMat44::sTranslation(
                                        JPH::RVec3(ctr.GetX(), ctr.GetY(),
                                                   ctr.GetZ())),
                            he, col);
            return;
        }
    }
}

// Turn the lines in [first, out.size()) into dashes by shortening each one to
// its first half. Applied after the fact, so it works uniformly for every
// primitive -- box edges, circle segments, capsule cap arcs -- without any of
// the shape emitters knowing that dashing exists.
void dashRange(std::vector<Physics::DebugLine>& out, size_t first)
{
    for (size_t i = first; i < out.size(); ++i)
        out[i].b = out[i].a + (out[i].b - out[i].a) * 0.5f;
}

// Scale the colour of the lines in [first, out.size()). Used for distance fade
// and to sink secondary geometry (reference bounds, swept volumes) behind the
// colliders that are the actual subject.
void tintRange(std::vector<Physics::DebugLine>& out, size_t first, float scale)
{
    if (scale >= 0.999f) return;
    for (size_t i = first; i < out.size(); ++i)
        out[i].colour *= scale;
}

// 1 at or inside fadeStart, falling to 0 at range. Returns 0 when the subject
// is beyond range, which callers use as "skip it entirely".
float distanceFade(const Physics::DebugDrawOptions& o, const glm::vec3& at)
{
    if (o.range <= 0.0f) return 1.0f;
    const float d = glm::length(at - o.viewer);
    if (d >= o.range) return 0.0f;
    const float start = o.fadeStart > 0.0f ? o.fadeStart : o.range * (2.0f / 3.0f);
    if (d <= start || start >= o.range) return 1.0f;
    return 1.0f - (d - start) / (o.range - start);
}

} // namespace

void Physics::debugDraw(std::vector<DebugLine>& out, ColliderPalette palette,
                        CollisionMask include) const
{
    DebugDrawOptions o;
    o.palette = palette;
    o.include = include;
    debugDraw(out, o);
}

void Physics::debugDraw(std::vector<DebugLine>& out,
                        const DebugDrawOptions& o) const
{
    if (!mImpl->inited) return;
    const bool byShape = o.palette == ColliderPalette::ByShape;

    for (const auto& rec : mImpl->bodies) {
        if (!rec.alive) continue;
        if (!any(o.include & layerMask(rec.layer))) continue;
        JPH::BodyLockRead lock(mImpl->system.GetBodyLockInterface(), rec.id);
        if (!lock.Succeeded()) continue;
        const JPH::Body& body = lock.GetBody();
        const JPH::RMat44 com = body.GetCenterOfMassTransform();
        const JPH::RVec3 t = com.GetTranslation();
        const glm::vec3 at{float(t.GetX()), float(t.GetY()), float(t.GetZ())};
        const float fade = distanceFade(o, at);
        if (fade <= 0.0f) continue;

        const JPH::Shape* shape = body.GetShape();
        const bool sensor = body.IsSensor();
        if (sensor && !o.drawSensors) continue;

        const glm::vec3 col = byShape ? shapeColour(shape->GetSubType())
                                      : layerColour(mImpl->setup, rec.layer);
        // Center-of-mass transform is where the shape lives; draw the shape in
        // that frame so rotation and true size show correctly.
        const size_t first = out.size();
        pushShape(out, shape, com, col);
        if (sensor) dashRange(out, first);
        tintRange(out, first, fade);
    }

    // Kinematic characters expose radius/height, not a body shape: draw the
    // capsule they collide with, oriented upright at their world position.
    if (o.drawCharacters &&
        any(o.include & layerMask(mImpl->setup.characterLayer))) {
        const glm::vec3 charCol =
            byShape ? shapeColour(JPH::EShapeSubType::Capsule)
                    : layerColour(mImpl->setup, mImpl->setup.characterLayer);
        for (const auto& rec : mImpl->characters) {
            if (!rec.alive || !rec.ch) continue;
            const JPH::RVec3 p = rec.ch->GetPosition();
            const glm::vec3 at{float(p.GetX()), float(p.GetY()), float(p.GetZ())};
            const float fade = distanceFade(o, at);
            if (fade <= 0.0f) continue;
            const float centreY = rec.height * 0.5f;
            const float cylinderHalfHeight =
                std::max(0.0f, rec.height - 2.0f * rec.radius) * 0.5f;

            size_t first = out.size();
            pushCapsuleOrCylinder(
                out,
                JPH::RMat44::sTranslation(
                    JPH::RVec3(p.GetX(), p.GetY() + centreY, p.GetZ())),
                cylinderHalfHeight, rec.radius, true, charCol);
            tintRange(out, first, fade);

            // The volume this step is about to move through, at the position
            // the current velocity reaches. Dashed and dimmed so it reads as a
            // prediction rather than as a second character standing there.
            const JPH::Vec3 v = rec.ch->GetLinearVelocity();
            const glm::vec3 step{v.GetX() * o.sweepDt, v.GetY() * o.sweepDt,
                                 v.GetZ() * o.sweepDt};
            if (glm::length(step) > 1e-4f) {
                first = out.size();
                pushCapsuleOrCylinder(
                    out,
                    JPH::RMat44::sTranslation(JPH::RVec3(
                        p.GetX() + step.x, p.GetY() + centreY + step.y,
                        p.GetZ() + step.z)),
                    cylinderHalfHeight, rec.radius, true, charCol);
                dashRange(out, first);
                tintRange(out, first, fade * 0.55f);
            }
        }
    }

    // Render-mesh bounds beside the collider that is meant to stand in for it.
    // Two disagreeing boxes is what a wrong-sized collider looks like; one box
    // on its own always looks fine.
    if (o.references) {
        // Alarm red, deliberately not in the shape or layer palettes, so a
        // prop with no collision cannot be mistaken for one of the shape kinds.
        const glm::vec3 kNoCollider{1.0f, 0.15f, 0.15f};
        const glm::vec3 kBounds{0.85f, 0.85f, 0.85f};
        for (const DebugReference& ref : *o.references) {
            const float fade = distanceFade(o, ref.centre);
            if (fade <= 0.0f) continue;
            const bool missing =
                !ref.body.valid() ||
                ref.body.id >= uint32_t(mImpl->bodies.size()) ||
                !mImpl->bodies[ref.body.id].alive;
            if (!missing &&
                !any(o.include & layerMask(mImpl->bodies[ref.body.id].layer)))
                continue;
            const size_t first = out.size();
            pushOrientedBox(
                out,
                JPH::RMat44::sTranslation(
                    JPH::RVec3(ref.centre.x, ref.centre.y, ref.centre.z)),
                JPH::Vec3(ref.halfExtents.x, ref.halfExtents.y,
                          ref.halfExtents.z),
                missing ? kNoCollider : kBounds);
            if (!missing) {
                // Dim and dashed: the collider is the subject, the mesh bounds
                // are the ruler held up next to it.
                dashRange(out, first);
                tintRange(out, first, fade * 0.5f);
            } else {
                tintRange(out, first, fade);
            }
        }
    }
}

} // namespace eng
