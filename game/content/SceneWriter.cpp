#include "SceneWriter.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace game::content {
namespace {

using Json = nlohmann::ordered_json; // ordered: key order is part of the format

// 4 decimals is finer than a millimetre at this scale and coarse enough that a
// drag which lands on the grid writes 2 rather than 1.9999999.
float canonical(float value)
{
    if (!std::isfinite(value))
        return 0.0f;
    const float rounded = std::round(value * 10000.0f) / 10000.0f;
    return rounded == 0.0f ? 0.0f : rounded; // kills -0
}

Json vec3(const glm::vec3& v)
{
    return Json::array({canonical(v.x), canonical(v.y), canonical(v.z)});
}

bool nearlyEqual(const glm::vec3& a, const glm::vec3& b)
{
    return canonical(a.x) == canonical(b.x) && canonical(a.y) == canonical(b.y) &&
           canonical(a.z) == canonical(b.z);
}

const char* edgeName(CellPlacement::Edge edge)
{
    switch (edge) {
    case CellPlacement::Edge::North: return "north";
    case CellPlacement::Edge::East: return "east";
    case CellPlacement::Edge::South: return "south";
    case CellPlacement::Edge::West: return "west";
    case CellPlacement::Edge::None: break;
    }
    return nullptr;
}

Json writeEntity(const Entity& entity)
{
    Json out = Json::object();
    out["id"] = entity.id;
    if (!entity.name.empty() && entity.name != entity.id)
        out["name"] = entity.name;
    if (!entity.prefab.empty())
        out["prefab"] = entity.prefab;
    if (!entity.castShadows)
        out["cast_shadows"] = false;

    const XformAuthor& xform = entity.transform;
    Json transform = Json::object();
    if (!nearlyEqual(xform.position, glm::vec3(0.0f)))
        transform["position"] = vec3(xform.position);
    if (!nearlyEqual(xform.rotationDegrees, glm::vec3(0.0f)))
        transform["rotation_degrees"] = vec3(xform.rotationDegrees);
    if (!nearlyEqual(xform.scale, glm::vec3(1.0f)))
        transform["scale"] = vec3(xform.scale);
    if (!transform.empty())
        out["transform"] = std::move(transform);

    if (entity.cell) {
        const CellPlacement& cell = *entity.cell;
        Json node = Json::object();
        node["col"] = cell.col;
        node["row"] = cell.row;
        if (const char* edge = edgeName(cell.edge))
            node["edge"] = edge;
        if (cell.span != 1)
            node["span"] = cell.span;
        if (cell.yawQuarters != 0)
            node["yaw_quarters"] = cell.yawQuarters;
        if (canonical(cell.level) != 0.0f)
            node["level"] = canonical(cell.level);
        out["cell"] = std::move(node);
    }
    if (entity.collider) {
        Json node = Json::object();
        node["shape"] = "box";
        node["half_extents"] = vec3(entity.collider->halfExtents);
        if (!nearlyEqual(entity.collider->offset, glm::vec3(0.0f)))
            node["offset"] = vec3(entity.collider->offset);
        out["collider"] = std::move(node);
    }
    if (entity.light) {
        const LightAuthor& light = *entity.light;
        Json node = Json::object();
        node["type"] =
            light.type == LightAuthor::Type::Directional ? "directional" : "point";
        node["colour"] = vec3(light.colour);
        if (light.type == LightAuthor::Type::Point)
            node["range"] = canonical(light.range);
        if (light.castShadows)
            node["cast_shadows"] = true;
        out["light"] = std::move(node);
    }
    if (entity.playerSpawn)
        out["player_spawn"] = true;
    if (entity.exitYawDegrees) {
        Json node = Json::object();
        node["yaw_degrees"] = canonical(*entity.exitYawDegrees);
        out["exit"] = std::move(node);
    }
    if (entity.enemySpawn)
        out["enemy_spawn"] = *entity.enemySpawn;
    if (entity.pickup)
        out["pickup"] = *entity.pickup;
    if (entity.trigger) {
        Json node = Json::object();
        node["size"] = vec3(entity.trigger->size);
        node["event"] = entity.trigger->event;
        out["trigger"] = std::move(node);
    }
    if (entity.marker)
        out["marker"] = *entity.marker;
    return out;
}

} // namespace

std::string serializeSceneSource(const SceneDocument& document)
{
    // Sorted by id, so the file order never depends on the order things were
    // created in this session.
    std::vector<const Entity*> sorted;
    sorted.reserve(document.entities.size());
    for (const Entity& entity : document.entities)
        sorted.push_back(&entity);
    std::sort(sorted.begin(), sorted.end(),
              [](const Entity* a, const Entity* b) { return a->id < b->id; });

    Json root = Json::object();
    root["$schema"] = "../schemas/scene.schema.json";
    root["format"] = "psx-dungeon-scene";
    root["version"] = 2;
    root["id"] = document.id;
    Json entities = Json::array();
    for (const Entity* entity : sorted)
        entities.push_back(writeEntity(*entity));
    root["entities"] = std::move(entities);

    return root.dump(2) + "\n";
}

bool writeSceneSource(const std::string& path, const SceneDocument& document,
                      std::string& error)
{
    error.clear();
    for (const Entity& entity : document.entities) {
        // An absolute path in a scene is how the previous editor produced maps
        // that only opened on the machine that authored them.
        if (!entity.prefab.empty() &&
            std::filesystem::path(entity.prefab).is_absolute()) {
            error = "entity '" + entity.id + "' has an absolute prefab path";
            return false;
        }
    }

    const std::string text = serializeSceneSource(document);
    const std::filesystem::path target(path);
    const std::filesystem::path temp =
        target.parent_path() / (target.filename().string() + ".tmp");
    {
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = path + ": cannot open for writing";
            return false;
        }
        output.write(text.data(), std::streamsize(text.size()));
        if (!output) {
            error = path + ": write failed";
            return false;
        }
    }
    std::error_code code;
    std::filesystem::rename(temp, target, code);
    if (code) {
        std::filesystem::remove(temp, code);
        error = path + ": atomic rename failed";
        return false;
    }
    return true;
}

} // namespace game::content
