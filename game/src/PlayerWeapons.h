#pragma once

#include "SpriteViewmodel.h"
#include "combat/FeelComponents.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace game {

enum class WeaponTrigger { Press, Automatic };

enum class WeaponPrimitive {
    Box,
    BeveledBox,
    Sphere,
    Capsule,
    Cylinder,
    Cone,
    Disc,
};

// How a weapon reaches what it is pointed at.
//
// This is the weapon system's ONLY branch point, and it is deliberately three
// cases rather than one per weapon: a weapon definition selects a delivery, and
// every other field it carries -- interval, damage payload, ARC cost, recoil,
// viewmodel, switch time -- means the same thing whichever one it picked.
// Adding a weapon stays a TOML table; adding a fourth *delivery* is the only
// thing that is C++, and it should stay that way.
//
//   projectile  a body is spawned and travels        (ProjectileSystem)
//   melee       a shape is swept in front of the eye (WeaponDeliverySystem)
//   hitscan     a ray resolves instantly             (WeaponDeliverySystem)
//
// There is deliberately no `spell` mode. A spell is not a fourth way to reach a
// target -- it is a projectile or a hitscan that costs ARC and wears a school's
// glow, and `arc_cost` plus `[...viewmodel] glow_school` already say that on
// every weapon. A mode for it would have been two code paths differing only in
// vocabulary, which is how a fireball and a bolt acquire separate bugs.
// See docs/fps-gameplay.md.
enum class WeaponFireMode { Projectile, Melee, Hitscan };

const char* weaponFireModeName(WeaponFireMode mode);
std::optional<WeaponFireMode> weaponFireModeFromName(const std::string& name);

// `[player_weapon.<id>.melee]` -- the swept-shape delivery.
//
// A swing is a *window*, not an instant: windup, then an active period during
// which the shape is swept every fixed step and each body is hit at most once.
// That is what lets the viewmodel's fire animation and the hit land together
// instead of the damage arriving on the frame the button went down.
struct WeaponMeleeDef {
    float reach = 2.2f;   // metres ahead of the eye the sweep ends
    float radius = 0.55f; // radius of the swept sphere
    float windup = 0.06f; // seconds before the window opens
    float active = 0.10f; // seconds the window stays open
    float impulse = 6.0f; // knockback along the swing
    int maxTargets = 4;   // bodies one swing may connect with
    std::string impactEffect;
    std::string impactSound;
};

// `[player_weapon.<id>.hitscan]` -- the instant-ray delivery.
struct WeaponHitscanDef {
    float range = 60.0f;
    float impulse = 2.0f;
    // The beam drawn along the ray. An empty material draws nothing, which is a
    // legitimate choice for an instant weapon whose feedback is the muzzle
    // flash and the impact rather than a visible trace.
    std::string beamMaterial;
    float beamWidth = 0.05f;
    float beamSeconds = 0.06f;
    std::string impactEffect;
    std::string impactSound;
};

bool validWeaponMeleeDef(const WeaponMeleeDef& melee);
bool validWeaponHitscanDef(const WeaponHitscanDef& hitscan);

struct PlayerProjectileDef {
    WeaponPrimitive primitive = WeaponPrimitive::Sphere;
    glm::vec3 visualScale{0.08f};
    std::string material = "Game/Prototype/ProjectileVesper";
    std::string trailEffect;
    std::string impactEffect;
    std::string impactSound;
    float speed = 50.0f;
    float lifetime = 1.5f;
    float radius = 0.05f;
    float mass = 0.1f;
    float gravityFactor = 0.0f;
    float aimRange = 80.0f;
};

struct WeaponViewmodelPart {
    WeaponPrimitive primitive = WeaponPrimitive::Box;
    glm::vec3 position{0.0f};
    glm::vec3 rotationDegrees{0.0f};
    glm::vec3 scale{1.0f};
    std::string material = "Game/ViewModelVesper";
    bool enchanted = false;
};

// Which presentation a weapon wears in the player's hands.
//
// Model is the default and covers both mesh and primitive weapons -- they share
// a socket on the skinned hand rig and differ only in what hangs there. Sprite
// is the flat layered presentation, which has no skeleton and therefore no
// socket, so it is the branch that genuinely differs.
enum class ViewmodelPresentation { Model, Sprite };

const char* viewmodelPresentationName(ViewmodelPresentation presentation);
std::optional<ViewmodelPresentation>
viewmodelPresentationFromName(const std::string& name);

struct WeaponViewmodelDef {
    // `presentation = "sprite"` in TOML. When it is Sprite, `model` and the
    // `parts` below are ignored and `spriteLayers` is what gets built.
    ViewmodelPresentation presentation = ViewmodelPresentation::Model;
    // Composited over the hands' own layers (viewmodel_hands.toml), sorted by
    // distance. Empty on a model weapon.
    std::vector<ViewmodelSpriteLayer> spriteLayers;
    // Where a sprite weapon's shots leave from, in camera space. A sprite has
    // no skeleton, so this replaces muzzleSocket/handsMuzzleJoint for that
    // presentation -- `aim != muzzle` is unaffected, aim is still the camera
    // ray. Ignored by model weapons.
    glm::vec3 spriteMuzzle{0.0f, -0.10f, -0.60f};

    glm::vec3 position{0.24f, -0.24f, -0.55f};
    glm::vec3 rotationDegrees{0.0f};
    std::vector<WeaponViewmodelPart> parts;
    std::string glowSchool = "arcane";

