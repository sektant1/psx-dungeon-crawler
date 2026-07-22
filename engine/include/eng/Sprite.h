#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace eng {

enum class SpriteBlend { Opaque, Alpha, Additive, Overlay };

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

struct TextSpriteStyle {
    struct ColourRule {
        std::string pattern;
        glm::vec4 colour{1.0f};
    };
    glm::vec4 textColour{1.0f, 0.90f, 0.62f, 1.0f};
    glm::vec4 backgroundColour{0.035f, 0.025f, 0.045f, 0.90f};
    glm::vec4 borderColour{0.42f, 0.30f, 0.12f, 1.0f};
    glm::vec4 accentColour{0.88f, 0.58f, 0.12f, 1.0f};
    float worldHeight = 0.34f;
    int paddingPixels = 4;
    int maxWidthPixels = 156;
    int lineSpacingPixels = 2;
    int accentWidthPixels = 3;
    std::vector<ColourRule> colourRules;
};

} // namespace eng
