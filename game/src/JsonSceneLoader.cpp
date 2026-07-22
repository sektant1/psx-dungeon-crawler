#include "JsonSceneLoader.h"

#include "DungeonGen.h"
#include "DungeonMap.h"

#include <eng/Physics.h>
#include <eng/Renderer.h>

#include <glm/gtc/quaternion.hpp>
#include <json.hpp>

#include <fstream>

namespace {

glm::vec3 vec3(const nlohmann::json& j, const char* key, glm::vec3 fallback)
{
    const auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() != 3)
        return fallback;
    return {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>()};
}

std::string pathJoin(const std::string& base, const std::string& path)
{
    if (path.empty() || path[0] == '/')
        return path;
    return base + "/" + path;
}

void addDefaultFloor(eng::Renderer& r, eng::NodeHandle parent)
{
    eng::NodeHandle root = r.createNode(parent, glm::vec3(0.0f), "Default Floor");
    eng::NodeHandle floor = r.createNode(root, glm::vec3(0.0f),
                                         "Prototype Grid Plane");
    r.setScale(floor, glm::vec3(48.0f, 1.0f, 48.0f));
    r.attachMesh(floor, r.createPlane(1.0f), "Game/PrototypeFloor");
}

void loadNode(const nlohmann::json& j, eng::Renderer& r, eng::NodeHandle parent,
              const std::string& assetDir)
{
    const std::string name = j.value("name", std::string("Node"));
    eng::NodeHandle node = r.createNode(parent, vec3(j, "position", {}), name);
    const glm::vec3 rot = vec3(j, "rotation_degrees", {});
    if (rot != glm::vec3(0.0f)) {
        r.setOrientation(node,
            glm::angleAxis(glm::radians(rot.y), glm::vec3(0, 1, 0)) *
            glm::angleAxis(glm::radians(rot.x), glm::vec3(1, 0, 0)) *
            glm::angleAxis(glm::radians(rot.z), glm::vec3(0, 0, 1)));
    }
    const glm::vec3 scale = vec3(j, "scale", glm::vec3(1.0f));
    if (scale != glm::vec3(1.0f))
        r.setScale(node, scale);

    if (const std::string mesh = j.value("mesh", std::string()); !mesh.empty()) {
        const std::string material =
            j.value("material", std::string("Game/PrototypeFloor"));
        r.attachMesh(node, r.loadObj(pathJoin(assetDir, mesh)), material,
                     j.value("cast_shadows", false));
    }
    if (const auto light = j.find("light"); light != j.end() && light->is_object()) {
        eng::LightDesc desc;
        const std::string type = light->value("type", std::string("point"));
        desc.type = type == "directional" ? eng::LightDesc::Type::Directional
                                          : eng::LightDesc::Type::Point;
        desc.colour = vec3(*light, "colour", glm::vec3(1.0f));
        desc.range = light->value("range", 5.0f);
        desc.castShadows = light->value("cast_shadows", false);
        r.attachLight(node, desc);
    }
    if (const auto children = j.find("children");
        children != j.end() && children->is_array()) {
        for (const nlohmann::json& child : *children)
            if (child.is_object())
                loadNode(child, r, node, assetDir);
    }
}

} // namespace

bool loadJsonScene(const std::string& path, eng::Renderer& r,
                   eng::Physics* physics, eng::NodeHandle parent,
                   const std::string& assetDir, std::string& error,
                   DungeonMap* dungeon)
{
    std::ifstream in(path);
    if (!in) {
        error = "could not open " + path;
        return false;
    }
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(in);
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }

    const std::string sceneName = root.value("name", std::string("JSON Scene"));
    eng::NodeHandle scene = r.createNode(parent, glm::vec3(0.0f), sceneName);

    // Generator directive: rebuild a procedural scene 1:1 from the same code
    // the game runs. Deterministic in the seed, so the editor view matches the
    // game's dungeon for that seed exactly.
    const auto gen = root.find("generator");
    const bool hasGenerator =
        gen != root.end() && gen->is_object();
    if (hasGenerator) {
        const std::string type = gen->value("type", std::string());
        if (type == "dungeon") {
            if (!dungeon || !physics) {
                error = "generator 'dungeon' needs a DungeonMap + Physics";
                return false;
            }
            const uint32_t seed = gen->value("seed", 1u);
            ::gen::Layout layout = ::gen::generate(seed);
            if (!dungeon->loadFromRows(r, *physics, std::move(layout),
                                       assetDir + "/meshes/tiles/",
                                       assetDir + "/meshes/props/", scene)) {
                error = "dungeon generator (seed " + std::to_string(seed) +
                        ") failed to build";
                return false;
            }
        } else {
            error = "unknown generator type: '" + type + "'";
            return false;
        }
    }

    // A generated dungeon brings its own floor, so only default a prototype
    // grid when there's no generator (unless the scene explicitly asks).
    if (root.value("default_floor", !hasGenerator))
        addDefaultFloor(r, scene);
    const auto nodes = root.find("nodes");
    if (nodes != root.end() && nodes->is_array())
        for (const nlohmann::json& node : *nodes)
            if (node.is_object())
                loadNode(node, r, scene, assetDir);

    error.clear();
    return true;
}
