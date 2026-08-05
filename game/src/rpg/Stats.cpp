#include "Stats.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace game::rpg {

namespace {

// Accumulated flat and percentage terms for one field. Percentages add before
// they multiply (+10% and +15% is +25%, not +26.5%) -- the additive convention,
// because it is the one a player can do in their head while comparing two rings.
struct Term {
    float flat = 0.0f;
    float percent = 0.0f;
};

using Terms = std::array<Term, std::size_t(StatField::Count)>;

Terms fold(const std::vector<ModifierGroup>& groups)
{
    Terms terms{};
    for (const ModifierGroup& group : groups) {
        for (const StatModifier& m : group.modifiers) {
            const auto i = std::size_t(m.field);
            if (i >= terms.size())
                continue;
            terms[i].flat += m.flat;
            terms[i].percent += m.percent;
        }
    }
    return terms;
}

float applyTerm(float value, const Term& t)
{
    return (value + t.flat) * (1.0f + t.percent);
}

} // namespace

namespace stats {

DerivedStats derive(const Attributes& base, const ProgressionCurve& curve,
                    const std::vector<ModifierGroup>& groups)
{
    const Terms terms = fold(groups);

    // Pass one: attributes. Negative attributes are clamped away rather than
    // allowed to invert a derived formula -- a curse that takes 20 vigour off a
    // level-1 character should leave them fragile, not give them negative
    // health and an unkillable 0/-40 health bar.
    Attributes a;
    for (int i = 0; i <= int(StatField::Fortune); ++i)
        a.value[i] = std::max(0.0f, applyTerm(base.value[i], terms[std::size_t(i)]));

    DerivedStats d;
    d.attributes = a;

    const float might = a[StatField::Might];
    const float agility = a[StatField::Agility];
    const float vigour = a[StatField::Vigour];
    const float attunement = a[StatField::Attunement];
    const float fortune = a[StatField::Fortune];

    d.healthMax = curve.healthBase + vigour * curve.healthPerVigour;
    d.staminaMax = curve.staminaBase + agility * curve.staminaPerAgility;
    d.manaMax = curve.manaBase + attunement * curve.manaPerAttunement;
    d.poiseMax = curve.poiseBase + vigour * curve.poisePerVigour +
                 might * curve.poisePerMight;
    d.carryCapacity = curve.carryBase + might * curve.carryPerMight;
    d.meleePower = 1.0f + might * curve.meleePowerPerMight;
    d.castPower = 1.0f + attunement * curve.castPowerPerAttunement;
    d.critChance = curve.critBase + fortune * curve.critPerFortune;
    // Relative to the 10 an unmodified attribute starts at, so a character who
    // has never spent a point moves at exactly the speed the controller was
    // tuned for and the RPG layer is invisible until it is used.
    d.moveSpeedScale = 1.0f + (agility - 10.0f) * curve.moveSpeedPerAgility;
    d.staminaRegen = curve.staminaRegenBase + agility * curve.staminaRegenPerAgility;
    d.manaRegen = curve.manaRegenBase + attunement * curve.manaRegenPerAttunement;

    // Pass two: modifiers that name a derived field directly.
    const auto at = [&](StatField f) -> const Term& {
        return terms[std::size_t(f)];
    };
    d.healthMax = applyTerm(d.healthMax, at(StatField::HealthMax));
    d.staminaMax = applyTerm(d.staminaMax, at(StatField::StaminaMax));
    d.manaMax = applyTerm(d.manaMax, at(StatField::ManaMax));
    d.poiseMax = applyTerm(d.poiseMax, at(StatField::PoiseMax));
    d.carryCapacity = applyTerm(d.carryCapacity, at(StatField::CarryCapacity));
    d.meleePower = applyTerm(d.meleePower, at(StatField::MeleePower));
    d.castPower = applyTerm(d.castPower, at(StatField::CastPower));
    d.critChance = applyTerm(d.critChance, at(StatField::CritChance));
    d.moveSpeedScale = applyTerm(d.moveSpeedScale, at(StatField::MoveSpeedScale));
    d.staminaRegen = applyTerm(d.staminaRegen, at(StatField::StaminaRegen));
    d.manaRegen = applyTerm(d.manaRegen, at(StatField::ManaRegen));

    // Floors. A pool of zero is not a design statement anyone authored; it is
    // an arithmetic accident that divides by zero in every ratio the HUD draws.
    d.healthMax = std::max(1.0f, d.healthMax);
    d.staminaMax = std::max(1.0f, d.staminaMax);
    d.manaMax = std::max(0.0f, d.manaMax);
    d.poiseMax = std::max(1.0f, d.poiseMax);
    d.carryCapacity = std::max(0.0f, d.carryCapacity);
    d.critChance = std::clamp(d.critChance, 0.0f, 1.0f);
    d.moveSpeedScale = std::clamp(d.moveSpeedScale, 0.25f, 3.0f);
    d.staminaRegen = std::max(0.0f, d.staminaRegen);
    d.manaRegen = std::max(0.0f, d.manaRegen);
    return d;
}

} // namespace stats

void CharacterSheet::setModifiers(const std::string& source,
                                  std::vector<StatModifier> modifiers)
{
    const auto it = std::find_if(
        mGroups.begin(), mGroups.end(),
        [&](const ModifierGroup& g) { return g.source == source; });
    if (modifiers.empty()) {
        // An empty push is a clear: an item with no modifiers should not leave
        // an empty group behind for the debug panel to list.
        if (it != mGroups.end())
            mGroups.erase(it);
        mDirty = true;
        return;
    }
    if (it != mGroups.end())
        it->modifiers = std::move(modifiers);
    else
        mGroups.push_back({source, std::move(modifiers)});
    mDirty = true;
}

void CharacterSheet::clearModifiers(const std::string& source)
{
    const auto it = std::remove_if(
        mGroups.begin(), mGroups.end(),
        [&](const ModifierGroup& g) { return g.source == source; });
    if (it != mGroups.end()) {
        mGroups.erase(it, mGroups.end());
        mDirty = true;
    }
}

void CharacterSheet::clearAllModifiers()
{
    if (mGroups.empty())
        return;
    mGroups.clear();
    mDirty = true;
}

const DerivedStats& CharacterSheet::derived() const
{
    if (mDirty) {
        mDerived = stats::derive(mBase, mCurve, mGroups);
        mDirty = false;
    }
    return mDerived;
}

} // namespace game::rpg
