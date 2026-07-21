#pragma once

#include <eng/Handles.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace eng { class Renderer; }

// Reusable descriptors/results for composed scene objects. Primitive meshes
// remain in Renderer; factories assemble them into game-facing prefabs.
struct PortalStyle {
    std::string material = "Game/PortalDown";
    std::string frameMaterial = "Game/DungeonTileTwoSided";
    std::string frameMesh;
    std::string particles = "PSX/Sparkles";
    glm::vec3 lightColour{1.05f, 0.20f, 1.45f};
    float yawDegrees = 0.0f;
    float lightRange = 5.5f;
    float outerRadius = 0.92f;
    float innerRadius = 1.0f;
    // Authored arch opening is broad and low (about 3.3 x 1.9 m).
    glm::vec2 fieldScale{1.62f, 1.30f};
    float height = 1.30f;
    int segments = 18;
};

struct PortalVisual {
    eng::NodeHandle root{};
    eng::NodeHandle field{};
    eng::LightHandle light{};
};

struct ShowcaseExhibit {
    std::string id;
    std::string label;
    glm::vec3 position{0.0f};
    glm::vec3 halfExtents{0.0f};
    bool blocksMovement = false;
};

PortalVisual createPortal(eng::Renderer& renderer, glm::vec3 floorPosition,
                          const PortalStyle& style);
void animatePortal(eng::Renderer& renderer, const PortalVisual& portal,
                   float time, float direction = 1.0f);

// Loads a TOML-authored primitive/material gallery. Returns false and logs a
// useful error if the file is malformed; individual unknown shapes are skipped.
bool loadPrimitiveShowcase(eng::Renderer& renderer, const std::string& path,
                           std::vector<ShowcaseExhibit>& loaded);
