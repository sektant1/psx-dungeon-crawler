#include "Ammunition.h"

#include <eng/Log.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>
#include <cmath>

namespace game {
namespace {

float number(const toml::table& table, const char* key, float fallback)
{
    return float(table[key].value_or(double(fallback)));
}

glm::vec3 vector3(const toml::table& table, const char* key, glm::vec3 fallback)
{
    const toml::array* values = table[key].as_array();
    if (!values || values->size() != 3)
        return fallback;
    return {float((*values)[0].value_or(double(fallback.x))),
            float((*values)[1].value_or(double(fallback.y))),
            float((*values)[2].value_or(double(fallback.z)))};
}

bool finite(float value) { return std::isfinite(value); }

} // namespace

glm::vec3 WindState::at(float time) const
{
    if (speed <= 0.0f && gustSpeed <= 0.0f)
        return glm::vec3(0.0f);
    glm::vec3 unit = direction;
    if (glm::dot(unit, unit) < 0.000001f)
        return glm::vec3(0.0f);
    unit = glm::normalize(unit);
    // Deterministic gust: a sinusoid on level time, so two replays of the same
    // shot agree and a long-range hit is reproducible. A random gust would make
    // the one shot that matters unrepeatable, which is the opposite of what a
    // wind system is for.
    const float gust =
        gustPeriod > 0.0f
            ? gustSpeed * std::sin(time * 6.28318530718f / gustPeriod)
            : 0.0f;
    return unit * (speed + gust);
}

float applyDrag(float speed, float drag, float dt)
{
    if (drag <= 0.0f || dt <= 0.0f)
        return speed;
    // Exponential decay. A linear one (`speed -= drag * speed0 * dt`) reaches
    // zero and keeps going, and clamping that leaves a round stopped dead in
    // mid-air -- which looks like the projectile hit an invisible wall.
    return speed * std::exp(-drag * dt);
}

PenetrationResult resolvePenetration(const Cartridge& round, int armourClass,
                                     float armourDurability, float roll)
{
    PenetrationResult result;
    armourClass = std::max(0, armourClass);
    armourDurability = std::clamp(armourDurability, 0.0f, 1.0f);

    // Unarmoured is the simple case and by far the common one, so it does not
    // pay the rest of this function.
    if (armourClass <= 0) {
        result.penetrated = true;
        result.damage = round.damage;
        result.speedRetained = 0.0f; // flesh stops a round; it does not exit
        if (roll < round.fragmentation) {
            result.fragmented = true;
            // Fragmenting is what makes a light fast round worth carrying: it
            // does not defeat armour, but against a soft target it does more
            // than its damage number suggests.
            result.damage *= 1.6f;
        }
        return result;
    }

    // Worn armour protects less. Without this a plate the player cannot defeat
    // is a permanent wall, and the honest answer to a wall is not "fire more".
    const float effective =
        float(armourClass) * (0.45f + 0.55f * armourDurability);

    if (float(round.penetration) >= effective) {
        result.penetrated = true;
        // How marginal the penetration was decides what is left of the round. A
        // round that barely gets through arrives slow and deformed; one that
        // vastly outclasses the plate arrives almost intact.
        const float margin = effective > 0.0f
                                 ? float(round.penetration) / effective
                                 : 2.0f;
        const float retained = std::clamp(0.35f + 0.35f * (margin - 1.0f),
                                          0.35f, 0.95f);
        result.damage = round.damage * retained;
        result.speedRetained = retained;
        result.armourDamage = round.armourDamage * 0.5f;
        if (roll < round.fragmentation * retained) {
            result.fragmented = true;
            result.damage *= 1.4f;
        }
        return result;
    }

    // Stopped. Blunt trauma rather than nothing: being shot in a plate still
    // hurts, and a hit that did literally nothing reads as not registering.
    result.penetrated = false;
    const float shortfall =
        effective > 0.0f ? float(round.penetration) / effective : 0.0f;
    result.damage = round.damage * 0.12f * std::clamp(shortfall, 0.0f, 1.0f);
    result.armourDamage = round.armourDamage;
    result.speedRetained = 0.0f;
    return result;
}

bool validCartridge(const Cartridge& round)
{
    if (round.id.empty() || round.calibre.empty())
        return false;
    if (!finite(round.muzzleVelocity) || round.muzzleVelocity <= 0.0f)
        return false;
    if (!finite(round.mass) || round.mass <= 0.0f)
        return false;
    if (!finite(round.drag) || round.drag < 0.0f)
        return false;
    if (!finite(round.gravity) || !finite(round.windFactor))
        return false;
    if (!finite(round.damage) || round.damage < 0.0f)
        return false;
    if (round.penetration < 0 || round.penetration > 10)
        return false;
    for (const float chance :
         {round.armourDamage, round.fragmentation, round.ricochet})
        if (!finite(chance) || chance < 0.0f || chance > 1.0f)
            return false;
    return true;
}

namespace {

bool parseTable(const toml::table& root, std::vector<Cartridge>& out,
                WindState& wind)
{
    if (const toml::table* windTable = root["wind"].as_table()) {
        wind.direction = vector3(*windTable, "direction", wind.direction);
        wind.speed = number(*windTable, "speed", wind.speed);
        wind.gustSpeed = number(*windTable, "gust_speed", wind.gustSpeed);
        wind.gustPeriod = number(*windTable, "gust_period", wind.gustPeriod);
    }

    const toml::array* rounds = root["cartridge"].as_array();
    if (!rounds)
        return false;

    std::vector<Cartridge> parsed;
    parsed.reserve(rounds->size());
    for (const toml::node& node : *rounds) {
        const toml::table* table = node.as_table();
        if (!table)
            return false;
        Cartridge round;
        round.id = (*table)["id"].value_or(std::string{});
        round.displayName = (*table)["name"].value_or(round.id);
        round.calibre = (*table)["calibre"].value_or(std::string{});
        round.muzzleVelocity =
            number(*table, "muzzle_velocity", round.muzzleVelocity);
        round.mass = number(*table, "mass", round.mass);
        round.drag = number(*table, "drag", round.drag);
        round.gravity = number(*table, "gravity", round.gravity);
        round.windFactor = number(*table, "wind_factor", round.windFactor);
        round.damage = number(*table, "damage", round.damage);
        round.penetration =
            int((*table)["penetration"].value_or(int64_t{round.penetration}));
        round.armourDamage =
            number(*table, "armour_damage", round.armourDamage);
        round.fragmentation =
            number(*table, "fragmentation", round.fragmentation);
        round.ricochet = number(*table, "ricochet", round.ricochet);
        round.tracer = (*table)["tracer"].value_or(round.tracer);

        if (!validCartridge(round))
            return false;
        // Two cartridges answering to one id is a content bug that shows up as
        // the wrong round being fired, so it fails at load.
        for (const Cartridge& existing : parsed)
            if (existing.id == round.id)
                return false;
        parsed.push_back(std::move(round));
    }
    out = std::move(parsed);
    return true;
}

} // namespace

bool AmmoLibrary::load(const std::string& tomlPath)
{
    const toml::parse_result parsed = toml::parse_file(tomlPath);
    if (!parsed || !parseTable(parsed.table(), mCartridges, mWind)) {
        eng::log::error("AmmoLibrary: invalid ammunition in '%s'",
                        tomlPath.c_str());
        return false;
    }
    return true;
}

bool AmmoLibrary::loadFromString(const char* tomlSource)
{
    const toml::parse_result parsed = toml::parse(tomlSource);
    return parsed && parseTable(parsed.table(), mCartridges, mWind);
}

const Cartridge* AmmoLibrary::find(std::string_view id) const
{
    for (const Cartridge& round : mCartridges)
        if (round.id == id)
            return &round;
    return nullptr;
}

std::vector<const Cartridge*>
AmmoLibrary::forCalibre(std::string_view calibre) const
{
    std::vector<const Cartridge*> out;
    for (const Cartridge& round : mCartridges)
        if (round.calibre == calibre)
            out.push_back(&round);
    return out;
}

} // namespace game
