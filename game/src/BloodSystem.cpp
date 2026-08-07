#include "BloodSystem.h"

#include <eng/Log.h>
#include <eng/Renderer.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace game {
namespace {

float num(const toml::table& t, const char* k, float d)
{
    return float(t[k].value_or(double(d)));
}

} // namespace

// Severity is the one knob a call site is expected to pass, so it has to map to
// something meaningful without the caller knowing any effect names.
float bloodSeverityScale(BloodSeverity severity)
{
    switch (severity) {
        case BloodSeverity::Light: return 0.45f;
        case BloodSeverity::Normal: return 1.0f;
        case BloodSeverity::Heavy: return 2.1f;
    }
    return 1.0f;
}

BloodSeverity bloodSeverityFor(float damageDealt, float maxHealth, bool killed)
{
    if (killed)
        return BloodSeverity::Heavy;
    if (maxHealth <= 0.0f)
        return BloodSeverity::Normal;
    const float fraction = damageDealt / maxHealth;
    // A twentieth of the bar is a graze; a third of it takes a limb's worth.
    if (fraction < 0.05f)
        return BloodSeverity::Light;
    if (fraction < 0.30f)
        return BloodSeverity::Normal;
    return BloodSeverity::Heavy;
}

bool parseBloodDefinitions(const std::string& tomlPath, BloodDefinitions& out)
{
    const toml::parse_result parsed = toml::parse_file(tomlPath);
    if (!parsed) {
        eng::log::warn("BloodSystem: cannot parse '%s' (%s); blood disabled",
                       tomlPath.c_str(),
                       std::string(parsed.error().description()).c_str());
        return false;
    }
    const toml::table& root = parsed.table();

    if (const toml::array* profiles = root["profile"].as_array()) {
        for (const toml::node& node : *profiles) {
            const toml::table* t = node.as_table();
            if (!t) continue;
            const std::string id = (*t)["id"].value_or(std::string{});
            if (id.empty()) {
                eng::log::warn("BloodSystem: profile without an id, skipped");
                continue;
            }
            BloodProfile p;
            p.sprayEffect = (*t)["spray_effect"].value_or(std::string{});
            p.gibEffect = (*t)["gib_effect"].value_or(std::string{});
            p.mistEffect = (*t)["mist_effect"].value_or(std::string{});
            p.dripEffect = (*t)["drip_effect"].value_or(std::string{});
            p.dripHpFraction =
                std::clamp(num(*t, "drip_hp_fraction", p.dripHpFraction),
                           0.0f, 1.0f);
            p.amountScale = std::max(0.0f, num(*t, "amount_scale",
                                               p.amountScale));
            p.damageBias =
                std::clamp(num(*t, "damage_bias", p.damageBias), 0.0f, 1.0f);
            out.profiles[id] = p;
        }
    }

    return true;
}

bool BloodSystem::load(eng::Renderer& renderer, const std::string& tomlPath)
{
    BloodDefinitions defs;
    if (!parseBloodDefinitions(tomlPath, defs)) return false;

    mProfiles = std::move(defs.profiles);
    mLoaded = !mProfiles.empty();
    eng::log::info("BloodSystem: %zu profiles from %s", mProfiles.size(),
                   tomlPath.c_str());
    return mLoaded;
}

const BloodProfile* BloodSystem::profile(const std::string& id) const
{
    auto it = mProfiles.find(id);
    if (it != mProfiles.end())
        return &it->second;
    // Everything bleeds unless it says otherwise. An enemy that names no
    // profile -- or names one that was removed or misspelled -- used to produce
    // no blood at all, which reads as a missing hit rather than as a design
    // choice. "none" is the explicit opt-out for things that genuinely do not
    // bleed, and it is spelled out rather than being the accidental default.
    if (id == kNoBlood)
        return nullptr;
    it = mProfiles.find(kDefaultBlood);
    return it == mProfiles.end() ? nullptr : &it->second;
}

std::vector<std::string> BloodSystem::profileIds() const
{
    std::vector<std::string> ids;
    ids.reserve(mProfiles.size());
    for (const auto& [id, unused] : mProfiles) ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    return ids;
}

void BloodSystem::spawnHit(eng::Renderer& renderer, const std::string& id,
                           glm::vec3 point, glm::vec3 normal,
                           glm::vec3 damageDir, BloodSeverity severity) const
{
    const BloodProfile* p = profile(id);
    if (!p) return;

    // Blend the surface normal with the direction the blow travelled. Spraying
    // purely along the normal makes every wound look like it was hit head-on;
    // spraying purely along the damage direction buries the spray in the wall
    // behind the target.
    glm::vec3 out = normal;
    if (glm::dot(normal, normal) < 1e-6f) out = glm::vec3(0.0f, 1.0f, 0.0f);
    out = glm::normalize(out);
    if (glm::dot(damageDir, damageDir) > 1e-6f) {
        const glm::vec3 blow = glm::normalize(damageDir);
        out = glm::normalize(glm::mix(out, blow, p->damageBias));
    }

    const float scale = bloodSeverityScale(severity) * p->amountScale;

    eng::ParticleSpawnOptions options;
    options.amountScale = scale;
    options.sizeScale = 1.0f + 0.25f * (scale - 1.0f);

    if (!p->sprayEffect.empty())
        renderer.spawnParticles(p->sprayEffect, point, options);

    // Gibs are the readable signal that something died rather than flinched, so
    // they are gated on severity instead of scaled by it.
    if (severity == BloodSeverity::Heavy && !p->gibEffect.empty())
        renderer.spawnParticles(p->gibEffect, point, options);

    if (!p->mistEffect.empty())
        renderer.spawnParticles(p->mistEffect, point, options);

    // No mark is stamped here. Blood is particles and voxel chunks only: the
    // spray and the gibs collide and settle where they land, and that IS the
    // mark.
}

void BloodSystem::updateDrip(eng::Renderer& renderer, uint32_t bleeder,
                             const std::string& id, glm::vec3 wound,
                             float healthFraction)
{
    const BloodProfile* p = profile(id);
    const bool bleeding = p && !p->dripEffect.empty() &&
                          healthFraction > 0.0f &&
                          healthFraction <= p->dripHpFraction;
    auto it = mDrips.find(bleeder);
    if (!bleeding) {
        if (it != mDrips.end() && it->second.fx.valid()) {
            // Stop emitting, but leave the drops already falling: they are
            // what marks the floor, and cutting them off in mid-air reads as a
            // bug.
            renderer.stopParticles(it->second.fx);
            it->second.fx = {};
        }
        return;
    }
    if (it == mDrips.end()) {
        Drip drip;
        drip.node = renderer.createNode(eng::kRootNode, wound);
        it = mDrips.emplace(bleeder, drip).first;
    }
    renderer.setPosition(it->second.node, wound);
    if (!it->second.fx.valid())
        it->second.fx = renderer.spawnParticles(p->dripEffect, it->second.node);
}

void BloodSystem::stopDrip(eng::Renderer& renderer, uint32_t bleeder)
{
    auto it = mDrips.find(bleeder);
    if (it == mDrips.end()) return;
    if (it->second.fx.valid())
        renderer.despawnParticles(it->second.fx);
    if (it->second.node.valid())
        renderer.destroyNode(it->second.node);
    mDrips.erase(it);
}

} // namespace game