    // --- attachment: which socket on the hand rig this weapon hangs off, and
    // what hangs there. See game/src/ViewmodelSocket.h and WeaponViewmodel.h.
    //
    // `model` set  -> that mesh is loaded and attached.
    // `model` empty -> the `parts` primitives above are generated instead.
    // That is the whole presentation seam: adding a weapon with a real model is
    // a path in a TOML, and the placeholder stays authorable until one exists.
    std::string socket = "right_hand";
    std::string model;
    std::string modelMaterial = "Game/ViewModelVesper";
    // Socket space, applied on top of the socket's own offset. This is the pair
    // of numbers a designer drags with the gizmo to seat a weapon in the hand.
    glm::vec3 attachOffset{0.0f};
    glm::vec3 attachRotationDegrees{0.0f};
    float attachScale = 1.0f;
    // Where its shots leave from, preferred over hands_muzzle_joint when set:
    // a socket is named once in viewmodel_hands.toml and reused, a joint name
    // is a Blender detail every weapon otherwise has to repeat.
    std::string muzzleSocket;

    std::string handsIdleAnimation = "relax";
    std::string handsDrawAnimation = "relax";
    std::string handsFireAnimation = "grab.R";
    std::string handsMuzzleJoint = "f_index.03.R";
    glm::vec3 handsMuzzleOffset{0.0f, 0.025f, 0.0f};
    // Per-weapon nudge on top of the shared rig socket (see ViewmodelRig.h).
    // Deliberately small and defaulted to identity: the framing the player
    // learns belongs to [player_viewmodel], a weapon only leans out of it.
    glm::vec3 handsOffset{0.0f};
    glm::vec3 handsRotationDegrees{0.0f};
    float handsScale = 1.0f;
    float glowStrength = 0.6f;

    float fireDuration = 0.16f;
    float recoilDistance = 0.06f;
    float recoilPitchDegrees = 6.0f;
    float recoilYawDegrees = 0.0f;
    float recoilRecovery = 20.0f;
    float movementBob = 0.012f;
    float movementBobSpeed = 7.5f;
    float idleSway = 0.004f;
    float lookSway = 0.0012f;
};

struct PlayerWeaponDef {
    std::string id;
    std::string displayName;
    std::string discipline;
    std::string payloadId;
    WeaponTrigger trigger = WeaponTrigger::Press;
    // Which delivery fires. Projectile is the default because it is what every
    // shipped weapon uses and what an omitted `fire_mode` key should mean.
    WeaponFireMode fireMode = WeaponFireMode::Projectile;
    float fireInterval = 0.25f;
    float arcCost = 5.0f;
    int projectileCount = 1;
    float spreadDegrees = 0.0f;
    float switchTime = 0.18f;
    // Camera-local right/up/forward offset. Aim remains camera-derived.
    glm::vec3 muzzleOffset{0.18f, -0.16f, 0.40f};
    std::string muzzleEffect;
    std::string fireSound;
    // One block per delivery, and only the selected one is validated or read.
    // They are plain members rather than a variant because a weapon being
    // retuned from a bolt into a beam should not lose the numbers it had --
    // an author flips `fire_mode` back and the old block is still there.
    PlayerProjectileDef projectile;
    WeaponMeleeDef melee;
    WeaponHitscanDef hitscan;
    WeaponViewmodelDef viewmodel;
};

// The impact cue for whichever delivery this weapon uses. Callers that want to
// play "what this weapon sounds like landing" must not reach into `.projectile`
// directly: a melee weapon has no projectile block worth reading, and the one
// call site that did read it silently played nothing for two of three modes.
const std::string& weaponImpactSound(const PlayerWeaponDef& weapon);

bool validPlayerWeaponDefinition(const PlayerWeaponDef& definition);

class PlayerWeaponLibrary {
public:
    PlayerWeaponLibrary();

    bool load(const std::string& tomlPath);
    bool loadFromString(const char* tomlSource);

    const std::vector<PlayerWeaponDef>& defs() const { return mDefs; }
    std::vector<PlayerWeaponDef>& defs() { return mDefs; }
    const PlayerWeaponDef* find(const std::string& id) const;

private:
    std::vector<PlayerWeaponDef> mDefs;
};

struct WeaponCommand {
    bool enabled = false;
    bool fireHeld = false;
    bool firePressed = false;
    bool swapPressed = false;
    int selectSlot = -1;
};

// Fixed-step mutable state for one player loadout. Definitions remain immutable;
// cooldown, switch lock, buffered input, and shared ARC spending live here.
class WeaponController {
public:
    void bind(const std::vector<PlayerWeaponDef>* definitions);
    void sample(const WeaponCommand& command);
    std::optional<std::size_t> fixedUpdate(float dt, Mana& arc,
                                           bool canFire = true);

    std::size_t selectedIndex() const { return mSelected; }
    const PlayerWeaponDef* selected() const;
    bool consumeSelectionChanged();
    void resetRuntime();

private:
    const std::vector<PlayerWeaponDef>* mDefinitions = nullptr;
    std::vector<float> mCooldowns;
    std::size_t mSelected = 0;
    float mSwitchRemaining = 0.0f;
    bool mFireHeld = false;
    bool mFirePressed = false;
    bool mSwapPressed = false;
    int mSelectSlot = -1;
    bool mSelectionChanged = false;
};

// Deterministic horizontal fan around a camera-derived aim direction.
std::vector<glm::vec3> projectileDirections(glm::vec3 direction, int count,
                                            float spreadDegrees);

} // namespace game
