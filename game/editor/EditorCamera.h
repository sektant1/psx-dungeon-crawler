#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
// Orbit/fly editor camera. Pure math + state; the caller applies the resulting
// transform to a Renderer camera node. No engine/Ogre dependency.
class EditorCamera {
public:
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
private:
    glm::vec3 mTarget{0.0f};
    float mDistance = 12.0f;
    float mYaw = 0.7f;
    float mPitch = -0.5f;
    glm::vec3 mFlyPos{0.0f, 2.0f, 6.0f};
};
