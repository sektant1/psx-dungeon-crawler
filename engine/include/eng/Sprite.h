#pragma once

#include <glm/glm.hpp>

#include <string>

namespace eng {

enum class SpriteBlend { Opaque, Alpha, Additive };

// One reusable 2D asset contract. The same clip can drive a billboard sprite
// or be turned into a material for arbitrary mesh UVs.
struct SpriteClip {
    std::string texture;
    glm::ivec2 grid{1, 1};
    int frameCount = 1;
    float framesPerSecond = 0.0f;
    glm::vec2 scrollVelocity{0.0f}; // UV units / second
    glm::vec2 uvScale{1.0f};
    glm::vec4 tint{1.0f};
    glm::vec2 worldSize{1.0f};
    float phaseSeconds = 0.0f;
    float alphaCutoff = 0.0f;
    SpriteBlend blend = SpriteBlend::Opaque;
};

} // namespace eng
