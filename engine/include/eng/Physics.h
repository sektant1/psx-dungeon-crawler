#pragma once
#include <eng/Handles.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace eng {

// Collision layers are plain indices into a table the *application* defines:
// the engine ships the mechanism (a layer table, a symmetric collision matrix,
// broad-phase bucketing) and no taxonomy. What "player" or "projectile" means
// is a game decision, and a game that grows a new one must not have to edit
// the physics engine to get it.
using CollisionLayer = uint8_t;
inline constexpr int kMaxCollisionLayers = 16;

// Queries take a mask, not a layer, so "everything solid" is one cast instead
// of one cast per layer with the results merged by hand. It is a distinct type
// rather than an alias for the layer's integer: a layer index silently used as
// a mask selects the wrong set (layer 0 becomes the empty mask), and that reads
// as "the ray hit nothing" rather than as a mistake.
enum class CollisionMask : uint32_t {};

constexpr CollisionMask layerMask(CollisionLayer layer)
{
    return layer < kMaxCollisionLayers
               ? CollisionMask{uint32_t{1} << layer}
               : CollisionMask{0};
}
constexpr CollisionMask operator|(CollisionMask a, CollisionMask b)
{
    return CollisionMask{uint32_t(a) | uint32_t(b)};
}
constexpr CollisionMask operator&(CollisionMask a, CollisionMask b)
{
    return CollisionMask{uint32_t(a) & uint32_t(b)};
}
constexpr CollisionMask operator~(CollisionMask m)
{
    return CollisionMask{~uint32_t(m)};
}
constexpr CollisionMask& operator|=(CollisionMask& a, CollisionMask b)
{
    return a = a | b;
}
constexpr CollisionMask& operator&=(CollisionMask& a, CollisionMask b)
{
    return a = a & b;
}
constexpr bool any(CollisionMask m) { return uint32_t(m) != 0; }

inline constexpr CollisionMask kAllLayers = CollisionMask{~uint32_t{0}};
inline constexpr CollisionMask kNoLayers = CollisionMask{0};

struct CollisionLayerDesc {
    std::string name = "layer"; // tooling and logs only
    // Broad-phase bucket. Level geometry is non-moving; anything simulated or
    // teleported around every frame is moving. Getting this wrong costs
    // performance, not correctness.
    bool moving = true;
    glm::vec3 debugColour{1.0f, 1.0f, 1.0f}; // ColliderPalette::ByLayer
};

// Everything the physics world needs to know about the application's layers.
// Passed once to Physics::init.
struct PhysicsSetup {
    // Index i describes layer i. Empty means the generic default below.
    std::vector<CollisionLayerDesc> layers;
    // collides[a] has bit b set when layers a and b interact. Symmetric;
    // maintain it through setPair.
    std::array<CollisionMask, kMaxCollisionLayers> collides{};
    // Layer that character controllers are created on.
    CollisionLayer characterLayer = 0;

    // Multi-threaded simulation. Off by default, and that default is a
    // gameplay decision rather than a performance oversight: Jolt with more
    // than one worker resolves contacts in whatever order the threads win the
    // race, so a stack of props settles differently every run and a boss-fight
    // retry is not the same fight. Single-worker simulation is reproducible
    // frame for frame, which is what retries, replays and any future netcode
    // need. Opt in only where a scene is heavy enough that the physics step
    // actually shows up in the frame and reproducibility does not matter.
    bool multithreaded = false;
    // Workers to use when `multithreaded`. 0 means hardware_concurrency() - 1.
    // Ignored entirely when `multithreaded` is false.
    int workerThreads = 0;

    // World gravity, in m/s^2. Deliberately stronger than Earth: a shorter,
    // snappier fall arc is what a fast first-person game wants, and the value
    // is pure feel, so the application supplies it.
    glm::vec3 gravity{0.0f, -18.0f, 0.0f};

    // How hard a character controller shoves a dynamic body it walks into,
    // as a multiple of the character's speed along the contact normal. 0
    // disables the shove and props become immovable walls to the player.
    float characterPushImpulse = 2.0f;

    void setPair(CollisionLayer a, CollisionLayer b, bool collides_)
    {
        if (a >= kMaxCollisionLayers || b >= kMaxCollisionLayers)
            return;
        auto apply = [&](CollisionLayer x, CollisionLayer y) {
            if (collides_) collides[x] |= layerMask(y);
            else           collides[x] &= ~layerMask(y);
        };
        apply(a, b);
        apply(b, a);
    }

