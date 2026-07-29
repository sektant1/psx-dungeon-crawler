#pragma once
#include "combat/CombatComponents.h"
#include "combat/CombatDirector.h"
#include "combat/CombatVocabulary.h"

#include <eng/Physics.h>

#include <entt/entt.hpp>

#include <string>
#include <unordered_map>

namespace game::sim {

// Headless simulation of the combat world -- no window, no renderer. Owns a real
// eng::Physics (Jolt) and the game's CombatDirector, so scripted playthroughs
// exercise the same damage/resistance/crowd-control model the game uses,
// deterministically. This is the testing/simulation API: drive it from code or
// from a text script (see SimScript), then query combatant state.
//
// Only combat is modelled today (the piece that is already renderer-free). The
// interface is small enough to grow player movement/interaction later, once
// those systems are decoupled from the renderer.
class World {
public:
    World();
    ~World();

    // Load a weapons.toml over the built-in defaults (optional).
    void loadWeapons(const std::string& tomlPath);

    // Damage channels + schools, as the game defines them. Loaded from
    // APP_ASSET_DIR/magic.toml at construction, so scripts can name channels.
    const CombatVocabulary& vocabulary() const { return mVocabulary; }

    // Register a named combatant. Returns false if the name is already taken.
    bool addCombatant(const std::string& name, float hp, Faction faction,
                      const Resistances& resist = {});

    // Land a weapon hit from `source` on `target`. dir orients knockback.
    // No-op (returns false) if either name is unknown.
    bool hit(const std::string& target, const std::string& weapon,
             const std::string& source);

    // Advance the combat model by dt (i-frames + status effects / DoT).
    void advance(float dt);

    // Queries. Unknown names report as dead / zero.
    bool alive(const std::string& name) const;
    float hp(const std::string& name) const;
    int activeEffects(const std::string& name) const;

    CombatDirector& combat() { return mCombat; }

private:
    CombatVocabulary mVocabulary;
    entt::entity entityOf(const std::string& name) const;

    eng::Physics mPhysics;
    CombatDirector mCombat;
    std::unordered_map<std::string, entt::entity> mByName;
    std::unordered_map<std::string, eng::BodyHandle> mBodies;
};

} // namespace game::sim
