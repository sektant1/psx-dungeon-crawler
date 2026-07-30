#pragma once

#include <eng/render/Enchantment.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace game {

// Damage channels and schools of magic are content, not code: both are defined
// in assets/magic.toml and referred to by string id. A designer adds a channel
// or a school by adding an entry, and the only C++ that changes is none.
//
// Ids are resolved to indices once, at load, and gameplay carries the index --
// string comparison has no place in the damage pipeline.
using DamageTypeId = uint8_t;

// Storage bound for a Resistances row. Raising it costs bytes per combatant;
// it is a fixed cap so resistance tables stay a flat array rather than a map.
inline constexpr int kMaxDamageTypes = 32;
inline constexpr DamageTypeId kInvalidDamageType = 0xFF;

struct DamageTypeDef {
    std::string id;
    // Skips the target's resistance table entirely.
    bool bypassesMitigation = false;
};

struct MagicSchoolDef {
    std::string id;
    DamageTypeId damage = 0;
    eng::EnchantmentPalette palette;
};

// The loaded vocabulary. One instance per world, passed to whoever needs to
// turn an authored name into an id -- weapon loading, level building, the
// status system. The damage resolver deliberately does not take one: a packet
// carries what it needs, so mitigation stays a pure function.
class CombatVocabulary {
public:
    // Replaces the current contents. On failure the vocabulary is left empty
    // and the caller gets false; a game with no damage types is a bug worth
    // failing loudly for, not one to paper over with built-in defaults.
    bool load(const std::string& tomlPath);

    int damageTypeCount() const { return int(mDamageTypes.size()); }
    // kInvalidDamageType when the name is not a defined channel.
    DamageTypeId damageType(std::string_view id) const;
    const DamageTypeDef* damageTypeDef(DamageTypeId) const;
    bool bypassesMitigation(DamageTypeId) const;

    const MagicSchoolDef* school(std::string_view id) const;
    // The school every enchantment without a named school falls back to: the
    // first one defined. Null only when the file defines no schools at all.
    const MagicSchoolDef* defaultSchool() const;
    // Palette for an authored school name, falling back to defaultSchool so a
    // typo in a level file dims into the default glow instead of dropping the
    // enchantment.
    eng::EnchantmentPalette palette(std::string_view schoolId) const;

    DamageTypeId burnDamageType() const { return mBurnDamage; }
    float defaultEnchantStrength() const { return mDefaultEnchantStrength; }

private:
    std::vector<DamageTypeDef> mDamageTypes;
    std::vector<MagicSchoolDef> mSchools;
    DamageTypeId mBurnDamage = 0;
    float mDefaultEnchantStrength = 0.75f;
};

} // namespace game
