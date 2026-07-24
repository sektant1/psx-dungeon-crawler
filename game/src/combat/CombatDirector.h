#pragma once
#include "CombatComponents.h"
#include "WeaponLibrary.h"

#include <eng/Handles.h>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace eng { class Physics; }

namespace game {

// Owns the combat registry (entities with Health/Resistances/Faction/Status),
// the weapon table, and the body<->entity lookup. It is the seam gameplay code
// talks to: register combatants, route hits by physics body, tick status
// effects, and get told when something dies. Damage math lives in the pure
// DamageSystem; physics knockback and death visuals are driven from here.
class CombatDirector {
public:
    using DeathFn = std::function<void(entt::entity)>;

    // Load weapons.toml (built-in defaults remain if the file is absent).
    void init(const std::string& weaponsTomlPath);

    // Register a combatant backed by a physics body. Returns its entity.
    entt::entity addCombatant(eng::BodyHandle body, const Health& hp,
                              const Resistances& resist, Faction faction);
    // Forget a combatant (its body was removed). Safe on unknown bodies.
    void removeCombatant(eng::BodyHandle body);

    entt::entity entityForBody(eng::BodyHandle body) const;

    // Resolve a hit on the entity behind `victimBody` using weapon `weaponId`.
    // `dir` orients knockback; `atPoint` is where the impulse is applied. No-op
    // if the body is not a registered combatant. Applies physics knockback and
    // fires the death callback when the hit kills.
    void hitBody(eng::Physics& physics, eng::BodyHandle victimBody,
                 const std::string& weaponId, entt::entity source,
                 glm::vec3 dir, glm::vec3 atPoint);

    // Advance i-frame timers and status effects; fire death callbacks for any
    // entity a damage-over-time tick killed this frame.
    void tick(float dt);

    void setDeathCallback(DeathFn fn) { mOnDeath = std::move(fn); }

    entt::registry& registry() { return mReg; }
    WeaponLibrary& weapons() { return mWeapons; }

private:
    entt::registry mReg;
    WeaponLibrary mWeapons;
    std::unordered_map<std::uint32_t, entt::entity> mByBody;
    std::mt19937 mRng{0xC0FFEEu}; // fixed seed: crit rolls stay reproducible
    DeathFn mOnDeath;
    std::vector<entt::entity> mKilledScratch;
};

} // namespace game
