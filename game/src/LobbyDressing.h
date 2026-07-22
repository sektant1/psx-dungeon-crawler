#pragma once

#include <eng/Handles.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace eng { class Renderer; }

// One authored placement. 1..N mesh/material pairs share a transform.
struct LobbyProp {
    std::vector<std::string> meshes;
    std::vector<std::string> materials;
    glm::vec3 position{0.0f};
    float yawDeg = 0.0f;
    glm::vec3 scale{1.0f};
    bool castShadows = true;
};

// Pure parse (no Renderer): fills `out`, returns false + fills `err` on a
// malformed file. Rows with mismatched or empty mesh/material counts are skipped.
bool parseLobbyDressing(const std::string& tomlPath,
                        std::vector<LobbyProp>& out, std::string& err);

// Loads authored static prop placements and attaches them under kRootNode.
// Returns false + logs on a malformed file; missing meshes are skipped.
// meshDir is the props mesh directory (assets + "/meshes/props/").
bool loadLobbyDressing(eng::Renderer& r, const std::string& tomlPath,
                       const std::string& meshDir);
