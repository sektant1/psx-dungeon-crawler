#include <eng/particles/ParticleLibrary.h>

#include <eng/Renderer.h>
#include <eng/Log.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

namespace {
float num(const toml::table& t, const char* k, float d) {
    return float(t[k].value_or(double(d)));
}
glm::vec3 vec3(const toml::table& t, const char* k, glm::vec3 d) {
    const toml::array* a = t[k].as_array();
    if (!a || a->size() != 3) return d;
    return { float((*a)[0].value_or(double(d.x))),
             float((*a)[1].value_or(double(d.y))),
             float((*a)[2].value_or(double(d.z))) };
}
glm::vec4 vec4(const toml::array* a, glm::vec4 d) {
    if (!a || a->size() != 4) return d;
    return { float((*a)[0].value_or(double(d.x))),
             float((*a)[1].value_or(double(d.y))),
             float((*a)[2].value_or(double(d.z))),
             float((*a)[3].value_or(double(d.w))) };
}
} // namespace

namespace eng {

bool ParticleLibrary::load(Renderer& r, const std::string& path) {
    toml::parse_result parsed = toml::parse_file(path);
    if (!parsed) { eng::log::error("ParticleLibrary: parse failed: %s", path.c_str()); return false; }
    const toml::array* effects = parsed.table()["effect"].as_array();
    if (!effects) { eng::log::error("ParticleLibrary: no [[effect]] array"); return false; }

    mDescs.clear();
    mIds.clear();
    mByName.clear();
    for (const toml::node& node : *effects) {
        const toml::table* e = node.as_table();
        if (!e) continue;
        eng::ParticleEffectDesc d;
        d.name        = (*e)["name"].value_or(std::string());
        d.material    = (*e)["material"].value_or(std::string());
        d.baseWidth   = num(*e, "base_width", 0.14f);
        d.baseHeight  = num(*e, "base_height", 0.14f);
        d.quota       = int(num(*e, "quota", 48));
        d.loop        = (*e)["loop"].value_or(true);
        d.burstCount  = num(*e, "burst_count", 0.0f);
        d.qualityWeight     = num(*e, "quality_weight", 1.0f);
        d.rotationJitterDeg = num(*e, "rotation_jitter_deg", 0.0f);
        d.hueJitter   = num(*e, "hue_jitter", 0.0f);
        d.scaleJitter = num(*e, "scale_jitter", 0.0f);
        d.softDepthFade = (*e)["soft_depth_fade"].value_or(false);

        if (const toml::array* ems = (*e)["emitter"].as_array())
            for (const toml::node& en : *ems) {
                const toml::table* et = en.as_table();
                if (!et) continue;
                eng::ParticleEmitterDesc em;
                const std::string shape =
                    (*et)["shape"].value_or(std::string("point"));
                em.shape = shape == "box" ? ParticleEmitterShape::Box
                                           : ParticleEmitterShape::Point;
                em.boxSize     = vec3(*et, "box_size", {1,1,1});
                em.position    = vec3(*et, "position", {0,0,0});
                em.direction   = vec3(*et, "direction", {0,1,0});
                em.angleDegrees= num(*et, "angle", 20.0f);
                em.emissionRate= num(*et, "emission_rate", 20.0f);
                em.ttlMin      = num(*et, "ttl_min", 0.3f);
                em.ttlMax      = num(*et, "ttl_max", 0.6f);
                em.velocityMin = num(*et, "velocity_min", 0.3f);
                em.velocityMax = num(*et, "velocity_max", 0.7f);
                em.startColour = vec4((*et)["start_colour"].as_array(), glm::vec4(1.0f));
                d.emitters.push_back(em);
            }
        d.localSpace = (*e)["local_space"].value_or(false);
        d.acceleration = vec3(*e, "acceleration", {0,0,0});
        if (const toml::array* cr = (*e)["colour_ramp"].as_array())
            for (const toml::node& sn : *cr) {
                const toml::table* st = sn.as_table();
                if (!st) continue;
                d.colourRamp.push_back({ num(*st, "t", 0.0f),
                    vec4((*st)["rgba"].as_array(), glm::vec4(1.0f)) });
            }
        if (const toml::array* sr = (*e)["size_ramp"].as_array())
            for (const toml::node& sn : *sr) {
                const toml::table* st = sn.as_table();
                if (!st) continue;
                d.sizeRamp.push_back({ num(*st, "t", 0.0f), num(*st, "scale", 1.0f) });
            }

        if (mByName.count(d.name)) {
            log::error("ParticleLibrary: duplicate effect '%s'; ignoring later definition",
                       d.name.c_str());
            continue;
        }
        const ParticleEffectId registered = r.registerParticleEffect(d);
        if (!registered.valid()) {
            log::error("ParticleLibrary: invalid effect '%s'; skipping",
                       d.name.c_str());
            continue;
        }
        const size_t idx = mDescs.size();
        mDescs.push_back(d);
        mIds.push_back(registered);
        mByName[d.name] = idx;
    }
    eng::log::info("ParticleLibrary: loaded %zu effects", mDescs.size());
    return true;
}

ParticleEffectId ParticleLibrary::id(const std::string& name) const {
    auto it = mByName.find(name);
    return it == mByName.end() ? eng::ParticleEffectId{} : mIds[it->second];
}

void ParticleLibrary::reregister(Renderer& r, size_t index) {
    if (index < mDescs.size())
        mIds[index] = r.registerParticleEffect(mDescs[index]);
}

} // namespace eng
