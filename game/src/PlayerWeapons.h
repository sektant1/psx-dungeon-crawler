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
    // Air resistance, as a fraction of speed shed per second. 0 is the magical
    // default -- a bolt that flies straight forever until its lifetime ends.
    //
    // A real bullet is the other case, and it is why this exists: with
    // `gravity_factor = 1` and a drag term, a round drops and slows over
    // distance, so range is a property of the weapon rather than a number in
    // its definition. See docs/fps-gameplay.md on why muzzle velocities are
    // scaled down rather than real.
    float drag = 0.0f;
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
    // THE BARREL TIP, in the weapon MODEL's own local space, metres.
    //
    // The muzzle socket above is a point on the HAND -- a fingertip, a wrist --
    // which is the right answer for a weapon that is a spell and the wrong one
    // for a rifle, where the bullet leaves a barrel 40 cm further forward. A
    // socket-derived muzzle on a gun puts the flash inside the player's fist
    // and starts the round behind its own foregrip.
    //
    // So a model weapon states where its own barrel ends. The point rides the
    // weapon node, which means it inherits the attach transform, the recoil and
    // every viewmodel layer for free -- a gun that kicks fires from where the
    // kicked barrel actually is.
    //
    // Non-zero switches the muzzle resolution to this; zero keeps the socket
    // behaviour, so nothing authored before this existed moves. Measured off
    // the imported mesh's bounds, which the prefab library records.
    glm::vec3 barrelOffset{0.0f};

    // Which hand rig this weapon is held in. Empty means "whatever the player
    // is wearing", which is what every weapon meant before the animated packs
    // arrived and is still right for a weapon that hangs on a socket.
    //
    // Naming one of the animated rigs (viewmodel_hands.toml, bundled_weapon)
    // makes equipping this weapon SWAP THE RIG: the gun is part of that mesh
    // and the clips were authored against it, so the hands and the weapon
    // arrive together or not at all.
    std::string handsRig;
    std::string handsIdleAnimation = "relax";
    std::string handsDrawAnimation = "relax";
    std::string handsFireAnimation = "grab.R";
    // Played while reloading. Empty falls back to idle, so a rig with no
    // reload clip still reloads -- it just does not show it.
    std::string handsReloadAnimation;
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

// Magazine ammunition and reloading.
//
// The loadout's original resource was ARC, a shared mana pool: every weapon
// drew from one number and nothing ever ran out mid-burst in a way you had to
// answer. That is a fantasy design and this is a gun game now, where the pacing
// mechanism IS the magazine -- when to break contact and reload is most of the
// moment-to-moment decision making.
//
// Both exist. ARC is still spent (`arc_cost`), so a weapon can cost both or
// neither, and a weapon with `magazine = 0` has no magazine at all and behaves
// exactly as it did before this struct existed. That is what keeps the shipped
// fantasy weapons working while the guns get magazines.
struct WeaponAmmoDef {
    // Rounds per magazine. 0 disables the whole system for this weapon.
    int magazine = 0;
    // Rounds carried beyond the loaded magazine, and the cap when picking up
    // more. -1 is infinite reserve, which is what a starter weapon wants and
    // what every weapon wants while a level's pickups are still being authored.
    int reserve = -1;
    int reserveMax = 240;
    // Rounds consumed per shot. A shotgun firing eight pellets spends one.
    int costPerShot = 1;
    // A reload from a partly-full magazine keeps the chambered round and is
    // quicker than one from empty, which is the distinction every shooter makes
    // and the reason there are two numbers rather than one.
    float reloadSeconds = 2.1f;
    float reloadEmptySeconds = 2.8f;
    // Reload one round at a time -- a pump shotgun -- rather than swapping a
    // magazine. Each round takes `reloadSeconds`, and the reload can be
    // interrupted by firing, which is the whole point of the mechanism.
    bool shellByShell = false;
    // Start a reload automatically when the magazine empties. On by default:
    // the alternative is a player holding the trigger on an empty gun.
    bool autoReload = true;
    // Named pool this weapon draws from. Two weapons sharing an id share their
    // reserve, which is how a pistol and an SMG in the same calibre behave.
    // Empty means the weapon owns its reserve alone.
    std::string ammoType;
    std::string reloadSound;
    std::string emptySound;
    // Clip on the hands rig played while reloading. Empty falls back to the
    // rig's own idle, so a rig with no reload animation still reloads.
    std::string reloadAnimation;
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
    WeaponAmmoDef ammo;

