#include "WeaponLibrary.h"

#include <eng/Log.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <array>
#include <string>

namespace game {

namespace {

float num(const toml::table& t, const char* key, float fb) {
    return float(t[key].value_or(double(fb)));
}

CrowdControl parseCC(const std::string& s, bool& ok) {
    ok = true;
    if (s == "stun") return CrowdControl::Stun;
    if (s == "root") return CrowdControl::Root;
    if (s == "silence") return CrowdControl::Silence;
    if (s == "slow") return CrowdControl::Slow;
    if (s == "chill") return CrowdControl::Chill;
    if (s == "burn") return CrowdControl::Burn;
    ok = false;
    return CrowdControl::Stun;
}

void parseWeapons(const toml::table& root,
                  std::unordered_map<std::string, WeaponDef>& mDefs) {
    const toml::table* weapons = root["weapon"].as_table();
    if (!weapons)
        return;

    for (auto&& [key, node] : *weapons) {
        const toml::table* t = node.as_table();
        if (!t)
            continue;
        WeaponDef def = mDefs.count(std::string(key.str()))
                            ? mDefs[std::string(key.str())]
                            : WeaponDef{};
        def.name = t->at_path("name").value_or(def.name);
        def.baseDamage = num(*t, "base_damage", def.baseDamage);
        if (auto s = (*t)["damage_type"].value<std::string>())
            def.damageTypeName = *s;
        def.critChance = num(*t, "crit_chance", def.critChance);
        def.critMultiplier = num(*t, "crit_multiplier", def.critMultiplier);
        def.knockback = num(*t, "knockback", def.knockback);
        // Optional CC array: each is { kind, magnitude, duration }.
        if (const toml::array* cc = (*t)["cc_on_hit"].as_array()) {
            def.ccOnHit.clear();
            for (auto&& elem : *cc) {
                const toml::table* c = elem.as_table();
                if (!c)
                    continue;
                bool ok = false;
                const std::string kind =
                    (*c)["kind"].value_or(std::string{});
                CrowdControl k = parseCC(kind, ok);
                if (!ok)
                    continue;
                def.ccOnHit.push_back({k, num(*c, "magnitude", 0.0f),
                                       num(*c, "duration", 0.0f)});
            }
        }
        // Feel-layer timing (defaults preserve AttackDef{} when omitted).
        def.timing.windup      = num(*t, "windup",       def.timing.windup);
        def.timing.active      = num(*t, "active",       def.timing.active);
        def.timing.recovery    = num(*t, "recovery",     def.timing.recovery);
        def.timing.staminaCost = num(*t, "stamina_cost", def.timing.staminaCost);
        def.timing.poiseDamage = num(*t, "poise_damage", def.timing.poiseDamage);
        def.timing.isSweep     = (*t)["is_sweep"].value_or(def.timing.isSweep);
        def.timing.arc         = num(*t, "arc",          def.timing.arc);
        def.drawTime           = num(*t, "draw_time",    def.drawTime);
        def.fullDrawMult       = num(*t, "full_draw_mult", def.fullDrawMult);
        mDefs[std::string(key.str())] = def;
    }
}

} // namespace

WeaponLibrary::WeaponLibrary() {
    // Built-in defaults: the game is playable without weapons.toml. Values are
    // the single source of balance truth until the file overrides them.
    WeaponDef sword;
    sword.name = "Iron Sword";
    sword.baseDamage = 22.0f;
    sword.damageTypeName = "physical";
    sword.critChance = 0.15f;
    sword.critMultiplier = 2.0f;
    sword.knockback = 6.0f;
    mDefs["sword"] = sword;

    WeaponDef arrow;
    arrow.name = "Arrow";
    arrow.baseDamage = 18.0f;
    arrow.damageTypeName = "physical";
    arrow.critChance = 0.20f;
    arrow.critMultiplier = 2.5f;
    arrow.knockback = 4.0f;
    mDefs["arrow"] = arrow;

    WeaponDef fireball;
    fireball.name = "Fireball";
    fireball.baseDamage = 30.0f;
    fireball.damageTypeName = "fire";
    fireball.critChance = 0.10f;
    fireball.critMultiplier = 2.0f;
    fireball.knockback = 5.0f;
    fireball.ccOnHit.push_back({CrowdControl::Burn, 6.0f, 3.0f}); // 6 dps, 3 s
    mDefs["fireball"] = fireball;

    WeaponDef beam;
    beam.name = "Frost Beam";
    beam.baseDamage = 12.0f;
    beam.damageTypeName = "frost";
    beam.knockback = 1.0f;
    beam.ccOnHit.push_back({CrowdControl::Chill, 0.4f, 2.0f}); // 40% slow, 2 s
    mDefs["beam"] = beam;

    WeaponDef torch;
    torch.name = "Torch";
    torch.baseDamage = 8.0f;
    torch.damageTypeName = "fire";
    torch.knockback = 3.0f;
    torch.ccOnHit.push_back({CrowdControl::Burn, 3.0f, 2.0f});
    mDefs["torch"] = torch;
}

bool WeaponLibrary::load(const std::string& tomlPath) {
    toml::parse_result parsed = toml::parse_file(tomlPath);
    if (!parsed)
        return false;
    parseWeapons(parsed.table(), mDefs);
    return true;
}

void WeaponLibrary::resolve(const CombatVocabulary& vocabulary) {
    for (auto& [id, def] : mDefs) {
        const DamageTypeId resolved = vocabulary.damageType(def.damageTypeName);
        if (resolved == kInvalidDamageType) {
            eng::log::error(
                "WeaponLibrary: weapon '%s' uses damage type '%s', which "
                "magic.toml does not define; falling back to the first channel",
                id.c_str(), def.damageTypeName.c_str());
            def.damageType = 0;
            def.damageIgnoresResistances = vocabulary.bypassesMitigation(0);
            continue;
        }
        def.damageType = resolved;
        def.damageIgnoresResistances = vocabulary.bypassesMitigation(resolved);
    }
}

bool WeaponLibrary::loadFromString(const char* tomlSrc) {
    toml::parse_result parsed = toml::parse(tomlSrc);
    if (!parsed)
        return false;
    parseWeapons(parsed.table(), mDefs);
    return true;
}

const WeaponDef& WeaponLibrary::get(const std::string& id) const {
    auto it = mDefs.find(id);
    return it == mDefs.end() ? mFallback : it->second;
}

} // namespace game
