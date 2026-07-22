#pragma once
#include <eng/Handles.h>
#include <glm/glm.hpp>
#include <string>

namespace eng { class Renderer; }

// First-person sword viewmodel. Parented to the camera head node so it
// inherits all view bob, crouch and tilt. Animated purely by transform
// composition each frame — no skeletal animation.
class ViewModel {
public:
    // Call once after every player.init() (the head node is new each time).
    void init(eng::Renderer& r, eng::NodeHandle headNode,
              const std::string& propsDir);

    // Call once per frame (variable dt is fine — this is cosmetic only).
    //   triggerAttack : rising edge that starts the slash animation.
    //   parryHeld     : true while the guard key is down.
    void update(eng::Renderer& r, float dt, bool triggerAttack, bool parryHeld);

private:
    eng::NodeHandle mNode{};

    // Head-local rest position: lower-right, slightly forward of the camera.
    glm::vec3 mBasePos{0.30f, -0.28f, -0.52f};

    // Attack animation state.  -1 = idle, 0..kAttackDur = active.
    static constexpr float kAttackDur = 0.35f;
    float mAttackTime = -1.0f;

    // Guard blend 0 (low) -> 1 (raised).
    float mParry = 0.0f;

    // Idle breathing sway accumulator.
    float mSwayPhase = 0.0f;
};
