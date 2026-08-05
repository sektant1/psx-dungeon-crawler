#pragma once
#include <eng/ecs/components/AudioEmitter.h>
#include <eng/ecs/components/AudioListener.h>
#include <eng/ecs/components/FirstPersonController.h>
#include <eng/ecs/components/ParticleEmitter.h>
#include <eng/ecs/components/PortalParams.h>
#include <eng/ecs/components/PrimitiveMesh.h>

#include "ViewmodelPreview.h"
#include "ViewmodelRig.h"
#include "audio/ActorSounds.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace game::content {

// A name the author gives an entity, unique within the document and stable for
// the life of the scene.
//
// This is the reason a SceneDocument exists instead of the editor working
// straight on an entt::registry: entity handles are recycled, so a delete
// followed by an undo hands back a *different* handle and every history entry
// that referenced the old one silently rots. Undo commands and cross-entity
// references address entities by AuthorId and resolve them at apply time, so
// the history stays valid across any sequence of edits.
using AuthorId = std::string;

// Authored transform, kept in the units the .scn file uses: degrees, not a
// quaternion. Round-tripping euler->quat->euler is lossy and would make a save
// after a no-op edit produce a diff.
struct XformAuthor {
    glm::vec3 position{0.0f};
    glm::vec3 rotationDegrees{0.0f};
    glm::vec3 scale{1.0f};
};

// Canonical authored rotation convention: yaw (Y), then pitch (X), then roll
// (Z). Cooker and editor must use these helpers together or compound rotations
// visibly change when switching between gizmo, preview and runtime.
glm::quat authorOrientation(const glm::vec3& rotationDegrees);
glm::vec3 authorRotationDegrees(const glm::quat& orientation);
glm::mat4 authorTransformMatrix(const XformAuthor& transform);

