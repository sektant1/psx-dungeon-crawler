#include <eng/particles/ParticleLibrary.h>

#include <eng/Renderer.h>
#include <eng/Log.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <cstdio>
#include <fstream>
#include <string>
#include <system_error>

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
eng::ParticleVisualRole visualRole(const std::string& role,
                                   const std::string& effectName) {
    if (role == "critical") return eng::ParticleVisualRole::Critical;
    if (role == "gameplay") return eng::ParticleVisualRole::Gameplay;
    if (role == "feedback") return eng::ParticleVisualRole::Feedback;
    if (role == "ambient") return eng::ParticleVisualRole::Ambient;
    eng::log::warn("ParticleLibrary: effect '%s' has unknown role '%s'; "
                   "using feedback",
                   effectName.c_str(), role.c_str());
    return eng::ParticleVisualRole::Feedback;
}
eng::ParticleRenderMode renderMode(const std::string& mode,
                                   const std::string& effectName) {
    if (mode == "sprite") return eng::ParticleRenderMode::Sprite;
    if (mode == "voxel")  return eng::ParticleRenderMode::Voxel;
    eng::log::warn("ParticleLibrary: effect '%s' has unknown render_mode '%s'; "
                   "using sprite",
                   effectName.c_str(), mode.c_str());
    return eng::ParticleRenderMode::Sprite;
}
eng::ParticleCollideResponse collideResponse(const std::string& response,
                                             const std::string& effectName) {
    if (response == "none")   return eng::ParticleCollideResponse::None;
    if (response == "die")    return eng::ParticleCollideResponse::Die;
    if (response == "bounce") return eng::ParticleCollideResponse::Bounce;
    if (response == "decal")  return eng::ParticleCollideResponse::Decal;
    eng::log::warn("ParticleLibrary: effect '%s' has unknown collide '%s'; "
                   "using none",
                   effectName.c_str(), response.c_str());
    return eng::ParticleCollideResponse::None;
}

// --- serialisation ------------------------------------------------------
// Names must match the parser above exactly; these are the only two places
// that know the string spelling of an enum value.
const char* roleName(eng::ParticleVisualRole role) {
    switch (role) {
        case eng::ParticleVisualRole::Critical: return "critical";
        case eng::ParticleVisualRole::Gameplay: return "gameplay";
        case eng::ParticleVisualRole::Feedback: return "feedback";
        case eng::ParticleVisualRole::Ambient:  return "ambient";
    }
    return "feedback";
}
const char* modeName(eng::ParticleRenderMode mode) {
    return mode == eng::ParticleRenderMode::Voxel ? "voxel" : "sprite";
}
const char* collideName(eng::ParticleCollideResponse response) {
    switch (response) {
        case eng::ParticleCollideResponse::None:   return "none";
        case eng::ParticleCollideResponse::Die:    return "die";
        case eng::ParticleCollideResponse::Bounce: return "bounce";
        case eng::ParticleCollideResponse::Decal:  return "decal";
    }
    return "none";
}

// A float needs 9 significant decimal digits to survive a text round trip, and
// %g drops the trailing zeros that would make the file noisy. Values that are
// whole numbers still get a ".0" so the result stays readable as a float.
std::string f2s(float v) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.9g", double(v));
    std::string s = buf;
    if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
    return s;
}
std::string v3s(const glm::vec3& v) {
    return "[" + f2s(v.x) + ", " + f2s(v.y) + ", " + f2s(v.z) + "]";
}
std::string v4s(const glm::vec4& v) {
    return "[" + f2s(v.x) + ", " + f2s(v.y) + ", " + f2s(v.z) + ", " +
           f2s(v.w) + "]";
}
// TOML basic strings: only the two characters that would end or escape one can
// appear in a path or a name here, so a full escaper would be dead code.
std::string q(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}
} // namespace