    bool usesMagazine() const { return ammo.magazine > 0; }
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
    bool reloadPressed = false;
    int selectSlot = -1;
};

// What the HUD and the viewmodel need to know about ammunition, in one place so
// neither has to reach into the controller's private state.
struct WeaponAmmoState {
    int magazine = 0;      // rounds loaded
    int magazineMax = 0;   // 0 means this weapon has no magazine
    int reserve = 0;       // -1 is infinite
    bool reloading = false;
    float reloadRemaining = 0.0f;
    float reloadTotal = 0.0f;

    bool usesMagazine() const { return magazineMax > 0; }
    bool empty() const { return usesMagazine() && magazine <= 0; }
    // 0..1 through the current reload, for a HUD ring or a bar. 0 when idle.
    float reloadProgress() const
    {
        return reloadTotal > 0.0f
                   ? 1.0f - (reloadRemaining / reloadTotal)
                   : 0.0f;
    }
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

    // --- ammunition ---------------------------------------------------------
    WeaponAmmoState ammoState(std::size_t index) const;
    WeaponAmmoState ammoState() const { return ammoState(mSelected); }
    // Begin a reload if one is possible: the weapon has a magazine, it is not
    // full, there is reserve to draw on, and no reload is already running.
    // False when none of that holds, which is also what makes it safe to call
    // from a key press every frame.
    bool beginReload(std::size_t index);
    bool beginReload() { return beginReload(mSelected); }
    // Give a weapon (or every weapon sharing an ammo type) more reserve, and
    // return how much was actually taken -- a pickup that would overflow
    // reserveMax should not vanish entirely.
    int addReserve(const std::string& ammoType, int rounds);
    // True on the frame a reload finished, so a caller can play a cue without
    // polling the timer. Consumed by reading.
    bool consumeReloadFinished();
    // True on the frame a shot was refused for want of ammunition -- the dry
    // click. Consumed by reading, for the same reason.
    bool consumeDryFire();

private:
    // Per-weapon ammunition. Parallel to `mDefinitions` like mCooldowns is,
    // rather than a map keyed by id: the controller already indexes weapons by
    // position everywhere else, and a second addressing scheme for one field is
    // how the two get out of step.
    struct AmmoRuntime {
        int magazine = 0;
        int reserve = 0;
        float reloadRemaining = 0.0f;
        float reloadTotal = 0.0f;
        bool reloading = false;
    };

    void syncAmmoRuntime();
    void updateReload(float dt);
    // Spend a shot's ammunition; false means the shot must not happen.
    bool consumeAmmo(std::size_t index);

    const std::vector<PlayerWeaponDef>* mDefinitions = nullptr;
    std::vector<float> mCooldowns;
    std::vector<AmmoRuntime> mAmmo;
    std::size_t mSelected = 0;
    float mSwitchRemaining = 0.0f;
    bool mFireHeld = false;
    bool mFirePressed = false;
    bool mSwapPressed = false;
    bool mReloadPressed = false;
    int mSelectSlot = -1;
    bool mSelectionChanged = false;
    bool mReloadFinished = false;
    bool mDryFire = false;
};

// Deterministic horizontal fan around a camera-derived aim direction.
std::vector<glm::vec3> projectileDirections(glm::vec3 direction, int count,
                                            float spreadDegrees);

} // namespace game
