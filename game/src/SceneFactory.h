#pragma once

#include <eng/Handles.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace eng { class Renderer; }

// Reusable descriptors/results for composed scene objects. Primitive meshes
// remain in Renderer; factories assemble them into game-facing prefabs.
struct PortalPropStyle {
    std::string material = "Game/PortalDown";
    std::string frameMaterial = "Game/DungeonTileTwoSided";
    std::string frameMesh;
    std::string particles = "sparkles"; // particle EFFECT name (particles.toml)
    glm::vec3 lightColour{1.05f, 0.20f, 1.45f};
    float yawDegrees = 0.0f;
    float lightRange = 5.5f;
    float outerRadius = 0.92f;
    float innerRadius = 1.0f;
    // Overscan hides the membrane edges behind the authored arch at oblique
    // camera angles. The small inset prevents z-fighting without exposing a
    // dark tunnel around the field.
    glm::vec2 fieldScale{1.90f, 1.55f};
    float height = 1.42f;
    float membraneInset = -0.035f;
    glm::vec3 frameOffset{-2.0f, 0.0f, 0.0f};
    glm::vec3 frameScale{1.0f, 1.0f, 0.12f};
    glm::vec3 labelOffset{0.0f, 2.82f, 0.10f};
    int segments = 18;
};

struct PortalProp {
    eng::NodeHandle root{};
    eng::NodeHandle field{};
    eng::NodeHandle labelAnchor{};
    eng::LightHandle light{};
    glm::vec3 labelWorldPosition{0.0f};
};

struct ShowcaseExhibit {
    std::string id;
    std::string label;
    std::string labelHighlightPattern;
    glm::vec4 labelAccent{0.88f, 0.58f, 0.12f, 1.0f};
    glm::vec4 labelHighlight{1.0f, 0.78f, 0.22f, 1.0f};
    glm::vec3 position{0.0f};
    glm::vec3 halfExtents{0.0f};
    bool blocksMovement = false;
};

// Deep portal-prop seam: one descriptor assembles frame, opaque animated
// membrane, light, particles and a correctly transformed tooltip anchor.
PortalProp createPortalProp(eng::Renderer& renderer, glm::vec3 floorPosition,
                            const PortalPropStyle& style = {});

// Loads a TOML-authored primitive/material gallery. Returns false and logs a
// useful error if the file is malformed; individual unknown shapes are skipped.
bool loadPrimitiveShowcase(eng::Renderer& renderer, const std::string& path,
                           std::vector<ShowcaseExhibit>& loaded);
