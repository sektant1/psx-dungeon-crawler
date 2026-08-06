#pragma once

#include "PlayerWeapons.h"

#include <eng/Handles.h>
#include <eng/Physics.h>

#include <glm/glm.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace eng { class Renderer; }

namespace game {

// The two deliveries that do not spawn a body.
//
// The data they run on -- WeaponFireMode, WeaponMeleeDef, WeaponHitscanDef --
// lives in PlayerWeapons.h beside every other weapon field, the same split
// ProjectileSystem and PlayerProjectileDef already use: definitions are content
// the editor and the cooker can read without a physics world, runtimes are not.
//
// Kept beside ProjectileSystem rather than inside it because their lifetimes
// are opposites: a projectile outlives the shot and is reconciled every frame,
// a hitscan is over before the call returns, and a melee swing is a timer with
// no entity at all. What they share is the *result* -- they report through the
// same impact callback ProjectileSystem uses, so damage, audio and hit feel see
// one kind of event and never learn which delivery produced it.
class WeaponDeliverySystem {
public:
    // Identical to ProjectileSystem::ImpactFn on purpose: one damage path.
    using ImpactFn = std::function<void(eng::BodyHandle victim,
                                        const std::string& payload,
                                        glm::vec3 travelDirection,
                                        glm::vec3 point)>;

    void setImpactCallback(ImpactFn fn) { mOnImpact = std::move(fn); }

    // Melee opens a swing window; hitscan resolves before this returns. Both
    // take the muzzle so the visual leaves the weapon while the aim stays the
    // camera ray -- the same `aim != muzzle` rule projectiles follow.
    void fire(eng::Physics& physics, eng::Renderer& renderer,
              const PlayerWeaponDef& weapon, glm::vec3 aimOrigin,
              glm::vec3 aimDirection,
              std::optional<glm::vec3> muzzleOrigin = std::nullopt);

    // Advances live swing windows and expires beam visuals. The aim frame is
    // passed in every step rather than captured at fire time so a swing tracks
    // the player: turning mid-swing turns the sweep, which is what a player
    // pressing the button while circling an enemy expects.
    void fixedUpdate(eng::Physics& physics, eng::Renderer& renderer,
                     glm::vec3 eye, glm::vec3 forward, float dt);

    void clear(eng::Physics& physics, eng::Renderer& renderer);
    bool swinging() const { return mSwing.active(); }

private:
    // One live melee swing. At most one at a time: the weapon's fire interval
    // already gates re-triggering, and overlapping swings from one hand is a
    // state no animation could present honestly.
    struct Swing {
        bool live = false;
        float windupLeft = 0.0f;
        float activeLeft = 0.0f;
        WeaponMeleeDef def;
        std::string payload;
        std::vector<uint32_t> alreadyHit;

        bool active() const { return live; }
    };
    // A beam quad/box that removes itself. Not particles: a hitscan trace is a
    // straight line of known length, and stretching one mesh is cheaper and
    // more readable than emitting along it.
    struct Beam {
        eng::NodeHandle node;
        float ttl = 0.0f;
    };

    void resolveHitscan(eng::Physics& physics, eng::Renderer& renderer,
                        const PlayerWeaponDef& weapon, glm::vec3 aimOrigin,
                        glm::vec3 aimDirection, glm::vec3 muzzle);
    void stepSwing(eng::Physics& physics, eng::Renderer& renderer,
                   glm::vec3 eye, glm::vec3 forward, float dt);
    eng::MeshHandle beamMesh(eng::Renderer& renderer);

    Swing mSwing;
    std::vector<Beam> mBeams;
    eng::MeshHandle mBeamMesh{};
    ImpactFn mOnImpact;
};

} // namespace game
