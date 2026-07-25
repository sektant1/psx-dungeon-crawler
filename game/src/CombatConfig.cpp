#include "CombatConfig.h"

#include <eng/Input.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>


#include <array>
#include <cstring>

// ---- toml load -------------------------------------------------------------

namespace {

float num(const toml::table& t, const char* key, float fallback) {
    return float(t[key].value_or(double(fallback)));
}

std::string str(const toml::table& t, const char* key, const std::string& fallback) {
    return t[key].value_or(fallback);
}

glm::vec3 col(const toml::table& t, const char* key, glm::vec3 fallback) {
    const toml::array* a = t[key].as_array();
    if (!a || a->size() != 3) return fallback;
    return glm::vec3(float((*a)[0].value_or(double(fallback.x))),
                     float((*a)[1].value_or(double(fallback.y))),
                     float((*a)[2].value_or(double(fallback.z))));
}

// Look up an action's first key name in the [bindings] table (string or array).
std::string binding(const toml::table& root, const std::string& action,
                    const std::string& fallback) {
    const toml::table* b = root["bindings"].as_table();
    if (!b) return fallback;
    if (const toml::node* n = b->get(action)) {
        if (auto s = n->value<std::string>()) return *s;
        if (const toml::array* a = n->as_array(); a && a->size() > 0)
            return (*a)[0].value_or(fallback);
    }
    return fallback;
}

} // namespace

bool CombatConfig::load(const std::string& tomlPath) {
    toml::parse_result parsed = toml::parse_file(tomlPath);
    if (!parsed) return false;
    const toml::table& root = parsed.table();

    if (const toml::table* c = root["combat"]["fireball"].as_table()) {
        fireball.speed         = num(*c, "speed", fireball.speed);
        fireball.radius        = num(*c, "radius", fireball.radius);
        fireball.mass          = num(*c, "mass", fireball.mass);
        fireball.ttl           = num(*c, "ttl", fireball.ttl);
        fireball.impactImpulse = num(*c, "impact_impulse", fireball.impactImpulse);
        fireball.lightColour   = col(*c, "light_colour", fireball.lightColour);
        fireball.lightRange    = num(*c, "light_range", fireball.lightRange);
        fireball.trailParticle  = str(*c, "trail_particle", fireball.trailParticle);
        fireball.muzzleParticle = str(*c, "muzzle_particle", fireball.muzzleParticle);
        fireball.impactParticle = str(*c, "impact_particle", fireball.impactParticle);
    }
    if (const toml::table* c = root["combat"]["beam"].as_table()) {
        beam.range      = num(*c, "range", beam.range);
        beam.width      = num(*c, "width", beam.width);
        beam.impulse    = num(*c, "impulse", beam.impulse);
        beam.segmentTtl = num(*c, "segment_ttl", beam.segmentTtl);
        beam.lightColour = col(*c, "light_colour", beam.lightColour);
        beam.lightRange  = num(*c, "light_range", beam.lightRange);
        beam.coreParticle   = str(*c, "core_particle", beam.coreParticle);
        beam.impactParticle = str(*c, "impact_particle", beam.impactParticle);
    }
    if (const toml::table* c = root["combat"]["arrow"].as_table()) {
        arrow.speed      = num(*c, "speed", arrow.speed);
        arrow.radius     = num(*c, "radius", arrow.radius);
        arrow.halfHeight = num(*c, "half_height", arrow.halfHeight);
        arrow.mass       = num(*c, "mass", arrow.mass);
        arrow.ttl        = num(*c, "ttl", arrow.ttl);
    }
    if (const toml::table* c = root["combat"]["melee"].as_table()) {
        melee.reach   = num(*c, "reach", melee.reach);
        melee.radius  = num(*c, "radius", melee.radius);
        melee.impulse = num(*c, "impulse", melee.impulse);
        melee.windup  = num(*c, "windup", melee.windup);
        melee.active  = num(*c, "active", melee.active);
    }

    // Current hotkeys mirror [bindings] so the UI shows/edit the live key.
    fireball.key = binding(root, fireball.action, fireball.key);
    beam.key     = binding(root, beam.action, beam.key);
    arrow.key    = binding(root, arrow.action, arrow.key);
    return true;
}
