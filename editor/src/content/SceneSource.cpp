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

    if (source.contains("prefab")) {
        if (!source["prefab"].is_string()) {
            error = location + "/prefab must be a logical asset id";
            return false;
        }
        out.prefab = source["prefab"].get<std::string>();
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
    if (source.contains("portal")) {
        PortalAuthor portal;
        if (!parseFields(source["portal"], eng::fieldsOf<PortalAuthor>(),
                         &portal, location + "/portal", error))
            return false;
        out.portal = portal;
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
        if (root.value("format", std::string()) != "psx-dungeon-scene" ||
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
