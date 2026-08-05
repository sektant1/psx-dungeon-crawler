#pragma once
#include "RpgTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The character sheet: base attributes, a modifier layer, derived combat
// numbers, and the experience curve.
//
// THE ONE RULE
// Base attributes are the only truth. Every derived number is recomputed from
// (base + modifiers) on demand and stored nowhere. A buff or a piece of armour
// NEVER edits a base value -- it pushes a modifier tagged with its source and
// pops it by that tag. This is designed in rather than tested for, because the
// alternative failure ("+2 vigour" added on equip, subtracted twice across a
// save/load) is silent, cumulative, and impossible to diagnose from a symptom.
//
// This module is pure: no registry, no renderer, no physics. `applyTo` in
// StatsApply.h is the seam to the live combat components.
namespace game::rpg {

// Base attributes. Five, because five is the number a player can hold in their
// head while comparing two pieces of armour.
struct Attributes {
    float value[std::size_t(StatField::Fortune) + 1] = {10.0f, 10.0f, 10.0f,
                                                        10.0f, 10.0f};

    float& operator[](StatField f) { return value[std::size_t(f)]; }
    float operator[](StatField f) const { return value[std::size_t(f)]; }
};

// Everything combat and movement actually read. Recomputed, never stored as
// authority. Defaults here are only what a sheet with no attributes at all
// would produce; the real numbers come from progression.toml.
struct DerivedStats {
    float healthMax = 100.0f;
    float staminaMax = 100.0f;
    float manaMax = 100.0f;
    float poiseMax = 100.0f;
    float carryCapacity = 40.0f;
    float meleePower = 1.0f;   // multiplier on outgoing melee damage
    float castPower = 1.0f;    // multiplier on outgoing spell damage
    float critChance = 0.05f;  // 0..1
    float moveSpeedScale = 1.0f;
    float staminaRegen = 35.0f;
    float manaRegen = 10.0f;
    // Post-derive attribute totals, so a UI can show "12 (+2)" without
    // re-running the modifier fold.
    Attributes attributes;
};

// How attributes turn into derived numbers, and how experience is priced.
// Authored in assets/config/progression.toml so tuning the curve is not a
// rebuild. The defaults below are a playable dark-fantasy baseline, not a
// placeholder: a level-1 character has 100 health and dies to four hits from a
// tier-1 enemy.
struct ProgressionCurve {
    // derived = base + perPoint * attribute
    float healthBase = 20.0f, healthPerVigour = 8.0f;
    float staminaBase = 40.0f, staminaPerAgility = 6.0f;
    float manaBase = 20.0f, manaPerAttunement = 8.0f;
    float poiseBase = 30.0f, poisePerVigour = 5.0f, poisePerMight = 2.0f;
    float carryBase = 15.0f, carryPerMight = 2.5f;
    float meleePowerPerMight = 0.05f; // 1.0 + might * this
    float castPowerPerAttunement = 0.05f;
    float critBase = 0.02f, critPerFortune = 0.004f;
    float moveSpeedPerAgility = 0.008f; // 1.0 + (agility - 10) * this
    float staminaRegenBase = 25.0f, staminaRegenPerAgility = 1.0f;
    float manaRegenBase = 5.0f, manaRegenPerAttunement = 0.5f;

};

// One entry in the modifier layer, tagged with whoever pushed it.
//
// `source` is a string rather than an id because it is what a debug panel shows
// and what a save writes; the comparison happens once per equip, not per frame.
struct ModifierGroup {
    std::string source; // "equip:head", "effect:blessing_of_ash"
    std::vector<StatModifier> modifiers;
};

// The player's (or any character's) sheet.
class CharacterSheet {
public:
    void setCurve(const ProgressionCurve& c) { mCurve = c; mDirty = true; }
    const ProgressionCurve& curve() const { return mCurve; }

    // The baseline every character starts from, before any skill or any piece
    // of equipment. Progression is NOT here: levels live in SkillSet (OSRS
    // skills) and in its character level (Tarkov), and both reach this sheet
    // the same way a breastplate does -- as a modifier group. That is what
    // stops "level" from being two numbers that can disagree.
    Attributes& base() { mDirty = true; return mBase; }
    const Attributes& base() const { return mBase; }

    // --- the modifier layer ------------------------------------------------

    // Replace the modifiers from `source`, adding the source if it is new.
    // Idempotent, which is what makes "re-apply everything the player is
    // wearing" a safe operation to run after any inventory change.
    void setModifiers(const std::string& source,
                      std::vector<StatModifier> modifiers);
    // Drop a source entirely. Silent no-op when it was never pushed.
    void clearModifiers(const std::string& source);
    void clearAllModifiers();
    const std::vector<ModifierGroup>& modifierGroups() const { return mGroups; }

    // --- the answer ---------------------------------------------------------

    // Recomputed on demand and memoised until something invalidates it. The
    // memo is an optimisation only: derive() is a pure function of (base,
    // groups, curve) and clearing the cache can never change an answer.
    const DerivedStats& derived() const;

private:
    ProgressionCurve mCurve;
    Attributes mBase;
    std::vector<ModifierGroup> mGroups;

    mutable DerivedStats mDerived;
    mutable bool mDirty = true;
};

namespace stats {

// The pure fold, exposed for tests and for anything that wants to preview a
// sheet ("what would this helmet do") without mutating one.
//
// Two passes, and the order is the contract: attribute modifiers land first and
// feed the curve, then derived-field modifiers apply on top of the curve's
// output. An item that gives "+2 vigour" and one that gives "+16 health" are
// therefore both meaningful and do not double-count.
DerivedStats derive(const Attributes& base, const ProgressionCurve& curve,
                    const std::vector<ModifierGroup>& groups);

// The name a modifier group gets for the skill contributions, so skills and
// equipment go through one layer and re-applying is idempotent.
inline constexpr const char* kSkillModifierSource = "skills";

} // namespace stats

} // namespace game::rpg
