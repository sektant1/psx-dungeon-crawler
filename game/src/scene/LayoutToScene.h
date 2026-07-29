#pragma once

#include <entt/entt.hpp>

#include <string>

namespace gen { class Layout; }

namespace game {

struct SceneGenOptions {
    float cell = 4.0f;
    float wallHeight = 3.0f;
    std::string kitDir; // meshes/kit, absolute, trailing slash
    std::string propDir; // absolute, trailing slash
};

// Populate reg with tile/wall/marker/prop/torch entities for layout. Registry
// data only (no renderer/physics). reg is NOT cleared first.
void layoutToScene(const gen::Layout& layout, const SceneGenOptions& opts,
                   entt::registry& reg);

} // namespace game
