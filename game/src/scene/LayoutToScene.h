#pragma once

#include <entt/entt.hpp>

#include <string>

namespace gen { class Layout; }

namespace game {

struct SceneGenOptions {
    float cell = 4.0f;
    float wallHeight = 3.0f;
    // Asset-root-relative runtime paths. Absolute source-machine paths make
    // cooked maps non-portable and non-deterministic across checkouts.
    std::string kitDir = "meshes/kit/";
    std::string propDir = "meshes/props/";
};

// Populate reg with tile/wall/marker/prop/torch entities for layout. Registry
// data only (no renderer/physics). reg is NOT cleared first. Invalid input is
// rejected before the registry is changed.
bool layoutToScene(const gen::Layout& layout, const SceneGenOptions& opts,
                   entt::registry& reg);

} // namespace game
