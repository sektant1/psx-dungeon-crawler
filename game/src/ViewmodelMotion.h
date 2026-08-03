#pragma once

#include "PlayerWeapons.h"
#include "ViewmodelRig.h"

#include <glm/glm.hpp>

#include <string>

namespace game {

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
    void setTuning(const ViewmodelRig& tuning) { mTuning = tuning; }
    const ViewmodelRig& tuning() const { return mTuning; }
    ViewmodelRig& tuning() { return mTuning; }

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
    ViewmodelRig mTuning{};
    ViewmodelFeel mFeel{};

    float mBobPhase = 0.0f;
    float mIdlePhase = 0.0f;
    float mRecoil = 0.0f;
    float mLanding = 0.0f;
    glm::vec2 mSway{0.0f};
    bool mWasGrounded = true;
};

// [player_viewmodel] out of a TOML document. Missing keys keep the default,
// a missing section is not an error (the shipped framing is the default).
bool loadViewmodelRig(const std::string& tomlPath, ViewmodelRig& out);
bool parseViewmodelRig(const char* tomlSource, ViewmodelRig& out);

// The section, formatted to paste straight back into assets/config/game.toml.
// The tuning loop is edit-live-then-paste, like the engine's Animation tab.
std::string viewmodelRigToml(const ViewmodelRig& rig);

// Writes `rig` into the `[player_viewmodel]` section of `tomlPath`, in place.
//
// Key by key, keeping the file's own comments, key order and everything outside
// the section: this file is authored content with forty lines of explanation in
// it, and a save that rewrote the whole section would silently delete them the
// first time somebody dragged a slider. Keys the section does not carry yet are
// appended to it; a file with no section at all gets one at the end.
//
// The write is atomic (temporary file, then rename), so a crash mid-save cannot
// leave the game with a half-written config it will refuse to load.
bool saveViewmodelRig(const std::string& tomlPath, const ViewmodelRig& rig);
// The [player_weapon.<id>.viewmodel] keys the panel can edit, same deal.
std::string viewmodelWeaponToml(const std::string& weaponId,
                                const WeaponViewmodelDef& viewmodel);

} // namespace game
