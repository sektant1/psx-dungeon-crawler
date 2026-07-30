#include "SceneSource.h"

#include <nlohmann/json.hpp>

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

    if (source.contains("prefab")) {
        if (!source["prefab"].is_string()) {
            error = location + "/prefab must be a logical asset id";
            return false;
        }
        out.prefab = source["prefab"].get<std::string>();
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