// A transform resolved against the world, as position/orientation/scale rather
// than a matrix: that is what the ECS transform holds, and decomposing a matrix
// back into those three is lossy in exactly the shear cases a level never has.
struct WorldTransform {
    glm::vec3 position{0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

// Composes a child onto its parent, TRS-style: rotate and scale the child's
// offset by the parent, multiply the rotations, multiply the scales.
//
// Component-wise scale composition is only exact while the parent's rotation
// keeps the child's axes aligned -- the same caveat every engine with a
// non-uniform scale in its hierarchy carries. Levels are authored on quarter
// turns, where it holds.
WorldTransform composeTransform(const WorldTransform& parent,
                                const XformAuthor& local);

// The inverse: what a child's authored transform must be for it to end up at
// `world` while hanging under `parent`.
//
// This is what keeps an entity where it is drawn when it changes parents, and
// what turns a gizmo drag -- which is always in world space, because that is
// where the mouse is -- back into the local numbers the file stores.
XformAuthor localFromWorld(const WorldTransform& parent,
                           const WorldTransform& world);

// Where a grid-constrained piece sits. Derived quantities (the world transform)
// come from GridMath; this is the authored intent, and it is what makes a wall
// still be "the north wall of cell (3,4)" after somebody nudges the geometry.
struct CellPlacement {
    enum class Edge { None, North, East, South, West };

    int col = 0;
    int row = 0;
    Edge edge = Edge::None; // Wall/Opening sit on an edge; Floor/Fill do not
    int span = 1;
    int yawQuarters = 0; // 0..3, rotation in 90-degree steps
    float level = 0.0f;  // work-plane height in metres
};

struct ColliderAuthor {
    glm::vec3 halfExtents{0.5f};
    glm::vec3 offset{0.0f};
};

// How a light modulates over time. Cooks to eng::ecs::LightAnimation, whose
// header is where the modes are defined; Steady means the light is not animated
// and is what a light with no `animation` block gets.
struct LightAnimAuthor {
    enum class Mode { Steady, Flicker, Pulse };
    Mode mode = Mode::Flicker;
    float speed = 7.0f;
    float amount = 0.3f;
    // Per-instance offset. Two torches on one wall with identical numbers and
    // no phase gutter in lockstep, which reads as a switch rather than fire.
    float phase = 0.0f;
};

struct LightAuthor {
    enum class Type { Directional, Point };
    Type type = Type::Point;
    glm::vec3 colour{1.0f};
    float range = 8.0f;
    bool castShadows = false;
    // Absent means a steady light, which is every light authored before this
    // existed.
    std::optional<LightAnimAuthor> animation;
};

struct TriggerAuthor {
    glm::vec3 size{1.0f}; // half-extents of the box volume
    std::string event;
};

// A point of view placed in the level. Cooks to eng::ecs::Camera, whose header
// is where the rules live; this is only what the file carries.
//
// The entity's transform is the shot -- position and facing -- so a camera
// parented to something that moves is a moving shot, and the composition rules
// are the ones every other entity already follows.
struct CameraAuthor {
    float fovDegrees = 60.0f; // vertical; the game's own framing is 70
    float nearClip = 0.05f;
    float farClip = 200.0f;
    int priority = 0;   // highest active camera wins
    bool active = true; // a parked alternate framing, kept but not used
};

// Travel around a point. Cooks to eng::ecs::Orbit, whose header is where the
// rules live; this is only what the file carries.
//
// `Spin` turns a thing where it stands; this moves it along a ring, and it
// needs no pivot entity to do it. The pivot rig is still what a *group* uses --
// a parent is how several things share one motion -- but a single orbiting
// camera, moon or drone is one component on one entity.
struct OrbitAuthor {
    enum class Facing { Free, Centre, Travel };
    glm::vec3 centre{0.0f}; // in the entity's own frame: its parent's, or world
    glm::vec3 axis{0.0f, 1.0f, 0.0f};
    float radius = 5.0f;
    float degreesPerSecond = 30.0f;
    float phaseDegrees = 0.0f;
    float height = 0.0f;
    Facing facing = Facing::Free;
};

// Constant rotation, in degrees per second about a local axis. Cooks to
// eng::ecs::Spin.
//
// Authored because the things it is for -- a turning pickup, a slowly rotating
// hazard, a pivot an orbiting camera hangs from -- are level decisions, and
// every one of them would otherwise be a line of C++ that has to find the
// entity again each frame.
// Per-entity shader uniforms, authored. Cooks to eng::ecs::ShaderParams, whose
// header says what each field drives; the defaults here are that struct's, so
// an entity that carries the component but sets nothing renders exactly as it
// did without it.
struct ShaderAuthor {
    glm::vec3 tint{1.0f, 1.0f, 1.0f};
    float opacity = 1.0f;
    glm::vec3 rimColour{0.55f, 0.75f, 1.0f};
    float rimStrength = 0.0f;
    float rimPower = 3.0f;
    float alphaScissor = 0.0f;
};

// The portal shader's knobs, authored. Field names are the uniform names, the
// same contract eng::ecs::PortalParams states -- so this is a mirror of that
// struct rather than a translation of it, and the cook is a field-by-field copy
// with nothing to get wrong in between.
using PortalAuthor = eng::ecs::PortalParams;

// A particle effect playing from this entity. The same mirror-not-translate
// trick: the authored type IS the component, so the cook is a copy and the
// accepted .scn keys are the component's own fields.
//
// This is what makes "give that torch smoke" inspector work. It used to be the
// Particles dock or nothing, and the dock spawns an effect into the *level* --
// a runtime act nothing saves, so an authored scene could not carry one.
using ParticleAuthor = eng::ecs::ParticleEmitter;

// Sound attached to an entity, and a listener candidate attached to a camera or
// empty rig node. Authored types are runtime components directly, so cooking
// cannot lose a setting in translation.
using AudioEmitterAuthor = eng::ecs::AudioEmitter;
using AudioListenerAuthor = eng::ecs::AudioListener;

// What this entity is to the game -- player, NPC or enemy -- and what each of
// its actions sounds like. Same mirror-not-translate rule as the two above: the
// authored types are the runtime ones, so the cook is a copy.
//
// An actor kind is what gates the sound table: a wall performs no actions, so
// asking an author to fill in "what does this crate sound like when it dies" is
// a panel full of fields nobody can answer. Placement components imply a kind
// (a player spawn is a player, an enemy spawn is an enemy), so this component
// only has to be added by hand for the case the format could not express at
// all: an NPC.
using ActorKindAuthor = game::ActorKind;
using ActorSoundsAuthor = game::ActorSoundSet;

// The player, authored on the camera that is their eye. Same mirror-not-
// translate rule again: these ARE the runtime components, so a cook is a copy
// and the .scn keys are the components' own field names.
//
// Together they are the answer to "how does this level play": the controller
// is how the body moves and what the lens does, the rig is where the hands sit
// in front of it. A scene that carries neither runs on the game's config
// defaults, which is every level authored before they existed.
using FirstPersonAuthor = eng::ecs::FirstPersonController;
using ViewmodelRigAuthor = game::ViewmodelRig;
// Editor-only: which weapon the viewport shows in the hands. Dropped at cook
// (see game/src/ViewmodelPreview.h) -- it is how a placement is judged, not
// something the level carries.
using ViewmodelPreviewAuthor = game::ViewmodelPreview;

// One authored value on a script instance. Mirrors eng::ecs::ScriptProp; kept
// as its own type because the editor's author structs are the document's
// vocabulary, and the cooker is what translates them into components.
struct ScriptPropAuthor {
    enum class Type { Bool, Number, String, Vec3, Entity };
    std::string key;
    Type type = Type::Number;
    bool boolValue = false;
    float numberValue = 0.0f;
    glm::vec3 vecValue{0.0f};
    // String's value, and also Entity's target entity name.
    std::string stringValue;
};

// One script attached to an entity. A list of these is the only component the
// document carries as a vector rather than an optional: an entity may run
// several scripts, and their order is the order they tick in.
struct ScriptAuthor {
    std::string path; // logical asset path, "scripts/door.lua"
    std::vector<ScriptPropAuthor> props;
    bool enabled = true;
};

struct SpinAuthor {
    glm::vec3 axis{0.0f, 1.0f, 0.0f};
    float degreesPerSecond = 45.0f;
};

// A mesh file named directly, rather than through a kit prefab.
//
// `prefab` covers the modular kit -- a wall is "kit.wall" and everything about
// it (its mesh, its material, its socket, the grid it snaps to) comes from
// kit.toml. That is right for a kit and wrong for everything else: the 200
// meshes under assets/meshes are not all kit pieces, and putting one in a level
// meant adding a kit.toml entry for a thing with no socket and no span, or not
// putting it in a level at all.
//
// The path is pack-relative ("meshes/props/Chair.obj"), which is exactly what
// eng::assets::resolve takes and what MeshSource carries, so the cook is a copy
// and the map stays portable.
struct MeshAuthor {
    std::string path;
    // Multiplies the entity's transform scale, the way KitPiece::importScale
    // does for a prefab. Kept separate from the transform because it is a fact
    // about the *file* -- what units it was authored in -- and an author who
    // fixes it once should not have to redo it on every instance.
    float importScale = 1.0f;
};

// A mesh the engine generates. Mirror-not-translate again: the authored type IS
// the runtime component, so the cook is a field-by-field copy with nothing to
// get wrong in between, and the accepted .scn keys are the component's own
// field names.
using PrimitiveAuthor = eng::ecs::PrimitiveMesh;

struct Entity {
    AuthorId id;
    std::string name;   // display name; defaults to id
    // Whose frame `transform` is expressed in. Empty means the world.
    //
    // A level is not a flat list: a torch belongs to the wall it hangs on, a
    // patrol route belongs to the room it guards, and a chandelier plus its
    // four candles is one thing an author moves. Without this the only way to
    // move a composed object was to select its parts and hope none was missed.
    //
    // The runtime map stays flat: the cooker bakes the chain into a world
    // transform, so nothing downstream of the editor knows hierarchies exist.
    AuthorId parent;
    std::string prefab; // "kit.wall", or empty for a marker/light/trigger
    // The two other ways to be a mesh. Exactly one of the three should be set:
    // a prefab is a kit piece, `mesh` is any file, `primitive` is generated.
    // The cooker resolves them in that order, so an entity that somehow carries
    // more than one draws the most specific thing rather than two overlapping
    // meshes; the editor's component table stops that happening by hand.
    std::optional<MeshAuthor> mesh;
    std::optional<PrimitiveAuthor> primitive;
    // Overrides the kit piece's own material. Empty means "use the kit's",
    // which is what almost every piece should do -- an override is for the
    // one-off, not for a look a whole level shares (change kit.toml for that).
    //
    // For a `mesh` or a `primitive` there is no kit entry to fall back on, so
    // this is the material outright and an empty one leaves the entity wearing
    // the renderer's default. Same field either way: what a mesh wears is one
    // question, and a second field for it would be a second answer.
    std::string material;
    XformAuthor transform;
    bool castShadows = true;
    // A compound kit piece (kit.prop_boss_placeholder and its sword) normally
    // emits its attached parts at cook time: they render, but they are not in
    // the document, so the editor cannot select, move or re-material one. That
    // is the right default -- a scene should not carry four entities for every
    // torch bracket -- but it makes the parts unreachable.
    //
    // Set by "Unpack attachments", which writes the parts out as real child
    // entities. The cook then skips its own expansion for this entity, because
    // the document now holds what it would have generated.
    bool unpackedAttachments = false;

    std::optional<CellPlacement> cell;
    std::optional<ColliderAuthor> collider;
    std::optional<LightAuthor> light;
    std::optional<CameraAuthor> camera;
    std::optional<FirstPersonAuthor> firstPerson;
    std::optional<ViewmodelRigAuthor> viewmodelRig;
    std::optional<ViewmodelPreviewAuthor> viewmodelPreview;
    std::vector<ScriptAuthor> scripts;
    std::optional<SpinAuthor> spin;
    std::optional<OrbitAuthor> orbit;
    std::optional<ShaderAuthor> shader;
    std::optional<PortalAuthor> portal;
    std::optional<ParticleAuthor> particles;
    std::optional<AudioEmitterAuthor> audio;
    std::optional<AudioListenerAuthor> audioListener;
    std::optional<ActorKindAuthor> actor;
    std::optional<ActorSoundsAuthor> sounds;
    std::optional<float> exitYawDegrees;
    std::optional<std::string> marker;
    std::optional<std::string> enemySpawn; // enemy type id
    std::optional<std::string> pickup;     // pickup type id
    std::optional<TriggerAuthor> trigger;
    bool playerSpawn = false;
};

// What the game will treat this entity as, or nothing when it is scenery.
//
// Explicit first: an `actor` component says so outright, which is the only way
// to author an NPC. Otherwise the placement components already in the format
// imply it -- a player spawn is the player, an enemy spawn and an "enemy."
// marker are enemies -- so every level authored before actors existed
// classifies correctly without being touched, and the editor, the cooker and
// the validator cannot disagree about what is an actor.
std::optional<game::ActorKind> actorKindOf(const Entity& entity);
inline bool isActor(const Entity& entity)
{
    return actorKindOf(entity).has_value();
}

// The authored scene: what a .scn file says, in memory. Renderer-free and
// EnTT-free on purpose, so it loads in a headless test in milliseconds.
class SceneDocument
{
public:
    std::string id; // "scene.test.ritual_boss_showroom"

    // The look this level is lit and graded with: a table in palettes.toml
    // ("dungeon", "showroom"). Empty means the game's default.
    //
    // A level's palette is a level design decision -- a crypt and a cathedral
    // are not the same room with different props -- and it was the one such
    // decision the format could not carry. Every authored scene played under
    // whatever the runtime happened to apply, so the only way to see a level in
    // its own light was to edit the game.
    //
    // A name rather than the values: palettes.toml already holds forty tuned
    // fields per look, and a copy of them in every .scn is forty fields that
    // drift.
    std::string palette;

    std::vector<Entity> entities;

    Entity* find(std::string_view authorId);
    const Entity* find(std::string_view authorId) const;
    bool contains(std::string_view authorId) const;

    Entity& add(Entity entity); // asserts the id is free
    bool remove(std::string_view authorId);

    // Deterministic id allocation: "<stem>_0001", lowest free index. Never a
    // timestamp or a session counter -- two authors editing in parallel must
    // not be guaranteed a merge conflict.
    AuthorId allocateId(std::string_view stem) const;

    // --- hierarchy ----------------------------------------------------------
    // Where the entity actually is, with its parent chain applied. Entities
    // with no parent return their own transform unchanged, which is every
    // entity in every scene authored before hierarchies existed.
    //
    // A broken chain -- a missing parent, or a cycle -- resolves as if the
    // first unreachable link were the world. That keeps a damaged document
    // openable and editable: validate() reports the break, and refusing to draw
    // the scene would leave nothing to fix it with.
    WorldTransform worldTransform(std::string_view authorId) const;

    // The entities whose `parent` is `authorId`, in document order. Pass an
    // empty id for the roots.
    std::vector<const Entity*> childrenOf(std::string_view authorId) const;

    // Every descendant of `authorId`, depth first, excluding itself. Cycles are
    // walked once and dropped, so this terminates on any document.
    std::vector<AuthorId> descendantsOf(std::string_view authorId) const;

    // True when making `child` a child of `parent` would close a loop -- either
    // because they are the same entity or because `parent` is already below
    // `child`. The one check that stops a hierarchy from becoming unwalkable.
    bool wouldCycle(std::string_view child, std::string_view parent) const;

    // Bumped on every mutation the editor makes, so the preview bridge can skip
    // its diff when nothing changed. Not serialized.
    uint64_t revision = 0;
    void touch() { ++revision; }

private:
    // Rebuilt on demand rather than maintained: entities is small and the
    // editor mutates it in bursts.
    void rebuildIndex() const;
    mutable std::unordered_map<std::string, std::size_t> mIndex;
    mutable uint64_t mIndexRevision = ~uint64_t(0);
};

} // namespace game::content
