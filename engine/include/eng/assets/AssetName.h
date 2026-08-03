#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace eng::assets {

// Names have different grammars because they serve different registries. Never
// normalize a loaded reference before lookup: validation reports legacy debt;
// canonicalToken() is for producers creating new names.
enum class AssetNameKind {
    LocalId,      // hollow_soldier
    QualifiedId,  // game.particle.fireball_trail
    RuntimePath,  // textures/vfx/flame_01.png
    MaterialId,   // Game/Kit/Dungeon
    ShaderPath,   // shaders/particle_sprite.vert
};

struct AssetNameIssue {
    std::string code;
    std::size_t offset = 0;
    std::string message;
};

std::optional<AssetNameIssue> validateAssetName(AssetNameKind kind,
                                                std::string_view name);

// Producer helper: mixed source/vendor text to lowercase snake case. This does
// not make a globally unique id; caller still owns namespace and collision
// policy.
std::string canonicalToken(std::string_view source);

// Tool-only fallback. Explicit authored display labels always win.
std::string friendlyAssetLabel(std::string_view stableName);

} // namespace eng::assets
