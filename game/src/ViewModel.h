#pragma once
#include "PlayerWeapons.h"

#include <eng/render/Enchantment.h>
#include <eng/Handles.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace eng { class Renderer; }

// Shared camera-space socket for first-person equipment. New weapons inherit
// this framing automatically and only need to override fields when their
// authored axis or dimensions differ substantially.
// The enchantment glow a viewmodel wears. Which school that is comes from the
// game's magic.toml, so the viewmodel takes the resolved palette and has no
// list of schools of its own. strength 0 = no glow.
struct ViewmodelGlow {
    eng::EnchantmentPalette palette;
    float strength = 0.0f;
};

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
              const std::string& propsDir, ViewmodelGlow glow = {},
              const WeaponViewmodelPose& pose = {});

    // Generic seam for future first-person weapons. `init` above is the
    // sword convenience wrapper used by the current game.
    void initWeapon(eng::Renderer& r, eng::NodeHandle headNode,
                    const std::string& meshPath,
                    const std::string& materialName, ViewmodelGlow glow = {},
                    const WeaponViewmodelPose& pose = {});

    // Procedural caster staff: a long thin shaft plus a crystal tip, attached to
    // the viewmodel node. Reuses the shared attack animation for a cast thrust.
    void initStaff(eng::Renderer& r, eng::NodeHandle headNode,
                   const std::string& crystalMeshPath, ViewmodelGlow tipGlow = {},
                   const WeaponViewmodelPose& pose = {});

    // Procedural handheld torch (no wall bracket): a short wood handle with a
    // live flame — fire/glow/ash particles plus a warm point light — at its top,
    // so it lights the scene while equipped. Reuses the attack thrust animation.
    void initTorch(eng::Renderer& r, eng::NodeHandle headNode,
                   ViewmodelGlow handleGlow = {},
                   const WeaponViewmodelPose& pose = {});

    // Data-authored procedural placeholder. Presentation type stays behind this
    // boundary so a later sprite/model implementation does not change weapon
    // simulation or inventory code.
    void initPlayerWeapon(eng::Renderer& r, eng::NodeHandle headNode,
                          const game::WeaponViewmodelDef& definition,
                          ViewmodelGlow glow = {});

    // Call once per frame (variable dt is fine — this is cosmetic only).
    //   triggerAttack : rising edge that starts the slash animation.
    //   parryHeld     : true while the guard key is down.
    void update(eng::Renderer& r, float dt, bool triggerFire, float moveSpeed,
                glm::vec2 lookDelta, bool grounded);
    void beginEquip();
    void configure(const game::WeaponViewmodelDef& definition);

    // Show/hide the whole viewmodel (used to swap the active weapon).
    void setVisible(eng::Renderer& r, bool show);

    // Toggle the enchantment glow at runtime. The authored glow and the node
    // that wears it are captured at init, so switching it back on restores this
    // weapon's own school instead of some generic default. Off until asked for:
    // the glow is presentation, and a plain weapon is the honest baseline to
    // tune lighting and materials against.
    void setEnchantEnabled(eng::Renderer& r, bool on);
    bool enchantEnabled() const { return mEnchantEnabled; }

private:
    // Applies mEnchantEnabled to every authored glow part.
    void applyEnchant(eng::Renderer& r);

    eng::NodeHandle mNode{};
    // The node carrying the glow: the weapon mesh for an imported weapon, the
    // crystal for the staff, the handle for the torch.
    std::vector<eng::NodeHandle> mGlowNodes;
    ViewmodelGlow mGlow{};
    bool mEnchantEnabled = false;

    WeaponViewmodelPose mPose{};
    game::WeaponViewmodelDef mPresentation{};

    // Attack animation state.  -1 = idle, 0..kAttackDur = active.
    float mAttackTime = -1.0f;
    float mRecoil = 0.0f;
    float mEquipTime = 0.0f;
    glm::vec2 mLookOffset{0.0f};

    // Idle breathing sway accumulator.
    float mSwayPhase = 0.0f;
    float mMovePhase = 0.0f;
};
