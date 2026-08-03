#pragma once

#include "PlayerWeapons.h"

#include <glm/glm.hpp>

#include <string>

namespace game {

// Where the shared first-person rig sits relative to the eye, and how strongly
// it is allowed to move there. This is the half of viewmodel presentation that
// belongs to the *player*, not to any one weapon: swapping weapons must never
// move the hands out of frame, so the camera-space socket, the scale and the
// global motion multipliers are authored once in game.toml's
// [player_viewmodel] and tuned live from the Viewmodel panel (F1).
//
// Defaults reproduce the framing the rig shipped with, so a missing section
// changes nothing.
struct ViewmodelRigTuning {
    // Camera space: +x right, +y up, -z forward (the camera looks down -z).
    glm::vec3 offset{0.0f, -0.95f, -0.75f};
    // The source rig faces glTF +z; the camera faces -z, hence the half turn.
    glm::vec3 rotationDegrees{0.0f, 180.0f, 0.0f};
    float scale = 0.50f;

    // Global multipliers over the per-weapon feel numbers. A designer dials the
    // whole presentation from here without editing three weapon definitions;
    // 0 switches a layer off entirely.
    float bobScale = 1.0f;
    float swayScale = 1.0f;
    float recoilScale = 1.0f;

    // Movement bob. Speed is normalised against `bobReferenceSpeed` so the bob
    // reads the same whether the player's tuned move speed is 3 or 8 m/s.
    float bobReferenceSpeed = 6.0f;
    float bobRollDegrees = 1.4f;

    // Mouse-look sway: the hands lag the view, then are pulled back to centre.
    float swayReturn = 9.0f;    // 1/s toward centre
    float swayMax = 0.05f;      // metres; clamps a fast flick
    float swayRollDegrees = 3.0f;

    // Landing impulse: a dip on touchdown, recovered like the recoil spring.
    float landingDip = 0.055f;
    float landingRecovery = 9.0f;

    // Freeze every procedural layer. Authoring a static pose is impossible
    // while the rig is breathing, so the panel needs one switch for it.
    bool motionEnabled = true;
};

// The per-weapon slice of presentation the motion composer needs. Copied out of
// WeaponViewmodelDef so the composer stays free of weapon simulation: it never
// sees ammo, damage or projectiles, only how this weapon wants to move.
struct ViewmodelFeel {
    glm::vec3 offset{0.0f};          // added to the rig socket, camera space
    glm::vec3 rotationDegrees{0.0f}; // added to the rig orientation
    float scale = 1.0f;              // multiplies the rig scale

    float recoilDistance = 0.06f;
    float recoilPitchDegrees = 6.0f;
    float recoilYawDegrees = 0.0f;
    float recoilRecovery = 20.0f;
    float movementBob = 0.012f;
    float movementBobSpeed = 7.5f;
    float idleSway = 0.004f;
    float lookSway = 0.0012f;
};

ViewmodelFeel viewmodelFeel(const WeaponViewmodelDef& definition);

struct ViewmodelMotionInput {
    float dt = 0.0f;
    float horizontalSpeed = 0.0f;
    bool grounded = true;
    // Raw look delta applied this frame, in the same units the controller was
    // fed (pixels). Sway is clamped, so the unit only sets the gain scale.
    glm::vec2 lookDelta{0.0f};
};

// Camera-space transform the rig node wears this frame.
struct ViewmodelPose {
    glm::vec3 position{0.0f};
    glm::vec3 rotationDegrees{0.0f};
    float scale = 1.0f;
};

// Composes the first-person rig transform from independent layers:
//
//   base (rig socket + weapon offset)
//     + movementBob + idleSway + lookSway + recoil + landing
//
// Each layer is its own accumulator with its own tuning, so one can be muted
// without touching the others, and none of them knows what a weapon is beyond
// the ViewmodelFeel it was handed. Pure math: no renderer, no ECS, no ImGui.
class ViewmodelMotion {
public:
    void setTuning(const ViewmodelRigTuning& tuning) { mTuning = tuning; }
    const ViewmodelRigTuning& tuning() const { return mTuning; }
    ViewmodelRigTuning& tuning() { return mTuning; }

    void setFeel(const ViewmodelFeel& feel) { mFeel = feel; }
    const ViewmodelFeel& feel() const { return mFeel; }

    // Rising edge of a shot. `strength` scales the kick for charged/alt fire.
    void kick(float strength = 1.0f);
    // Drops every accumulator (weapon switch, respawn, level transition).
    void reset();

    ViewmodelPose update(const ViewmodelMotionInput& input);

    // Read-only state, for the tuning panel's live readout.
    float recoil() const { return mRecoil; }
    float landing() const { return mLanding; }
    glm::vec2 swayOffset() const { return mSway; }
    float bobPhase() const { return mBobPhase; }

private:
    ViewmodelRigTuning mTuning{};
    ViewmodelFeel mFeel{};

    float mBobPhase = 0.0f;
    float mIdlePhase = 0.0f;
    float mRecoil = 0.0f;
    float mLanding = 0.0f;
    glm::vec2 mSway{0.0f};
    bool mWasGrounded = true;
};

// Every field finite, scale positive, multipliers non-negative. A rejected
// table leaves the caller's tuning untouched rather than half-applied.
bool validViewmodelRigTuning(const ViewmodelRigTuning& tuning);

// [player_viewmodel] out of a TOML document. Missing keys keep the default,
// a missing section is not an error (the shipped framing is the default).
bool loadViewmodelRigTuning(const std::string& tomlPath,
                            ViewmodelRigTuning& out);
bool parseViewmodelRigTuning(const char* tomlSource, ViewmodelRigTuning& out);

// The section, formatted to paste straight back into assets/config/game.toml.
// The tuning loop is edit-live-then-paste, like the engine's Animation tab.
std::string viewmodelRigToml(const ViewmodelRigTuning& tuning);
// The [player_weapon.<id>.viewmodel] keys the panel can edit, same deal.
std::string viewmodelWeaponToml(const std::string& weaponId,
                                const WeaponViewmodelDef& viewmodel);

} // namespace game
