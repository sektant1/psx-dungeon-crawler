#include <editor/content/SceneSource.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace game::content {
namespace {

using Json = nlohmann::json;

bool finite(const glm::vec3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

bool readVec3(const Json& value, glm::vec3& out)
{
    if (!value.is_array() || value.size() != 3)
        return false;
    for (std::size_t i = 0; i < 3; ++i)
        if (!value[i].is_number())
            return false;
    out = {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
    return finite(out);
}

bool parseTransform(const Json& entity, XformAuthor& out,
                    const std::string& location, std::string& error)
{
    if (!entity.contains("transform"))
        return true;
    const Json& source = entity["transform"];
    if (!source.is_object()) {
        error = location + "/transform must be an object";
        return false;
    }
    if (source.contains("position") &&
        !readVec3(source["position"], out.position)) {
        error = location + "/transform/position must be three finite numbers";
        return false;
    }
    if (source.contains("rotation_degrees") &&
        !readVec3(source["rotation_degrees"], out.rotationDegrees)) {
        error =
            location + "/transform/rotation_degrees must be three finite numbers";
        return false;
    }
    if (source.contains("scale") && !readVec3(source["scale"], out.scale)) {
        error = location + "/transform/scale must be three finite numbers";
        return false;
    }
    if (out.scale.x <= 0.0f || out.scale.y <= 0.0f || out.scale.z <= 0.0f) {
        error = location + "/transform/scale must be positive";
        return false;
    }
    return true;
}

bool parseCell(const Json& source, CellPlacement& out,
               const std::string& location, std::string& error)
{
    if (!source.is_object() || !source.contains("col") ||
        !source.contains("row") || !source["col"].is_number_integer() ||
        !source["row"].is_number_integer()) {
        error = location + "/cell needs integer col and row";
        return false;
    }
    out.col = source["col"].get<int>();
    out.row = source["row"].get<int>();
    const std::string edge = source.value("edge", std::string());
    if (edge.empty())
        out.edge = CellPlacement::Edge::None;
    else if (edge == "north")
        out.edge = CellPlacement::Edge::North;
    else if (edge == "east")
        out.edge = CellPlacement::Edge::East;
    else if (edge == "south")
        out.edge = CellPlacement::Edge::South;
    else if (edge == "west")
        out.edge = CellPlacement::Edge::West;
    else {
        error = location + "/cell/edge must be north, east, south or west";
        return false;
    }
    out.span = source.value("span", 1);
    out.yawQuarters = source.value("yaw_quarters", 0);
    out.level = source.value("level", 0.0f);
    if (out.span < 1 || out.yawQuarters < 0 || out.yawQuarters > 3 ||
        !std::isfinite(out.level)) {
        error = location + "/cell has an out-of-range span, yaw_quarters or level";
        return false;
    }
    return true;
}

bool parseLight(const Json& source, LightAuthor& out,
                const std::string& location, std::string& error)
{
    if (!source.is_object()) {
        error = location + " must be an object";
        return false;
    }
    const std::string type = source.value("type", std::string());
    if (type == "directional")
        out.type = LightAuthor::Type::Directional;
    else if (type == "point")
        out.type = LightAuthor::Type::Point;
    else {
        error = location + "/type must be 'directional' or 'point'";
        return false;
    }
    if (!source.contains("colour") || !readVec3(source["colour"], out.colour)) {
        error = location + "/colour must be three finite numbers";
        return false;
    }
    if (source.contains("range")) {
        if (!source["range"].is_number()) {
            error = location + "/range must be a number";
            return false;
        }
        out.range = source["range"].get<float>();
    }
    out.castShadows = source.value("cast_shadows", false);
    if (!std::isfinite(out.range) || out.range < 0.0f) {
        error = location + "/range must be a non-negative number";
        return false;
    }
    if (source.contains("animation")) {
        const Json& node = source["animation"];
        if (!node.is_object()) {
            error = location + "/animation must be an object";
            return false;
        }
        LightAnimAuthor animation;
        const std::string mode = node.value("mode", std::string("flicker"));
        if (mode == "steady")
            animation.mode = LightAnimAuthor::Mode::Steady;
        else if (mode == "flicker")
            animation.mode = LightAnimAuthor::Mode::Flicker;
        else if (mode == "pulse")
            animation.mode = LightAnimAuthor::Mode::Pulse;
        else {
            error = location + "/animation/mode must be 'steady', 'flicker' or "
                               "'pulse'";
            return false;
        }
        animation.speed = node.value("speed", animation.speed);
        animation.amount = node.value("amount", animation.amount);
        animation.phase = node.value("phase", animation.phase);
        if (!std::isfinite(animation.speed) || animation.speed < 0.0f ||
            !std::isfinite(animation.amount) || animation.amount < 0.0f ||
            animation.amount > 1.0f || !std::isfinite(animation.phase)) {
            error = location +
                    "/animation needs a non-negative speed, an amount in 0..1 "
                    "and a finite phase";
            return false;
        }
        out.animation = animation;
    }
    return true;
}

bool parseCamera(const Json& source, CameraAuthor& out,
                 const std::string& location, std::string& error)
{
    if (!source.is_object()) {
        error = location + " must be an object";
        return false;
    }
    const auto number = [&](const char* key, float& value) {
        if (!source.contains(key))
            return true;
        if (!source[key].is_number()) {
            error = location + "/" + key + " must be a number";
            return false;
        }
        value = source[key].get<float>();
        return true;
    };
    if (!number("fov_degrees", out.fovDegrees) ||
        !number("near_clip", out.nearClip) || !number("far_clip", out.farClip))
        return false;
    if (source.contains("priority")) {
        if (!source["priority"].is_number_integer()) {
            error = location + "/priority must be an integer";
            return false;
        }
        out.priority = source["priority"].get<int>();
    }
    out.active = source.value("active", true);

    // A camera with these wrong does not misdraw, it draws nothing -- a black
    // viewport that reads as a broken renderer rather than a bad number.
    if (!(out.fovDegrees > 0.0f) || !(out.fovDegrees < 180.0f)) {
        error = location + "/fov_degrees must be between 0 and 180";
        return false;
    }
    if (!(out.nearClip > 0.0f) || !(out.farClip > out.nearClip)) {
        error = location + "/near_clip must be positive and below far_clip";
        return false;
    }
    return true;
}

bool parseAudioEmitter(const Json& source, AudioEmitterAuthor& out,
                       const std::string& location, std::string& error)
{
    if (!source.is_object()) {
        error = location + " must be an object";
        return false;
    }
    if (source.contains("source")) {
        if (!source["source"].is_string()) {
            error = location + "/source must be a logical audio path";
            return false;
        }
        out.source = source["source"].get<std::string>();
    }
    if (source.contains("offset") && !readVec3(source["offset"], out.offset)) {
        error = location + "/offset must be three finite numbers";
        return false;
    }

    const std::string bus = source.value("bus", std::string("sfx"));
    static const std::pair<const char*, eng::AudioBus> kBuses[] = {
        {"master", eng::AudioBus::Master},
        {"music", eng::AudioBus::Music},
        {"ambience", eng::AudioBus::Ambience},
        {"dialogue", eng::AudioBus::Dialogue},
        {"weapons", eng::AudioBus::Weapons},
        {"sfx", eng::AudioBus::Sfx},
        {"ui", eng::AudioBus::Ui},
        {"warnings", eng::AudioBus::Warnings},
    };
    bool knownBus = false;
    for (const auto& [name, value] : kBuses) {
        if (bus != name)
            continue;
        out.bus = static_cast<int>(value);
        knownBus = true;
        break;
    }
    if (!knownBus) {
        error = location + "/bus is not a mixer bus";
        return false;
    }

    const std::string priority =
        source.value("priority", std::string("normal"));
    if (priority == "background")
        out.priority = static_cast<int>(eng::AudioPriority::Background);
    else if (priority == "low")
        out.priority = static_cast<int>(eng::AudioPriority::Low);
    else if (priority == "normal")
        out.priority = static_cast<int>(eng::AudioPriority::Normal);
    else if (priority == "important")
        out.priority = static_cast<int>(eng::AudioPriority::Important);
    else if (priority == "critical")
        out.priority = static_cast<int>(eng::AudioPriority::Critical);
    else {
        error = location + "/priority is not a supported voice priority";
        return false;
    }

    const auto number = [&](const char* key, float& value) {
        if (!source.contains(key))
            return true;
        if (!source[key].is_number()) {
            error = location + "/" + key + " must be a number";
            return false;
        }
        value = source[key].get<float>();
        if (!std::isfinite(value)) {
            error = location + "/" + key + " must be finite";
            return false;
        }
        return true;
    };
    const auto boolean = [&](const char* key, bool& value) {
        if (!source.contains(key))
            return true;
        if (!source[key].is_boolean()) {
            error = location + "/" + key + " must be true or false";
            return false;
        }
        value = source[key].get<bool>();
        return true;
    };
    if (!number("gain_db", out.gainDb) || !number("pitch", out.pitch) ||
        !number("min_distance", out.minDistance) ||
        !number("max_distance", out.maxDistance) ||
        !number("rolloff", out.rolloff) ||
        !number("doppler_factor", out.dopplerFactor) ||
        !boolean("loop", out.loop) || !boolean("stream", out.streaming) ||
        !boolean("spatial", out.spatialized) ||
        !boolean("autostart", out.playing) ||
        !boolean("stealable", out.stealable))
        return false;

    if (out.gainDb < -80.0f || out.gainDb > 12.0f || out.pitch <= 0.0f ||
        out.minDistance < 0.0f || out.maxDistance <= out.minDistance ||
        out.rolloff < 0.0f || out.dopplerFactor < 0.0f) {
        error = location + " has invalid gain, pitch or attenuation values";
        return false;
    }
    return true;
}

bool parseAudioListener(const Json& source, AudioListenerAuthor& out,
                        const std::string& location, std::string& error)
{
    if (!source.is_object()) {
        error = location + " must be an object";
        return false;
    }
    if (source.contains("priority")) {
        if (!source["priority"].is_number_integer()) {
            error = location + "/priority must be an integer";
            return false;
        }
        out.priority = source["priority"].get<int>();
    }
    if (source.contains("active")) {
        if (!source["active"].is_boolean()) {
            error = location + "/active must be true or false";
            return false;
        }
        out.active = source["active"].get<bool>();
    }
    if (out.priority < -100 || out.priority > 100) {
        error = location + "/priority must be between -100 and 100";
        return false;
    }
    return true;
}

// Every field optional and clamped, because these are shader inputs: a negative
// rimPower or an opacity of 40 is not a scene the renderer draws differently,
// it is one it draws wrongly, and the author would be looking at the model
// rather than at the number.
bool parseShader(const Json& source, ShaderAuthor& out,
                 const std::string& location, std::string& error)
{
    if (!source.is_object()) {
        error = location + " must be an object";
        return false;
    }
    if (source.contains("tint") && !readVec3(source["tint"], out.tint)) {
        error = location + "/tint must be three finite numbers";
        return false;
    }
    if (source.contains("rim_colour") &&
        !readVec3(source["rim_colour"], out.rimColour)) {
        error = location + "/rim_colour must be three finite numbers";
        return false;
    }
    const auto number = [&](const char* key, float& target, float lo,
                            float hi) -> bool {
        if (!source.contains(key))
            return true;
        if (!source[key].is_number()) {
            error = location + "/" + key + " must be a number";
            return false;
        }
        const float value = source[key].get<float>();
        if (!std::isfinite(value) || value < lo || value > hi) {
            error = location + "/" + key + " must be between " +
                    std::to_string(lo) + " and " + std::to_string(hi);
            return false;
        }
        target = value;
        return true;
    };
    return number("opacity", out.opacity, 0.0f, 1.0f) &&
           number("rim_strength", out.rimStrength, 0.0f, 8.0f) &&
           number("rim_power", out.rimPower, 0.05f, 64.0f) &&
           number("alpha_scissor", out.alphaScissor, 0.0f, 1.0f);
}

// Driven by the component's own field table, so the accepted keys and the
// editable ones cannot drift: adding a uniform to PortalParams adds it here.
bool parseFields(const Json& source, const eng::FieldSpan& fields, void* out,
                 const std::string& location, std::string& error)
{
    if (!source.is_object()) {
        error = location + " must be an object";
        return false;
    }
    for (auto it = source.begin(); it != source.end(); ++it) {
        const eng::Field* field = nullptr;
        for (int i = 0; i < fields.count; ++i)
            if (it.key() == fields.data[i].name)
                field = &fields.data[i];
        if (!field) {
            error = location + "/" + it.key() + " is not a field of this "
                    "component";
            return false;
        }
        void* at = eng::fieldPtr(out, *field);
        switch (field->type) {
        case eng::FieldType::Float: {
            if (!it.value().is_number()) {
                error = location + "/" + it.key() + " must be a number";
                return false;
            }
            const float v = it.value().get<float>();
            // Clamped to the range the field declares rather than rejected:
            // these are shader inputs, and the range is what the effect stays
            // coherent within, not a format rule.
            if (!std::isfinite(v)) {
                error = location + "/" + it.key() + " must be finite";
                return false;
            }
            *static_cast<float*>(at) =
                field->max > field->min
                    ? std::clamp(v, field->min, field->max) : v;
            break;
        }
        case eng::FieldType::Vec3:
        case eng::FieldType::Colour: {
            glm::vec3 v{0.0f};
            if (!readVec3(it.value(), v)) {
                error = location + "/" + it.key() +
                        " must be three finite numbers";
                return false;
            }
            *static_cast<glm::vec3*>(at) = v;
            break;
        }
        case eng::FieldType::Bool:
            if (!it.value().is_boolean()) {
                error = location + "/" + it.key() + " must be true or false";
                return false;
            }
            *static_cast<bool*>(at) = it.value().get<bool>();
            break;
        case eng::FieldType::Int:
            if (!it.value().is_number_integer()) {
                error = location + "/" + it.key() + " must be a whole number";
                return false;
            }
            *static_cast<int*>(at) = it.value().get<int>();
            break;
        case eng::FieldType::String:
            if (!it.value().is_string()) {
                error = location + "/" + it.key() + " must be a string";
                return false;
            }
            *static_cast<std::string*>(at) = it.value().get<std::string>();
            break;
        case eng::FieldType::Quat:
            error = location + "/" + it.key() + " is not authorable";
            return false;
        }
    }
    return true;
}

// A typed key-value bag: a script's props, or an entity's free-form
// properties. One decoder for both -- they are the same data, and the writer
// has one encoder for the same reason.
//
// Types are inferred from the JSON value -- boolean, number, string, three
// numbers as a vec3 -- except Entity, which is the tagged object
// { "entity": "name" } so the type survives a round trip and the inspector
// knows to offer a picker.
bool parseProps(const Json& source, std::vector<ScriptPropAuthor>& out,
                const std::string& where, std::string& error)
{
    if (!source.is_object()) {
        error = where + " must be an object";
        return false;
    }
    for (auto it = source.begin(); it != source.end(); ++it) {
        ScriptPropAuthor p;
        p.key = it.key();
        const Json& v = it.value();
        if (v.is_boolean()) {
            p.type = ScriptPropAuthor::Type::Bool;
            p.boolValue = v.get<bool>();
        } else if (v.is_number()) {
            const double n = v.get<double>();
            if (!std::isfinite(n)) {
                error = where + "/" + it.key() + " must be finite";
                return false;
            }
            p.type = ScriptPropAuthor::Type::Number;
            p.numberValue = float(n);
        } else if (v.is_string()) {
            p.type = ScriptPropAuthor::Type::String;
            p.stringValue = v.get<std::string>();
        } else if (v.is_array()) {
            if (!readVec3(v, p.vecValue)) {
                error = where + "/" + it.key() +
                        " must be three finite numbers";
                return false;
            }
            p.type = ScriptPropAuthor::Type::Vec3;
        } else if (v.is_object() && v.contains("entity") &&
                   v["entity"].is_string()) {
            p.type = ScriptPropAuthor::Type::Entity;
            p.stringValue = v["entity"].get<std::string>();
        } else {
            error = where + "/" + it.key() +
                    " must be a boolean, number, string, three numbers, "
                    "or { \"entity\": \"name\" }";
            return false;
        }
        out.push_back(std::move(p));
    }
    return true;
}

// Scripts cannot go through parseFields: that helper is driven by a FieldSpan,
// which describes a fixed layout, and this is a variable-length list of
// heterogeneous values. So it is parsed by hand, and the JSON schema is what
// keeps the accepted shape documented.
bool parseScripts(const Json& entity, std::vector<ScriptAuthor>& out,
                  const std::string& location, std::string& error)
{
    if (!entity.contains("scripts"))
        return true;
    const Json& list = entity["scripts"];
    if (!list.is_array()) {
        error = location + "/scripts must be an array";
        return false;
    }

    for (std::size_t i = 0; i < list.size(); ++i) {
        const Json& node = list[i];
        const std::string where = location + "/scripts/" + std::to_string(i);
        if (!node.is_object() || !node.contains("path") ||
            !node["path"].is_string()) {
            error = where + " needs a string 'path'";
            return false;
        }

        ScriptAuthor script;
        script.path = node["path"].get<std::string>();
        script.enabled = node.value("enabled", true);

        if (node.contains("props") &&
            !parseProps(node["props"], script.props, where + "/props", error))
            return false;
        out.push_back(std::move(script));
    }
    return true;
}

bool parseSpin(const Json& source, SpinAuthor& out, const std::string& location,
               std::string& error)
{
    if (!source.is_object()) {
        error = location + " must be an object";
        return false;
    }
    if (source.contains("axis") && !readVec3(source["axis"], out.axis)) {
        error = location + "/axis must be three finite numbers";
        return false;
    }
    if (source.contains("degrees_per_second")) {
        if (!source["degrees_per_second"].is_number()) {
            error = location + "/degrees_per_second must be a number";
            return false;
        }
        out.degreesPerSecond = source["degrees_per_second"].get<float>();
    }
    if (!std::isfinite(out.degreesPerSecond)) {
        error = location + "/degrees_per_second must be finite";
        return false;
    }
    if (glm::length(out.axis) <= 0.0f) {
        error = location + "/axis must not be zero";
        return false;
    }
    return true;
}

bool parseOrbit(const Json& source, OrbitAuthor& out,
                const std::string& location, std::string& error)
{
    if (!source.is_object()) {
        error = location + " must be an object";
        return false;
    }
    if (source.contains("centre") && !readVec3(source["centre"], out.centre)) {
        error = location + "/centre must be three finite numbers";
        return false;
    }
    if (source.contains("axis") && !readVec3(source["axis"], out.axis)) {
        error = location + "/axis must be three finite numbers";
        return false;
    }
    const auto number = [&](const char* key, float& value) {
        if (!source.contains(key))
            return true;
        if (!source[key].is_number()) {
            error = location + "/" + key + " must be a number";
            return false;
        }
        value = source[key].get<float>();
        return std::isfinite(value)
                   ? true
                   : (error = location + "/" + key + " must be finite", false);
    };
    if (!number("radius", out.radius) ||
        !number("degrees_per_second", out.degreesPerSecond) ||
        !number("phase_degrees", out.phaseDegrees) ||
        !number("height", out.height))
        return false;

    const std::string facing = source.value("facing", std::string("free"));
    if (facing == "free")
        out.facing = OrbitAuthor::Facing::Free;
    else if (facing == "centre")
        out.facing = OrbitAuthor::Facing::Centre;
    else if (facing == "travel")
        out.facing = OrbitAuthor::Facing::Travel;
    else {
        error = location + "/facing must be 'free', 'centre' or 'travel'";
        return false;
    }

    if (out.radius < 0.0f) {
        error = location + "/radius must not be negative";
        return false;
    }
    if (glm::length(out.axis) <= 0.0f) {
        error = location + "/axis must not be zero";
        return false;
    }
    return true;
}

bool parseMesh(const Json& source, MeshAuthor& out, const std::string& location,
               std::string& error)
{
    if (!source.is_object()) {
        error = location + " must be an object";
        return false;
    }
    if (!source.contains("path") || !source["path"].is_string() ||
        source["path"].get<std::string>().empty()) {
        error = location + "/path must be a non-empty asset path";
        return false;
    }
    out.path = source["path"].get<std::string>();
    // Absolute paths parse, but they do not travel: a map that names
    // /home/someone/meshes/x.obj is a map only that machine can load. The
    // resolver takes pack-relative paths, so that is what the format states.
    if (out.path.front() == '/' || out.path.find(':') != std::string::npos) {
        error = location + "/path must be relative to the asset root";
        return false;
    }
    if (source.contains("import_scale")) {
        if (!source["import_scale"].is_number()) {
            error = location + "/import_scale must be a number";
            return false;
        }
        out.importScale = source["import_scale"].get<float>();
    }
    if (!std::isfinite(out.importScale) || out.importScale <= 0.0f) {
        error = location + "/import_scale must be a positive finite number";
        return false;
    }
    return true;
}

// The generated-mesh block. Every field is optional and defaults to the
// component's own, so `"primitive": {"kind": "sphere"}` is a valid half-metre
// sphere -- the shortest thing an author can write that means something.
bool parsePrimitive(const Json& source, PrimitiveAuthor& out,
                    const std::string& location, std::string& error)
{
    if (!source.is_object()) {
        error = location + " must be an object";
        return false;
    }
    if (source.contains("kind")) {
        if (!source["kind"].is_string()) {
            error = location + "/kind must be a primitive name";
            return false;
        }
        const std::string kind = source["kind"].get<std::string>();
        out.kind = eng::ecs::primitiveKindFromName(kind);
        // primitiveKindFromName answers Box for anything it does not know,
        // which is right for a damaged .map and wrong for a hand-edited .scn:
        // a typo would silently become a box and the author would be left
        // wondering why "sphre" is square.
        if (kind != eng::ecs::primitiveKindName(out.kind)) {
            error = location + "/kind '" + kind + "' is not a primitive";
            return false;
        }
    }
    const auto number = [&](const char* key, float& value) {
        if (!source.contains(key))
            return true;
        if (!source[key].is_number() || !std::isfinite(source[key].get<float>())) {
            error = location + "/" + key + " must be a finite number";
            return false;
        }
        value = source[key].get<float>();
        return true;
    };
    const auto count = [&](const char* key, int& value) {
        if (!source.contains(key))
            return true;
        if (!source[key].is_number_integer()) {
            error = location + "/" + key + " must be an integer";
            return false;
        }
        value = source[key].get<int>();
        return true;
    };
    if (source.contains("size") && !readVec3(source["size"], out.size)) {
        error = location + "/size must be three finite numbers";
        return false;
    }
    if (!number("radius", out.radius) || !number("height", out.height) ||
        !number("bevel", out.bevel) || !number("thickness", out.thickness))
        return false;
    if (!count("rings", out.rings) || !count("segments", out.segments) ||
        !count("subdivisions", out.subdivisions))
        return false;
    if (source.contains("inward_facing")) {
        if (!source["inward_facing"].is_boolean()) {
            error = location + "/inward_facing must be a boolean";
            return false;
        }
        out.inwardFacing = source["inward_facing"].get<bool>();
    }
    // The bounds the generators need, checked here so a bad number is a parse
    // error naming the field rather than an entity that silently draws nothing.
    if (out.size.x <= 0.0f || out.size.y <= 0.0f || out.size.z <= 0.0f) {
        error = location + "/size must be positive";
        return false;
    }
    if (out.radius <= 0.0f || out.height <= 0.0f || out.thickness <= 0.0f) {
        error = location + " needs positive radius, height and thickness";
        return false;
    }
    if (out.rings < 2 || out.segments < 3 || out.subdivisions < 0) {
        error = location + " needs rings >= 2, segments >= 3, subdivisions >= 0";
        return false;
    }
    return true;
}

bool parseCollider(const Json& source, ColliderAuthor& out,
                   const std::string& location, std::string& error)
{
    if (!source.is_object() ||
        source.value("shape", std::string("box")) != "box") {
        error = location + " currently supports only box shapes";
        return false;
    }
    if (!source.contains("half_extents") ||
        !readVec3(source["half_extents"], out.halfExtents)) {
        error = location + "/half_extents must be three finite numbers";
        return false;
    }
    if (source.contains("offset") && !readVec3(source["offset"], out.offset)) {
        error = location + "/offset must be three finite numbers";
        return false;
    }
    return true;
}

bool parseTrigger(const Json& source, TriggerAuthor& out,
                  const std::string& location, std::string& error)
{
    if (!source.is_object() || !source.contains("size") ||
        !readVec3(source["size"], out.size)) {
        error = location + "/size must be three finite numbers";
        return false;
    }
    out.event = source.value("event", std::string());
    return true;
}

bool parseEntity(const Json& source, const std::string& location, Entity& out,
                 std::string& error)
{
    if (!source.is_object()) {
        error = location + " must be an object";
        return false;
    }
    out.id = source.value("id", std::string());
    if (out.id.empty()) {
        error = location + "/id must be a non-empty string";
        return false;
    }
    out.name = source.value("name", out.id);
    out.castShadows = source.value("cast_shadows", true);

    if (source.contains("parent")) {
        if (!source["parent"].is_string()) {
            error = location + "/parent must be the id of another entity";
            return false;
        }
        // Existence is not checked here: entities are parsed in file order and
        // a parent is allowed to appear after its child. validate() is what
        // reports a parent that is nowhere in the document.
        out.parent = source["parent"].get<std::string>();
    }

    if (source.contains("layer")) {
        if (!source["layer"].is_string()) {
            error = location + "/layer must be the id of a declared layer";
            return false;
        }
        // Existence is validate()'s to report, for the same reason `parent` is:
        // a document that names a layer somebody deleted has to open, or there
        // is nothing to fix it with.
        out.layer = source["layer"].get<std::string>();
    }

    if (source.contains("prefab")) {
        if (!source["prefab"].is_string()) {
            error = location + "/prefab must be a logical asset id";
            return false;
        }
        out.prefab = source["prefab"].get<std::string>();
    }
    if (source.contains("mesh")) {
        MeshAuthor mesh;
        if (!parseMesh(source["mesh"], mesh, location + "/mesh", error))
            return false;
        out.mesh = mesh;
    }
    if (source.contains("primitive")) {
        PrimitiveAuthor primitive;
        if (!parsePrimitive(source["primitive"], primitive,
                            location + "/primitive", error))
            return false;
        out.primitive = primitive;
    }
    // Three ways to be a mesh, one entity. Refused rather than silently
    // resolved: the cooker has an order, but an author who wrote both meant one
    // of them and finding out which in the viewport is not authoring.
    if ((!out.prefab.empty() ? 1 : 0) + (out.mesh ? 1 : 0) +
            (out.primitive ? 1 : 0) >
        1) {
        error = location +
                " may carry only one of prefab, mesh and primitive";
        return false;
    }
    if (source.contains("material")) {
        if (!source["material"].is_string() ||
            source["material"].get<std::string>().empty()) {
            error = location + "/material must be a non-empty string";
            return false;
        }
        out.material = source["material"].get<std::string>();
    }
    if (!parseTransform(source, out.transform, location, error))
        return false;
    if (source.contains("cell")) {
        CellPlacement cell;
        if (!parseCell(source["cell"], cell, location, error))
            return false;
        out.cell = cell;
    }
    if (source.contains("collider")) {
        ColliderAuthor collider;
        if (!parseCollider(source["collider"], collider, location + "/collider",
                           error))
            return false;
        out.collider = collider;
    }
    if (source.contains("light")) {
        LightAuthor light;
        if (!parseLight(source["light"], light, location + "/light", error))
            return false;
        out.light = light;
    }
    if (source.contains("camera")) {
        CameraAuthor camera;
        if (!parseCamera(source["camera"], camera, location + "/camera", error))
            return false;
        out.camera = camera;
    }
    if (!parseScripts(source, out.scripts, location, error))
        return false;
    if (source.contains("properties") &&
        !parseProps(source["properties"], out.properties,
                    location + "/properties", error))
        return false;
    if (source.contains("spin")) {
        SpinAuthor spin;
        if (!parseSpin(source["spin"], spin, location + "/spin", error))
            return false;
        out.spin = spin;
    }
    if (source.contains("orbit")) {
        OrbitAuthor orbit;
        if (!parseOrbit(source["orbit"], orbit, location + "/orbit", error))
            return false;
        out.orbit = orbit;
    }
    if (source.contains("particles")) {
        ParticleAuthor particles;
        if (!parseFields(source["particles"], eng::fieldsOf<ParticleAuthor>(),
                         &particles, location + "/particles", error))
            return false;
        out.particles = particles;
    }
    if (source.contains("audio")) {
        AudioEmitterAuthor audio;
        if (!parseAudioEmitter(source["audio"], audio, location + "/audio",
                               error))
            return false;
        out.audio = audio;
    }
    if (source.contains("audio_listener")) {
        AudioListenerAuthor listener;
        if (!parseAudioListener(source["audio_listener"], listener,
                                location + "/audio_listener", error))
            return false;
        out.audioListener = listener;
    }
    if (source.contains("actor")) {
        if (!source["actor"].is_string()) {
            error = location + "/actor must be \"player\", \"npc\" or \"enemy\"";
            return false;
        }
        game::ActorKind kind{};
        if (!game::parseActorKind(source["actor"].get<std::string>(), kind)) {
            error = location + "/actor is not an actor kind";
            return false;
        }
        out.actor = kind;
    }
    if (source.contains("sounds")) {
        const Json& sounds = source["sounds"];
        if (!sounds.is_object()) {
            error = location + "/sounds must be an object of action -> cue id";
            return false;
        }
        ActorSoundsAuthor set;
        for (auto row = sounds.begin(); row != sounds.end(); ++row) {
            const game::ActorActionInfo* info =
                game::findActorAction(row.key());
            if (!info) {
                // Named rather than ignored: a misspelled action is a sound
                // that never plays, and finding that out at load is the whole
                // point of having a closed vocabulary.
                error = location + "/sounds/" + row.key() +
                        " is not an action an actor performs";
                return false;
            }
            if (!row.value().is_string()) {
                error = location + "/sounds/" + row.key() +
                        " must be a cue id from audio.toml";
                return false;
            }
            set.set(info->action, row.value().get<std::string>());
        }
        out.sounds = set;
    }
    if (source.contains("portal")) {
        PortalAuthor portal;
        if (!parseFields(source["portal"], eng::fieldsOf<PortalAuthor>(),
                         &portal, location + "/portal", error))
            return false;
        out.portal = portal;
    }
    // The player rig: both components are reflected, so the accepted keys are
    // their own field names and there is no parser here to fall behind them.
    if (source.contains("first_person")) {
        FirstPersonAuthor player;
        if (!parseFields(source["first_person"],
                         eng::fieldsOf<FirstPersonAuthor>(), &player,
                         location + "/first_person", error))
            return false;
        out.firstPerson = player;
    }
    if (source.contains("third_person")) {
        ThirdPersonAuthor camera;
        if (!parseFields(source["third_person"],
                         eng::fieldsOf<ThirdPersonAuthor>(), &camera,
                         location + "/third_person", error))
            return false;
        out.thirdPerson = camera;
    }
    if (source.contains("screen")) {
        ScreenAuthor screen;
        if (!parseFields(source["screen"], eng::fieldsOf<ScreenAuthor>(),
                         &screen, location + "/screen", error))
            return false;
        out.screen = screen;
    }
    if (source.contains("viewmodel_rig")) {
        ViewmodelRigAuthor rig;
        if (!parseFields(source["viewmodel_rig"],
                         eng::fieldsOf<ViewmodelRigAuthor>(), &rig,
                         location + "/viewmodel_rig", error))
            return false;
        out.viewmodelRig = rig;
    }
    if (source.contains("unpacked_attachments")) {
        if (!source["unpacked_attachments"].is_boolean()) {
            error = location + "/unpacked_attachments must be a boolean";
            return false;
        }
        out.unpackedAttachments = source["unpacked_attachments"].get<bool>();
    }
    if (source.contains("viewmodel_preview")) {
        ViewmodelPreviewAuthor preview;
        if (!parseFields(source["viewmodel_preview"],
                         eng::fieldsOf<ViewmodelPreviewAuthor>(), &preview,
                         location + "/viewmodel_preview", error))
            return false;
        out.viewmodelPreview = preview;
    }
    if (source.contains("shader")) {
        ShaderAuthor shader;
        if (!parseShader(source["shader"], shader, location + "/shader", error))
            return false;
        out.shader = shader;
    }
    if (source.contains("exit")) {
        const Json& exit = source["exit"];
        if (!exit.is_object() || !exit.contains("yaw_degrees") ||
            !exit["yaw_degrees"].is_number()) {
            error = location + "/exit/yaw_degrees must be a number";
            return false;
        }
        const float yaw = exit["yaw_degrees"].get<float>();
        if (!std::isfinite(yaw)) {
            error = location + "/exit/yaw_degrees must be finite";
            return false;
        }
        out.exitYawDegrees = yaw;
    }
    if (source.contains("marker")) {
        if (!source["marker"].is_string() ||
            source["marker"].get<std::string>().empty()) {
            error = location + "/marker must be a non-empty string";
            return false;
        }
        out.marker = source["marker"].get<std::string>();
    }
    if (source.contains("enemy_spawn")) {
        if (!source["enemy_spawn"].is_string()) {
            error = location + "/enemy_spawn must be a string";
            return false;
        }
        out.enemySpawn = source["enemy_spawn"].get<std::string>();
    }
    if (source.contains("pickup")) {
        if (!source["pickup"].is_string()) {
            error = location + "/pickup must be a string";
            return false;
        }
        out.pickup = source["pickup"].get<std::string>();
    }
    if (source.contains("npc")) {
        if (!source["npc"].is_string()) {
            error = location + "/npc must be a string";
            return false;
        }
        out.npc = source["npc"].get<std::string>();
    }
    if (source.contains("trigger")) {
        TriggerAuthor trigger;
        if (!parseTrigger(source["trigger"], trigger, location + "/trigger",
                          error))
            return false;
        out.trigger = trigger;
    }
    out.playerSpawn = source.value("player_spawn", false);
    return true;
}

} // namespace

bool parseSceneSource(const std::string& json, const std::string& location,
                      SceneDocument& out, std::string& error)
{
    error.clear();
    Json root = Json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        error = location + ": invalid JSON object";
        return false;
    }
    try {
        const int version = root.value("version", 0);
        if (root.value("format", std::string()) != "raven-scene" ||
            (version != 1 && version != 2) ||
            root.value("id", std::string()).empty()) {
            error = location +
                    ": format, version 1 or 2, and non-empty id are required";
            return false;
        }
        if (!root.contains("entities") || !root["entities"].is_array()) {
            error = location + ": entities must be an array";
            return false;
        }

        SceneDocument document;
        document.id = root["id"].get<std::string>();
        if (root.contains("palette")) {
            if (!root["palette"].is_string()) {
                error = location + "/palette must be the name of a table in "
                                   "palettes.toml";
                return false;
            }
            // Existence is palettes.toml's business and validate()'s to report:
            // a scene naming a palette that has been renamed must still open,
            // because opening it is how the name gets fixed.
            document.palette = root["palette"].get<std::string>();
        }
        if (root.contains("layers")) {
            const Json& list = root["layers"];
            if (!list.is_array()) {
                error = location + "/layers must be an array";
                return false;
            }
            std::unordered_set<std::string> seenLayers;
            for (std::size_t i = 0; i < list.size(); ++i) {
                const Json& node = list[i];
                const std::string where =
                    location + "/layers/" + std::to_string(i);
                if (!node.is_object() || !node.contains("id") ||
                    !node["id"].is_string() ||
                    node["id"].get<std::string>().empty()) {
                    error = where + " needs a non-empty string 'id'";
                    return false;
                }
                Layer layer;
                layer.id = node["id"].get<std::string>();
                if (!seenLayers.insert(layer.id).second) {
                    error = where + "/id '" + layer.id + "' is not unique";
                    return false;
                }
                layer.name = node.value("name", layer.id);
                if (node.contains("colour") &&
                    !readVec3(node["colour"], layer.colour)) {
                    error = where + "/colour must be three finite numbers";
                    return false;
                }
                document.layers.push_back(std::move(layer));
            }
        }
        std::unordered_set<std::string> seen;
        const Json& entities = root["entities"];
        document.entities.reserve(entities.size());
        for (std::size_t i = 0; i < entities.size(); ++i) {
            Entity entity;
            const std::string where = location + ":/entities/" + std::to_string(i);
            if (!parseEntity(entities[i], where, entity, error))
                return false;
            if (!seen.insert(entity.id).second) {
                error = where + "/id '" + entity.id + "' is not unique";
                return false;
            }
            document.entities.push_back(std::move(entity));
        }
        out = std::move(document);
        return true;
    } catch (const std::exception& exception) {
        error = location + ": " + exception.what();
        return false;
    }
}

bool loadSceneSource(const std::string& sourcePath, SceneDocument& out,
                     std::string& error)
{
    std::ifstream input(sourcePath);
    if (!input) {
        error = sourcePath + ": cannot open scene source";
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return parseSceneSource(buffer.str(), sourcePath, out, error);
}

} // namespace game::content