    // All layers collide with all others, then subtract. Convenient because
    // most tables are "everything hits everything except these few pairs".
    void collideAll()
    {
        for (auto& m : collides) m = kAllLayers;
    }

    // Two generic layers -- 0 static, 1 dynamic -- for callers with no layer
    // scheme of their own (tests, tools, the sample apps).
    static PhysicsSetup generic();
};

enum class ShapeKind { Box, Sphere, Capsule, Cylinder };

struct BodyDesc {
    ShapeKind kind = ShapeKind::Box;
    glm::vec3 halfExtents{0.5f};
    float radius = 0.5f;
    float halfHeight = 0.5f;
    glm::vec3 position{0.0f};
    glm::quat orientation{1,0,0,0};
    CollisionLayer layer = 0;
    bool dynamic = true;
    bool sensor = false;
    bool continuousCast = false;
    float mass = 1.0f;
    // Per-body gravity multiplier. Magical projectiles use 0; ordinary dynamic
    // props keep the default world gravity.
    float gravityFactor = 1.0f;
    float friction = 0.5f;
    // No bounce by default: a grounded dungeon wants props to thud and settle
    // and arrows to stick, not rebound. Jolt combines the two bodies'
    // restitution, so a non-zero default made even restitution-0 arrows bounce
    // off walls. Opt in explicitly for anything that should be springy.
    float restitution = 0.0f;
};

struct CharacterDesc {
    float radius = 0.30f;
    float height = 1.7f;
    glm::vec3 position{0.0f};
    float maxSlopeDeg = 46.0f;
    float stepHeight = 0.4f;
    float mass = 80.0f;
};

enum class GroundState { OnGround, OnSteepSlope, InAir };
struct CharacterState {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 groundNormal{0,1,0};
    GroundState ground = GroundState::InAir;
    bool grounded() const { return ground == GroundState::OnGround; }
};

struct RayHit  { BodyHandle body; glm::vec3 point{0}; glm::vec3 normal{0}; float fraction = 1.0f; };
struct ShapeHit { BodyHandle body; glm::vec3 point{0}; glm::vec3 normal{0}; float penetration = 0.0f; };
struct HitEvent { BodyHandle self; BodyHandle other; glm::vec3 point{0}; glm::vec3 normal{0}; float impulse = 0.0f; };

class EngContactListener;

// A contact subscription. Namespace scope so a caller can store one without
// naming Physics.
using ContactToken = uint32_t;

class Physics {
public:
    Physics();
    ~Physics();
    void init(const PhysicsSetup& setup = PhysicsSetup::generic());
    void shutdown();
    void update(float fixedDt, int collisionSteps = 1);
    float interpolationAlpha() const;
    void setInterpolationAlpha(float alpha);

    BodyHandle createBody(const BodyDesc&);
    BodyHandle createMeshBody(const std::vector<glm::vec3>& verts,
                              const std::vector<uint32_t>& indices,
                              glm::vec3 pos, glm::quat rot, CollisionLayer);
    void removeBody(BodyHandle);
    void setBodyTransform(BodyHandle, glm::vec3, glm::quat);
    void getRenderTransform(BodyHandle, glm::vec3& pos, glm::quat& rot) const;
    void applyImpulse(BodyHandle, glm::vec3 impulse, glm::vec3 atPoint);
    void setBodyKinematic(BodyHandle, bool);
    int  activeBodyCount() const;
    int  bodyCount() const;       // total live bodies (dynamic + static) not yet removed
    void  setGravity(float y);      // sets world gravity to (0, y, 0)
    float gravityY() const;         // current gravity y component

    CharacterHandle createCharacter(const CharacterDesc&);
    void removeCharacter(CharacterHandle);
    void characterSetVelocity(CharacterHandle, glm::vec3 velocity);
    // `mask` narrows the character controller's own sweep on top of the world
    // collision matrix: the character collides with a layer only if the matrix
    // allows it *and* the mask names it. The default is every layer, i.e. the
    // matrix alone, so callers that do not care are unaffected. Clearing a bit
    // is how gameplay makes a layer pass-through for this character only.
    void characterUpdate(CharacterHandle, float dt,
                         CollisionMask mask = kAllLayers);
    CharacterState characterState(CharacterHandle) const;
    void characterSetShape(CharacterHandle, float radius, float height);

