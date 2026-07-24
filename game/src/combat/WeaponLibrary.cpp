#include "WeaponLibrary.h"

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <array>
#include <string>

namespace game {

namespace {

float num(const toml::table& t, const char* key, float fb) {
    return float(t[key].value_or(double(fb)));
}

DamageType parseType(const std::string& s, DamageType fb) {
    static const std::array<std::pair<const char*, DamageType>, kDamageTypeCount>
        kMap{{{"physical", DamageType::Physical},
              {"fire", DamageType::Fire},
              {"frost", DamageType::Frost},
              {"lightning", DamageType::Lightning},
              {"poison", DamageType::Poison},
              {"arcane", DamageType::Arcane},
              {"true", DamageType::True}}};
    for (auto& [name, t] : kMap)
        if (s == name)
            return t;
    return fb;
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

} // namespace

WeaponLibrary::WeaponLibrary() {
    // Built-in defaults: the game is playable without weapons.toml. Values are
    // the single source of balance truth until the file overrides them.
    WeaponDef sword;
    sword.name = "Iron Sword";
    sword.baseDamage = 22.0f;
    sword.damageType = DamageType::Physical;
    sword.critChance = 0.15f;
    sword.critMultiplier = 2.0f;
    sword.knockback = 6.0f;
    mDefs["sword"] = sword;

    WeaponDef arrow;
    arrow.name = "Arrow";
    arrow.baseDamage = 18.0f;
    arrow.damageType = DamageType::Physical;
    arrow.critChance = 0.20f;
    arrow.critMultiplier = 2.5f;
    arrow.knockback = 4.0f;
    mDefs["arrow"] = arrow;

    WeaponDef fireball;
    fireball.name = "Fireball";
    fireball.baseDamage = 30.0f;
    fireball.damageType = DamageType::Fire;
    fireball.critChance = 0.10f;
    fireball.critMultiplier = 2.0f;
    fireball.knockback = 5.0f;
    fireball.ccOnHit.push_back({CrowdControl::Burn, 6.0f, 3.0f}); // 6 dps, 3 s
    mDefs["fireball"] = fireball;

    WeaponDef beam;
    beam.name = "Frost Beam";
    beam.baseDamage = 12.0f;
    beam.damageType = DamageType::Frost;
    beam.knockback = 1.0f;
    beam.ccOnHit.push_back({CrowdControl::Chill, 0.4f, 2.0f}); // 40% slow, 2 s
    mDefs["beam"] = beam;

    WeaponDef torch;
    torch.name = "Torch";
    torch.baseDamage = 8.0f;
    torch.damageType = DamageType::Fire;
    torch.knockback = 3.0f;
    torch.ccOnHit.push_back({CrowdControl::Burn, 3.0f, 2.0f});
    mDefs["torch"] = torch;
}

bool WeaponLibrary::load(const std::string& tomlPath) {
    toml::parse_result parsed = toml::parse_file(tomlPath);
    if (!parsed)
        return false;
    const toml::table& root = parsed.table();
    const toml::table* weapons = root["weapon"].as_table();
    if (!weapons)
        return false;

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
            def.damageType = parseType(*s, def.damageType);
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
        mDefs[std::string(key.str())] = def;
    }
    return true;
}

const WeaponDef& WeaponLibrary::get(const std::string& id) const {
    auto it = mDefs.find(id);
    return it == mDefs.end() ? mFallback : it->second;
}

} // namespace game
