#pragma once

#include "ShowcaseVisibility.h"
#include "combat/CombatVocabulary.h"

#include <eng/Handles.h>

#include <glm/glm.hpp>

#include <algorithm>

#include <string>
#include <unordered_map>
#include <vector>

namespace eng { class Renderer; }

// Reusable descriptors/results for composed scene objects. Primitive meshes
// remain in Renderer; factories assemble them into game-facing prefabs.
struct PortalPropStyle {
    std::string material = "Game/PortalDown";
    // The surround is built from kit meshes, so it wears the kit's atlas. This
    // used to be beveled-box primitives under an atlas material, whose 0..1
    // per-face UVs stretched the *whole* dungeon sheet over every block.
    std::string frameMaterial = "Kit/Dungeon";
    // Directory holding the kit .obj files, trailing slash included. Empty
    // leaves the membrane bare (a caller that supplies its own surround).
    std::string kitMeshDir;
    std::string particles = "engine.portal_wisps";
    glm::vec3 lightColour{1.05f, 0.20f, 1.45f};
    float yawDegrees = 0.0f;
    float lightRange = 5.5f;
    // The membrane is a rectangle, in metres, standing on the floor -- a quad,
    // not an ellipse, which would leave the corners of its surround open.
    // Deliberately larger than the kit's 2.0 x 2.25 doorways: the portal is the
    // room's landmark, not another door. Sized with the surround to fit one 4 m
    // cell under a 3 m ceiling: fieldSize.x + 2*pillar <= 4, fieldSize.y +
    // head <= 3.
    glm::vec2 fieldSize{2.8f, 2.5f};
    // Offset along the portal's local +Z, which faces into the room. Positive
    // keeps the membrane clear of the wall face it overhangs.
    float membraneInset = 0.06f;
    // The membrane is a slab, not a sheet: edge on, a zero-thickness field
    // vanished, and its rim now catches light against the arch.
    float membraneThickness = 0.14f;
    // A stone panel closing the surround from behind. Without it the portal is
    // a picture frame: walk around it and the arch is a hole with the room on
    // the other side of it, and the membrane's lit face floats in the gap.
    //
    // It also decides how tall the membrane is. The kit Arch's soffit is a
    // curve, so a membrane that stops at the opening's top edge leaves a dark
    // crescent under the stones -- with a panel behind, that crescent is filled
    // by the panel and the membrane can stop just inside the springline, where
    // the soffit is still wider than it is. Without one the membrane has to run
    // the full height of the surround to fill the crescent itself, and pays for
    // it with the square top corners that spill past the curve.
    bool backing = true;
    // Plain tiling stone, NOT the kit atlas: a generated quad carries 0..1 UVs
    // over its whole face, so an atlas material stretches the entire sheet
    // across it and the panel reads as a collage of unrelated wall bits.
    std::string backingMaterial = "Kit/Stone";
    // How far the membrane's top edge tucks past the springline, so the two
    // meet with no seam. Small on purpose: the soffit narrows fast, and past
    // roughly a tenth of a metre the corners are outside the curve again.
    float membraneHeadOverlap = 0.06f;
    // Surround: a kit Pillar either side of the opening and a kit Arch across
    // the top. Sized so pillars + opening stay inside one 4 m cell.
    float framePillarWidth = 0.50f;
    float frameHeadHeight = 0.45f;
    float frameDepth = 0.42f;
    glm::vec3 labelOffset{0.0f, 2.82f, 0.10f};
};

// Where the portal's surfaces sit along its local +Z, which faces into the
// room. Pulled out of createPortalProp as a pure function because the one thing
// that has repeatedly gone wrong here is depth ordering -- the membrane showing
// past the stone meant to hide it -- and that is arithmetic, testable without a
// renderer, rather than something to re-check by eye in a screenshot.
struct PortalDepths {
    float panelBack = 0.0f;     // backing slab, rear face
    float panelFront = 0.0f;    // backing slab, front face
    float membraneBack = 0.0f;  // membrane slab, rear face
    float membraneFront = 0.0f; // membrane slab, front face
    float pillarBack = 0.0f;    // pillar bounding box, rear face

    float backingGap() const { return membraneBack - panelFront; }
};

inline PortalDepths portalDepths(const PortalPropStyle& style)
{
    PortalDepths d;
    const float half = style.membraneThickness * 0.5f;
    d.membraneBack = style.membraneInset - half;
    d.membraneFront = style.membraneInset + half;
    d.pillarBack = -style.framePillarWidth * 0.5f;
    // Clear of the pillars' back faces, so the slab reads as a wall closing the
    // surround rather than as a panel jammed between the posts.
    d.panelBack = d.pillarBack - 0.02f;
    // Forward to just short of the membrane. The gap it leaves is deliberate
    // and small: coplanar faces would z-fight, but anything wider is an open
    // slot, and the kit Pillar is a round shaft whose radius is about 0.71 of
    // its bounding box -- it plugs neither the corners of its own footprint nor
    // the space behind it, so an oblique view looks straight through into the
    // membrane's lit edge.
    d.panelFront = std::max(d.membraneBack - 0.02f, d.panelBack + 0.05f);
    return d;
}

struct PortalProp {
    eng::NodeHandle root{};
    eng::NodeHandle field{};
    eng::NodeHandle labelAnchor{};
    eng::LightHandle light{};
    glm::vec3 labelWorldPosition{0.0f};
    // The membrane's size as built, so the interaction target is derived from
    // the thing the player is looking at rather than tuned alongside it.
    glm::vec2 opening{0.0f};

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
                           const game::CombatVocabulary& vocabulary,
                           const std::unordered_map<std::string, glm::vec3>&
                               placements);

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
TreasureShrine buildTreasureShrine(eng::Renderer& r,
                                   const std::string& propMeshDir,
                                   glm::vec3 origin);

// Fresh reward-ring assembly for authored scenes. Unlike DemoScene's imported
// composition, this is placed explicitly by a scene marker.
void buildCrystalRing(eng::Renderer& r, const std::string& meshDir,
                      glm::vec3 origin);

// Grounds the DemoScene's two omni lamps in open barrels with a flame on the
// rim, and lifts the light just above the flames. Moves the passed omni nodes.
void buildBraziers(eng::Renderer& r, const std::string& propMeshDir,
                   eng::NodeHandle omniA, eng::NodeHandle omniB);
