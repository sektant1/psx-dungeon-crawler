#pragma once
#include "CombatVocabulary.h"
#include "WeaponDef.h"

#include <string>
#include <unordered_map>

namespace game {

// Data-driven weapon table. Seeds sensible built-in defs (sword/arrow/fireball/
// beam/torch) so the game runs without a file, then overlays weapons.toml when
// present. Look weapons up by id; missing ids return a safe unarmed fallback.
class WeaponLibrary {
public:
    WeaponLibrary(); // installs built-in defaults

    // Overlay definitions from weapons.toml. Missing file leaves defaults
    // intact and returns false (already logged by the caller if it cares).
    bool load(const std::string& tomlPath);

    // Overlay definitions from an in-memory TOML string (same format as the
    // file variant). Useful for tests that don't want a real file on disk.
    bool loadFromString(const char* tomlSrc);

    // Turn every definition's authored damage-channel name into an id. Call
    // after the last load; until then WeaponDef::damageType is meaningless.
    void resolve(const CombatVocabulary& vocabulary);

    // Look up by id ("sword", "arrow", "fireball", ...). Never null: unknown ids
    // resolve to a neutral unarmed def.
    const WeaponDef& get(const std::string& id) const;

    std::unordered_map<std::string, WeaponDef>& defs() { return mDefs; }

private:
    std::unordered_map<std::string, WeaponDef> mDefs;
    WeaponDef mFallback;
};

} // namespace game
