#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
// Orbit/fly/walk editor camera. Pure math + state; the caller applies the
// resulting transform to a Renderer camera node. No engine/Ogre dependency.
class EditorCamera {
public:
    // The player's eye sits this far above the floor. This is a second copy of
    // kPlayerEyeHeight in game/src/main.cpp, which in turn matches
    // eng::CharacterDesc::height used to create the player; the editor cannot
    // include the game app, so the two must be changed together.
    static constexpr float kPlayerEyeHeight = 1.7f;
    // The game's vertical field of view, from game.cameraFov in
    // game/src/main.cpp (eng::FpsGameConfig::cameraFov). The walk preview must
    // frame exactly as much world as the player will see, which is the entire
    // point of the mode, so it deliberately does NOT use the editor's 65 deg.
    static constexpr float kGameFovDeg = 70.0f;
    // The orbit/fly viewport fov, mirrored here only so tests can assert that
    // the walk preview really is a different framing and catch a silent drift
    // in either value.
    static constexpr float kEditorFovDeg = 65.0f;
    // A comfortable authoring walk pace in metres per second. Slower than the
    // player's run speed on purpose: the mode is for reading a space, not for
    // racing through it.
    static constexpr float kWalkSpeed = 3.0f;

    void orbit(float dYawRad, float dPitchRad); // mouse drag
    void dolly(float delta);                    // scroll toward/away target
    void pan(glm::vec3 worldDelta);
    void frame(glm::vec3 target, float distance); // recenter + set orbit radius
    glm::vec3 eye() const;          // derived from target/distance/yaw/pitch
    glm::quat orientation() const;  // look-at target
    glm::vec3 target() const { return mTarget; }
    float distance() const { return mDistance; }

    // --- free-fly mode (independent of orbit target) ---------------------
    void setFlyPosition(glm::vec3 pos) { mFlyPos = pos; }
    void setYawPitch(float yawRad, float pitchRad);
    void addYawPitch(float dYawRad, float dPitchRad);
    void moveLocal(glm::vec3 localDelta); // +X right, +Y up, -Z forward
    glm::vec3 flyEye() const { return mFlyPos; }
    glm::quat flyOrientation() const;
    float yaw() const { return mYaw; }
    float pitch() const { return mPitch; }

    // --- walk mode (player-eye level preview) -----------------------------
    // A presentation preview, NOT a character controller: there is no gravity,
    // no collision, no step-up and no physics world in the editor. The eye is
    // pinned to a constant height above a flat floor plane, so it will happily
    // pass through walls and ignore stairs. Anyone tempted to grow this into a
    // simulation should drive the real eng::FpsController instead.
    void enterWalk(glm::vec3 floorPosition, float yawDegrees);
    // Restores the orbit/fly camera exactly as it was when enterWalk() ran, so
    // an author never loses their framing by peeking at eye level.
    void leaveWalk();
    bool walking() const { return mWalking; }
    // Teleports the preview to a new floor position, keeping the eye height.
    void setWalkFloorPosition(glm::vec3 floorPosition);
    // Mouse look. Pitch is clamped to a human range and roll is structurally
    // impossible: the orientation is built yaw-then-pitch only.
    void walkLook(float dYawRad, float dPitchRad);
    // Horizontal movement only; localDelta is in metres, +X right, -Z forward.
    // Any Y component is discarded so the eye height cannot drift.
    void walkMove(glm::vec3 localDelta);
    glm::vec3 walkFloorPosition() const { return mWalkFloor; }
    glm::vec3 walkEye() const;
    glm::quat walkOrientation() const;
    float walkFovDeg() const { return kGameFovDeg; }
    float walkYaw() const { return mWalkYaw; }
    float walkPitch() const { return mWalkPitch; }

    // Pose currently presented by the viewport. Picking, gizmos and rendering
    // must all use these accessors or walk mode becomes visually disconnected
    // from the tools under the cursor.
    glm::vec3 activeEye() const { return mWalking ? walkEye() : flyEye(); }
    glm::quat activeOrientation() const
    {
        return mWalking ? walkOrientation() : flyOrientation();
    }
    float activeFovDeg() const { return mWalking ? kGameFovDeg : kEditorFovDeg; }
private:
    glm::vec3 mTarget{0.0f};
    float mDistance = 12.0f;
    float mYaw = 0.7f;
    float mPitch = -0.5f;
    glm::vec3 mFlyPos{0.0f, 2.0f, 6.0f};

    // Walk state is kept separate from the orbit/fly state rather than
    // borrowing mYaw/mFlyPos, so entering the mode cannot perturb the author's
    // framing in the first place; the saved snapshot below is belt-and-braces
    // for callers that also drive the shared setters while walking.
    bool mWalking = false;
    glm::vec3 mWalkFloor{0.0f};
    float mWalkYaw = 0.0f;
    float mWalkPitch = 0.0f;
    glm::vec3 mSavedTarget{0.0f};
    float mSavedDistance = 12.0f;
    float mSavedYaw = 0.0f;
    float mSavedPitch = 0.0f;
    glm::vec3 mSavedFlyPos{0.0f};
};
