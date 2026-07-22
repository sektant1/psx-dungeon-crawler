#include "LobbyDressing.h"

#include <eng/Log.h>
#include <eng/Renderer.h>

#include <glm/gtc/quaternion.hpp>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

namespace {

// Read a 3-array into `out`, leaving `out` untouched when the key is absent or
// malformed (so caller-supplied defaults survive).
void vec3(const toml::table& t, const char* key, glm::vec3& out) {
    const toml::array* a = t[key].as_array();
    if (!a || a->size() != 3) return;
    out = glm::vec3(float((*a)[0].value_or(double(out.x))),
                    float((*a)[1].value_or(double(out.y))),
                    float((*a)[2].value_or(double(out.z))));
}

std::vector<std::string> strArray(const toml::table& t, const char* key) {
    std::vector<std::string> v;
    const toml::array* a = t[key].as_array();
    if (!a) return v;
    for (const toml::node& n : *a)
        if (auto s = n.value<std::string>()) v.push_back(*s);
    return v;
}

} // namespace

bool parseLobbyDressing(const std::string& tomlPath,
                        std::vector<LobbyProp>& out, std::string& err) {
    toml::parse_result parsed = toml::parse_file(tomlPath);
    if (!parsed) {
        err = std::string(parsed.error().description());
        return false;
    }
    const toml::table& root = parsed.table();
    const toml::array* props = root["prop"].as_array();
    if (!props) {
        err = "no [[prop]] array in " + tomlPath;
        return false;
    }

    for (const toml::node& node : *props) {
        const toml::table* t = node.as_table();
        if (!t) continue;
        LobbyProp p;
        p.meshes = strArray(*t, "meshes");
        p.materials = strArray(*t, "materials");
        // Skip rows with empty or mismatched mesh/material counts.
        if (p.meshes.empty() || p.meshes.size() != p.materials.size())
            continue;
        vec3(*t, "position", p.position);
        p.yawDeg = float((*t)["yaw"].value_or(double(p.yawDeg)));
        vec3(*t, "scale", p.scale);
        p.castShadows = (*t)["cast_shadows"].value_or(p.castShadows);
        out.push_back(std::move(p));
    }
    return true;
}

bool loadLobbyDressing(eng::Renderer& r, const std::string& tomlPath,
                       const std::string& meshDir) {
    std::vector<LobbyProp> props;
    std::string err;
    if (!parseLobbyDressing(tomlPath, props, err)) {
        eng::log::error("lobby dressing: %s", err.c_str());
        return false;
    }

    for (const LobbyProp& p : props) {
        eng::NodeHandle n = r.createNode(eng::kRootNode, p.position);
        if (p.yawDeg != 0.0f)
            r.setOrientation(n, glm::angleAxis(glm::radians(p.yawDeg),
                                               glm::vec3(0, 1, 0)));
        if (p.scale != glm::vec3(1.0f))
            r.setScale(n, p.scale);
        for (size_t i = 0; i < p.meshes.size(); ++i) {
            eng::MeshHandle m = r.loadObj(meshDir + p.meshes[i]);
            if (!m.valid()) continue; // missing mesh: skip this part
            r.attachMesh(n, m, p.materials[i], p.castShadows);
        }
    }
    return true;
}
