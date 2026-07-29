#pragma once

#include "ShowcaseVisibility.h"
#include "combat/CombatVocabulary.h"

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
    std::string particles = "engine.portal_wisps";
    glm::vec3 lightColour{1.05f, 0.20f, 1.45f};
    float yawDegrees = 0.0f;
    float lightRange = 5.5f;
    float innerRadius = 1.0f;
    float frameWidth = 0.34f;
    float frameDepth = 0.30f;
    float frameBevel = 0.08f;
    glm::vec2 fieldScale{1.90f, 1.55f};
    float height = 1.42f;
    float membraneInset = -0.035f;
    glm::vec3 frameScale{1.0f};
    glm::vec3 labelOffset{0.0f, 2.82f, 0.10f};
    int segments = 18;
};

struct PortalProp {
    eng::NodeHandle root{};
    eng::NodeHandle field{};
    eng::NodeHandle labelAnchor{};
    eng::LightHandle light{};
    glm::vec3 labelWorldPosition{0.0f};

    bool valid() const
    {
        return root.valid() && field.valid() && labelAnchor.valid();
    }
};

struct ShowcaseExhibit {
    eng::NodeHandle root{};
    std::string id;
    std::string label;
    std::string labelHighlightPattern;
    glm::vec4 labelAccent{0.88f, 0.58f, 0.12f, 1.0f};
    glm::vec4 labelHighlight{1.0f, 0.78f, 0.22f, 1.0f};
    glm::vec3 labelOffset{0.0f};
    glm::vec3 position{0.0f};
    glm::vec3 halfExtents{0.0f};
    float visibilityRange = 0.0f;
    ShowcaseVisibilityState visibility =
        ShowcaseVisibilityState::Uninitialized;
    bool blocksMovement = false;
};

// Deep portal-prop seam: one descriptor assembles frame, opaque animated
// membrane, light, particles and a correctly transformed tooltip anchor.
PortalProp createPortalProp(eng::Renderer& renderer, glm::vec3 floorPosition,
                            const PortalPropStyle& style = {});

// Loads a TOML-authored primitive/material gallery. Returns false and logs a
// useful error if the file is malformed; individual unknown shapes are skipped.
// `vocabulary` resolves the `enchantment` key -- a school name authored in the
// document -- into the palette the renderer takes.
bool loadPrimitiveShowcase(eng::Renderer& renderer, const std::string& path,
                           std::vector<ShowcaseExhibit>& loaded,
                           const game::CombatVocabulary& vocabulary);

// Levitating treasure chest + offerings + warm glow. The chest/glow are
// animated by the caller, so their handles come back in the result.
struct TreasureShrine {
    eng::NodeHandle chestBase{};
    eng::NodeHandle chestSpin{};
    // The warm glow is spawned by the caller as an ECS light actor (so its
    // pulse is driven through the registry), using these authored values.
    glm::vec3 chestGlowColour{0.0f};
    float glowRange = 6.0f;
};
TreasureShrine buildTreasureShrine(eng::Renderer& r, const std::string& propMeshDir);

// Grounds the DemoScene's two omni lamps in open barrels with a flame on the
// rim, and lifts the light just above the flames. Moves the passed omni nodes.
void buildBraziers(eng::Renderer& r, const std::string& propMeshDir,
                   eng::NodeHandle omniA, eng::NodeHandle omniB);
