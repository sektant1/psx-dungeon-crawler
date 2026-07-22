#pragma once
#include <eng/Handles.h>
#include <glm/glm.hpp>
#include <string>

namespace eng { class Renderer; }

// Shared camera-space socket for first-person equipment. New weapons inherit
// this framing automatically and only need to override fields when their
// authored axis or dimensions differ substantially.
struct WeaponViewmodelPose {
    glm::vec3 position{0.30f, -0.30f, -0.42f};
    glm::vec3 rotationDegrees{-72.0f, -4.0f, 18.0f}; // pitch, yaw, roll
    float scale = 0.043f;
};

// First-person sword viewmodel. Parented to the camera head node so it
// inherits all view bob, crouch and tilt. Animated purely by transform
// composition each frame — no skeletal animation.
class ViewModel {
public:
    // Call once after every player.init() (the head node is new each time).
    void init(eng::Renderer& r, eng::NodeHandle headNode,
              const std::string& propsDir,
              const WeaponViewmodelPose& pose = {});

    // Generic seam for future first-person weapons. `init` above is the
    // sword convenience wrapper used by the current game.
    void initWeapon(eng::Renderer& r, eng::NodeHandle headNode,
                    const std::string& meshPath,
                    const std::string& materialName,
                    const WeaponViewmodelPose& pose = {});

    // Call once per frame (variable dt is fine — this is cosmetic only).
    //   triggerAttack : rising edge that starts the slash animation.
    //   parryHeld     : true while the guard key is down.
    void update(eng::Renderer& r, float dt, bool triggerAttack, bool parryHeld);

private:
    eng::NodeHandle mNode{};

    WeaponViewmodelPose mPose{};

    // Attack animation state.  -1 = idle, 0..kAttackDur = active.
    static constexpr float kAttackDur = 0.35f;
    float mAttackTime = -1.0f;

    // Guard blend 0 (low) -> 1 (raised).
    float mParry = 0.0f;

    // Idle breathing sway accumulator.
    float mSwayPhase = 0.0f;
};