namespace eng {

bool ParticleLibrary::parseFile(const std::string& path,
                                std::vector<ParticleEffectDesc>& out)
{
    toml::parse_result parsed = toml::parse_file(path);
    if (!parsed) { eng::log::error("ParticleLibrary: parse failed: %s", path.c_str()); return false; }
    const toml::array* effects = parsed.table()["effect"].as_array();
    if (!effects) { eng::log::error("ParticleLibrary: no [[effect]] array"); return false; }

    out.clear();
    for (const toml::node& node : *effects) {
        const toml::table* e = node.as_table();
        if (!e) continue;
        eng::ParticleEffectDesc d;
        d.name        = (*e)["name"].value_or(std::string());
        d.material    = (*e)["material"].value_or(std::string());
        // Preferred over `material`: a texture stem resolved to a generated
        // material later. Both may be present; the resolver decides, not us.
        d.texture     = (*e)["texture"].value_or(std::string());
        d.renderMode  = renderMode(
            (*e)["render_mode"].value_or(std::string("sprite")), d.name);
        d.baseWidth   = num(*e, "base_width", 0.14f);
        d.baseHeight  = num(*e, "base_height", 0.14f);
        d.quota       = int(num(*e, "quota", 48));
        d.loop        = (*e)["loop"].value_or(true);
        d.burstCount  = num(*e, "burst_count", 0.0f);
        d.qualityWeight     = num(*e, "quality_weight", 1.0f);
        d.visualRole = visualRole(
            (*e)["visual_role"].value_or(std::string("feedback")), d.name);
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
        d.drag = num(*e, "drag", 0.0f);

        // Collision is opt-in per effect because tracing is globally budgeted:
        // an effect that never asks is never queued and costs nothing.
        d.collideResponse = collideResponse(
            (*e)["collide"].value_or(std::string("none")), d.name);
        d.restitution = num(*e, "restitution", 0.35f);
        d.friction    = num(*e, "friction", 0.20f);
        d.decalProfile = (*e)["decal_profile"].value_or(std::string());

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
        out.push_back(std::move(d));
    }
    return true;
}

void ParticleLibrary::adopt(Renderer& r,
                            std::vector<ParticleEffectDesc>& parsed)
{
    mDescs.clear();
    mIds.clear();
    mByName.clear();
    for (ParticleEffectDesc& d : parsed) {
        if (mByName.count(d.name)) {
            log::error("ParticleLibrary: duplicate effect '%s'; ignoring later definition",
                       d.name.c_str());
            continue;
        }
        // The Renderer keys effects by name, so this returns the *same* id an
        // earlier load handed out. That is what lets a reload swap the desc
        // under live instances instead of orphaning them.
        const ParticleEffectId registered = r.registerParticleEffect(d);
        if (!registered.valid()) {
            log::error("ParticleLibrary: invalid effect '%s'; skipping",
                       d.name.c_str());
            continue;
        }
        const size_t idx = mDescs.size();
        mDescs.push_back(std::move(d));
        mIds.push_back(registered);
        mByName[mDescs.back().name] = idx;
    }
}

bool ParticleLibrary::load(Renderer& r, const std::string& path)
{
    std::vector<ParticleEffectDesc> parsed;
    if (!parseFile(path, parsed)) return false;

    adopt(r, parsed);
    mPath = path;
    std::error_code ec;
    mMtime = std::filesystem::last_write_time(path, ec);

    eng::log::info("ParticleLibrary: loaded %zu effects", mDescs.size());
    return true;
}

bool ParticleLibrary::reloadIfChanged(Renderer& r)
{
    if (mPath.empty()) return false;

    // A missing or unreadable file is not an error worth logging every frame:
    // an editor mid-save can transiently show either. We simply wait.
    std::error_code ec;
    const std::filesystem::file_time_type mtime =
        std::filesystem::last_write_time(mPath, ec);
    if (ec || mtime == mMtime) return false;

    // Stamp the mtime before parsing, so a file that is genuinely broken is
    // reported once rather than on every subsequent frame.
    mMtime = mtime;

    std::vector<ParticleEffectDesc> parsed;
    if (!parseFile(mPath, parsed)) return false;
    adopt(r, parsed);
    eng::log::info("ParticleLibrary: reloaded %zu effects from %s",
                   mDescs.size(), mPath.c_str());
    return true;
}

bool ParticleLibrary::save(const std::string& path) const
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        log::error("ParticleLibrary: cannot write '%s'", path.c_str());
        return false;
    }

    out << "# Generated by the particle editor. Hand edits are preserved in\n"
           "# value only: comments and ordering of keys are rewritten on save.\n";

    for (const ParticleEffectDesc& d : mDescs) {
        out << "\n[[effect]]\n";
        out << "name = " << q(d.name) << "\n";
        out << "visual_role = " << q(roleName(d.visualRole)) << "\n";
        // Both material and texture are written even when empty: which one the
        // effect uses is a meaningful authoring choice, and an omitted key
        // would read as "not decided yet".
        out << "material = " << q(d.material) << "\n";
        out << "texture = " << q(d.texture) << "\n";
        out << "render_mode = " << q(modeName(d.renderMode)) << "\n";
        out << "base_width = " << f2s(d.baseWidth) << "\n";
        out << "base_height = " << f2s(d.baseHeight) << "\n";
        out << "quota = " << d.quota << "\n";
        out << "loop = " << (d.loop ? "true" : "false") << "\n";
        out << "burst_count = " << f2s(d.burstCount) << "\n";
        out << "quality_weight = " << f2s(d.qualityWeight) << "\n";
        out << "rotation_jitter_deg = " << f2s(d.rotationJitterDeg) << "\n";
        out << "hue_jitter = " << f2s(d.hueJitter) << "\n";
        out << "scale_jitter = " << f2s(d.scaleJitter) << "\n";
        out << "soft_depth_fade = " << (d.softDepthFade ? "true" : "false") << "\n";
        out << "local_space = " << (d.localSpace ? "true" : "false") << "\n";
        out << "acceleration = " << v3s(d.acceleration) << "\n";
        out << "drag = " << f2s(d.drag) << "\n";
        out << "collide = " << q(collideName(d.collideResponse)) << "\n";
        out << "restitution = " << f2s(d.restitution) << "\n";
        out << "friction = " << f2s(d.friction) << "\n";
        out << "decal_profile = " << q(d.decalProfile) << "\n";

        for (const ParticleEmitterDesc& em : d.emitters) {
            out << "[[effect.emitter]]\n";
            out << "shape = "
                << q(em.shape == ParticleEmitterShape::Box ? "box" : "point")
                << "\n";
            out << "box_size = " << v3s(em.boxSize) << "\n";
            out << "position = " << v3s(em.position) << "\n";
            out << "direction = " << v3s(em.direction) << "\n";
            out << "angle = " << f2s(em.angleDegrees) << "\n";
            out << "emission_rate = " << f2s(em.emissionRate) << "\n";
            out << "ttl_min = " << f2s(em.ttlMin) << "\n";
            out << "ttl_max = " << f2s(em.ttlMax) << "\n";
            out << "velocity_min = " << f2s(em.velocityMin) << "\n";
            out << "velocity_max = " << f2s(em.velocityMax) << "\n";
            out << "start_colour = " << v4s(em.startColour) << "\n";
        }
        for (const ColourStop& stop : d.colourRamp) {
            out << "[[effect.colour_ramp]]\n";
            out << "t = " << f2s(stop.t) << "\n";
            out << "rgba = " << v4s(stop.rgba) << "\n";
        }
        for (const SizeStop& stop : d.sizeRamp) {
            out << "[[effect.size_ramp]]\n";
            out << "t = " << f2s(stop.t) << "\n";
            out << "scale = " << f2s(stop.scale) << "\n";
        }
    }

    if (!out) {
        log::error("ParticleLibrary: write failed for '%s'", path.c_str());
        return false;
    }
    log::info("ParticleLibrary: wrote %zu effects to %s", mDescs.size(),
              path.c_str());
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

void ParticleLibrary::reregisterAll(Renderer& r) {
    mIds.resize(mDescs.size());
    for (size_t i = 0; i < mDescs.size(); ++i)
        mIds[i] = r.registerParticleEffect(mDescs[i]);
}

} // namespace eng
