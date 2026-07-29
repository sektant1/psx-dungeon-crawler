#include "CombatVocabulary.h"

#include <eng/Log.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

namespace game {

namespace {

glm::vec4 vec4Or(const toml::node_view<const toml::node>& node, glm::vec4 fb)
{
    const toml::array* a = node.as_array();
    if (!a || a->size() != 4)
        return fb;
    glm::vec4 out = fb;
    for (size_t i = 0; i < 4; ++i)
        out[int(i)] = float((*a)[i].value_or(double(fb[int(i)])));
    return out;
}

glm::vec3 vec3Or(const toml::node_view<const toml::node>& node, glm::vec3 fb)
{
    const toml::array* a = node.as_array();
    if (!a || a->size() != 3)
        return fb;
    glm::vec3 out = fb;
    for (size_t i = 0; i < 3; ++i)
        out[int(i)] = float((*a)[i].value_or(double(fb[int(i)])));
    return out;
}

} // namespace

bool CombatVocabulary::load(const std::string& tomlPath)
{
    mDamageTypes.clear();
    mSchools.clear();

    toml::parse_result parsed = toml::parse_file(tomlPath);
    if (!parsed) {
        eng::log::error("CombatVocabulary: %s: %s", tomlPath.c_str(),
                        std::string(parsed.error().description()).c_str());
        return false;
    }
    const toml::table& root = parsed.table();

    if (const toml::array* types = root["damage_type"].as_array()) {
        for (const toml::node& node : *types) {
            const toml::table* entry = node.as_table();
            if (!entry) continue;
            const std::string id = (*entry)["id"].value_or(std::string());
            if (id.empty()) {
                eng::log::error("CombatVocabulary: damage_type without an id");
                continue;
            }
            if (damageType(id) != kInvalidDamageType) {
                eng::log::error("CombatVocabulary: duplicate damage type '%s'",
                                id.c_str());
                continue;
            }
            if (mDamageTypes.size() >= size_t(kMaxDamageTypes)) {
                eng::log::error(
                    "CombatVocabulary: more than %d damage types; '%s' and "
                    "everything after it is ignored",
                    kMaxDamageTypes, id.c_str());
                break;
            }
            mDamageTypes.push_back(
                {id, (*entry)["bypasses_mitigation"].value_or(false)});
        }
    }
    if (mDamageTypes.empty()) {
        eng::log::error("CombatVocabulary: %s defines no damage types",
                        tomlPath.c_str());
        return false;
    }

    if (const toml::array* schools = root["school"].as_array()) {
        for (const toml::node& node : *schools) {
            const toml::table* entry = node.as_table();
            if (!entry) continue;
            const std::string id = (*entry)["id"].value_or(std::string());
            if (id.empty()) {
                eng::log::error("CombatVocabulary: school without an id");
                continue;
            }
            const std::string damageId =
                (*entry)["damage_type"].value_or(std::string());
            const DamageTypeId damage = damageType(damageId);
            if (damage == kInvalidDamageType) {
                eng::log::error(
                    "CombatVocabulary: school '%s' names damage type '%s', "
                    "which is not defined; skipped",
                    id.c_str(), damageId.c_str());
                continue;
            }
            MagicSchoolDef def;
            def.id = id;
            def.damage = damage;
            const eng::EnchantmentPalette defaults;
            def.palette.colour = vec4Or((*entry)["colour"], defaults.colour);
            def.palette.scrollDirection =
                vec3Or((*entry)["scroll"], defaults.scrollDirection);
            def.palette = eng::sanitizeEnchantmentPalette(def.palette);
            mSchools.push_back(std::move(def));
        }
    }

    const std::string burn =
        root["status"]["burn_damage_type"].value_or(std::string());
    mBurnDamage = damageType(burn);
    if (mBurnDamage == kInvalidDamageType) {
        eng::log::error(
            "CombatVocabulary: status.burn_damage_type '%s' is not a defined "
            "damage type; burns fall back to '%s'",
            burn.c_str(), mDamageTypes.front().id.c_str());
        mBurnDamage = 0;
    }
    mDefaultEnchantStrength =
        float(root["status"]["default_enchant_strength"].value_or(0.75));

    eng::log::info("CombatVocabulary: %d damage types, %d schools of magic",
                   int(mDamageTypes.size()), int(mSchools.size()));
    return true;
}

DamageTypeId CombatVocabulary::damageType(std::string_view id) const
{
    for (size_t i = 0; i < mDamageTypes.size(); ++i)
        if (mDamageTypes[i].id == id)
            return DamageTypeId(i);
    return kInvalidDamageType;
}

const DamageTypeDef* CombatVocabulary::damageTypeDef(DamageTypeId type) const
{
    return size_t(type) < mDamageTypes.size() ? &mDamageTypes[type] : nullptr;
}

bool CombatVocabulary::bypassesMitigation(DamageTypeId type) const
{
    const DamageTypeDef* def = damageTypeDef(type);
    return def && def->bypassesMitigation;
}

const MagicSchoolDef* CombatVocabulary::school(std::string_view id) const
{
    for (const MagicSchoolDef& def : mSchools)
        if (def.id == id)
            return &def;
    return nullptr;
}

const MagicSchoolDef* CombatVocabulary::defaultSchool() const
{
    return mSchools.empty() ? nullptr : &mSchools.front();
}

eng::EnchantmentPalette CombatVocabulary::palette(std::string_view id) const
{
    if (const MagicSchoolDef* def = school(id))
        return def->palette;
    if (const MagicSchoolDef* fallback = defaultSchool())
        return fallback->palette;
    return {};
}

} // namespace game
