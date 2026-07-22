#pragma once
#include <eng/Handles.h>
#include <glm/glm.hpp>
#include <string>

namespace eng { class Renderer; }

// Shared camera-space socket for first-person equipment. New weapons inherit
// this framing automatically and only need to override fields when their
// authored axis or dimensions differ substantially.
struct WeaponViewmodelPose {
    // Lunacid-style idle: grip low-right, blade upright along the screen edge.
    glm::vec3 position{0.31f, -0.35f, -0.72f};
    glm::vec3 rotationDegrees{-8.0f, 12.0f, 4.0f}; // pitch, yaw, roll
    float scale = 0.035f;

    // New weapon assets should be authored with their hand/grip at the origin.
    // Legacy imports can override this mesh-space point without changing the
    // universal camera socket or animation.
    glm::vec3 gripPivot{0.0f};

    // Axial mesh correction around the authored grip/blade +Y axis. The
    // This is the default convention for every future upright melee weapon:
    // its narrow edge faces the camera and cutting edges point forward/back.
    float gripAxisTwistDegrees = 90.0f;
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

    // Procedural caster staff: a long thin shaft plus a crystal tip, attached to
    // the viewmodel node. Reuses the shared attack animation for a cast thrust.
    void initStaff(eng::Renderer& r, eng::NodeHandle headNode,
                   const std::string& crystalMeshPath,
                   const WeaponViewmodelPose& pose = {});

    // Call once per frame (variable dt is fine — this is cosmetic only).
    //   triggerAttack : rising edge that starts the slash animation.
    //   parryHeld     : true while the guard key is down.
    void update(eng::Renderer& r, float dt, bool triggerAttack, bool parryHeld);

    // Show/hide the whole viewmodel (used to swap the active weapon).
    void setVisible(eng::Renderer& r, bool show);

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