    bool rayCast(glm::vec3 from, glm::vec3 dir, float dist, RayHit&,
                 CollisionMask mask = kAllLayers) const;
    int  shapeCast(const BodyDesc& shape, glm::vec3 from, glm::vec3 to,
                   std::vector<ShapeHit>&, CollisionMask mask = kAllLayers) const;
    int  overlap(const BodyDesc& shape, glm::vec3 at, std::vector<ShapeHit>&,
                 CollisionMask mask = kAllLayers) const;

    using HitCallback = std::function<void(const HitEvent&)>;

    // Every subscriber sees every contact, in subscription order.
    //
    // Multi-subscriber rather than one slot because the slot had two claimants
    // the moment anything besides combat wanted contacts: the game's combat
    // system and the script host's trigger bridge. A setter would have let
    // whichever ran second silently unregister the first, and nothing would
    // have reported it.
    //
    // Returns a non-zero token; zero is never issued, so it is a usable "none".
    ContactToken addContactCallback(HitCallback);
    void removeContactCallback(ContactToken);

    // Debug visualisation: fills `out` with the wireframe of every live body's
    // actual collision shape (oriented box/sphere/capsule/cylinder; mesh/hull
    // fall back to their oriented local bounds) plus every live character
    // capsule. Call once per frame when the overlay is on.
    struct DebugLine { glm::vec3 a{0}; glm::vec3 b{0}; glm::vec3 colour{1,1,1}; };
    // How the emitted line colours are chosen.
    enum class ColliderPalette {
        ByShape, // box/sphere/capsule/cylinder/mesh each get a distinct colour
        ByLayer, // each layer draws in its CollisionLayerDesc::debugColour
    };
    // `include` filters by layer: dropping the level-geometry layer leaves
    // just the props and dynamic bodies, which is far less cluttered inside a
    // built dungeon.
    void debugDraw(std::vector<DebugLine>& out,
                   ColliderPalette palette = ColliderPalette::ByShape,
                   CollisionMask include = kAllLayers) const;

    // A render mesh's world bounds, paired with the body that is supposed to
    // represent it. Drawn alongside the collider so the two can be compared by
    // eye: that is the only way to see a collider that is the wrong *size*,
    // because a collider on its own always looks plausible.
    //
    // An invalid `body` means this mesh has no collider at all. That case is
    // drawn in alarm red at full brightness rather than being silently absent,
    // because "nothing was drawn here" and "this prop has no collision" look
    // identical otherwise, and they are the two failures hardest to tell apart
    // when walking a dungeon looking for holes.
    //
    // Handle-based on purpose: the engine has no idea what a render mesh is,
    // so the caller -- which owns both the node and the body -- supplies the
    // pairing rather than the engine reaching across into the renderer.
    struct DebugReference {
        BodyHandle body{};
        glm::vec3 centre{0.0f};      // world-space centre of the mesh bounds
        glm::vec3 halfExtents{0.0f}; // world-space half extents
    };

    struct DebugDrawOptions {
        ColliderPalette palette = ColliderPalette::ByShape;
        CollisionMask include = kAllLayers;

        // Range limit and distance fade. A whole dungeon of colliders drawn at
        // full brightness is a wall of lines you cannot read anything out of;
        // limiting to what is near the player is what makes it a diagnostic
        // rather than a light show. `range` <= 0 disables both.
        glm::vec3 viewer{0.0f};
        float range = 0.0f;
        // Distance at which fading begins. <= 0 means fade over the outer
        // third of `range`.
        float fadeStart = 0.0f;

        // Character controllers: the capsule they collide with, plus the
        // volume they are about to sweep through this step, drawn dashed at
        // the position their current velocity takes them to. Seeing the sweep
        // separately is what shows a tunnelling or step-up problem, which the
        // resting capsule alone never does.
        bool drawCharacters = true;
        float sweepDt = 1.0f / 60.0f;

        // Sensors are drawn dashed, solids continuous. A trigger volume and a
        // wall look the same as wireframes and behave nothing alike, and the
        // usual bug is one being the other by mistake.
        bool drawSensors = true;

        // Optional; not retained past the call.
        const std::vector<DebugReference>* references = nullptr;
    };
    void debugDraw(std::vector<DebugLine>& out,
                   const DebugDrawOptions& options) const;

private:
    friend class EngContactListener;
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace eng
