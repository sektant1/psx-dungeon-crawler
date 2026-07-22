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
private:
    glm::vec3 mTarget{0.0f};
    float mDistance = 12.0f;
    float mYaw = 0.7f;
    float mPitch = -0.5f;
};
