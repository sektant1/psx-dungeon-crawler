#pragma once
#include <eng/DebugTools.h> // eng::DebugTools, the panel host these join

#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <vector>

namespace eng {

class Renderer;

// Live tuning for the engine's surface shader family: the portal membrane
// (portal.frag over surface_common.glsl) and the scrolling liquids
// (liquid.frag, lava.frag over scroll_common.glsl).
//
// These panels used to live in the game, which meant the demo -- whose whole
// job is to show a renderer feature next to the profile it renders under -- had
// a portal on screen and no way to touch it. The shaders are engine assets and
// the knobs are their uniforms, so the panel is engine tooling too; what stays
// with the game is only what the game owns, which is the portal *prop* (its
// light and its wisps) and that arrives through setPortalDressing().
//
// Nothing here caches renderer state. Every widget writes straight through to
// the material with Renderer::setMaterialParam, so an edit lands on the next
// frame and the material is the only copy of the value that matters. The
// structs below are UI-side only: they exist so a slider has somewhere to keep
// the number between frames, and they are seeded from the values the material
// ships with so the first touch of a slider does not snap the look elsewhere.

// One portal material's knobs, mirroring the defaults in
// assets/engine/programs/vfx.program.
struct PortalTuning {
    glm::vec4 dark{0.06f, 0.30f, 0.05f, 1.0f};
    glm::vec4 mid{0.16f, 1.05f, 0.07f, 1.0f};
    glm::vec4 bright{0.85f, 1.80f, 0.20f, 1.0f};
    glm::vec4 core{1.55f, 2.40f, 0.70f, 1.0f};
    float stepFps = 12.0f;
    float flowSpeed = 0.32f;
    float swirlSpeed = 0.09f;
    float twist = 0.18f;
    float arms = 3.0f;
    float armWidth = 0.5f;
    float texelSize = 0.055f;
    float pixelGrid = 48.0f;
    float depthScale = 0.70f;
    float parallax = 0.35f;
    float fieldWeight = 0.45f;
    float coreRadius = 0.10f;
    float coreBoost = 0.85f;
    float rimRadius = 0.44f;
    float rimWidth = 0.035f;
    float rimIntensity = 0.70f;
    float edgeFade = 0.045f;
    float dither = 0.60f;
    float edgeGlow = 0.85f;
    float edgeFlow = 0.50f;
    int edgeMode = 0;
    // Emission: the colour the portal blooms in, and how hard. Separate from
    // the palette because bloom has no colour of its own -- it blurs whatever
    // exceeds its threshold, in that pixel's colour -- so without this the
    // palette has to be both the portal's colour and its glow at once.
    glm::vec4 glowColour{1.30f, 0.10f, 2.20f, 1.0f};
    float glowStrength = 1.05f;
    float glowThreshold = 0.50f;
    float brightness = 1.0f;
};

// One scrolling-liquid material's knobs (water, slime). liquid.frag is the
// scrolling half of the surface split: palette plus two tiling layers sliding
// at different rates.
struct LiquidTuning {
    glm::vec4 dark{0.015f, 0.07f, 0.20f, 1.0f};
    glm::vec4 mid{0.03f, 0.42f, 0.92f, 1.0f};
    glm::vec4 bright{0.22f, 0.92f, 1.20f, 1.0f};
    glm::vec2 flowA{0.07f, 0.035f};
    glm::vec2 flowB{-0.035f, 0.055f};
    float stepFps = 8.0f;
    float pixelGrid = 32.0f;
    float emission = 0.0f;
};

// Lava's knobs. A separate struct rather than a third LiquidTuning because lava
// is a four-tone palette with one flow speed, not a three-tone palette with two
// flow vectors -- lava.frag warps its own domain instead of crossing two
// scrolling layers.
struct LavaTuning {
    glm::vec4 dark{0.10f, 0.003f, 0.001f, 1.0f};
    glm::vec4 crust{0.42f, 0.018f, 0.003f, 1.0f};
    glm::vec4 hot{1.70f, 0.30f, 0.012f, 1.0f};
    glm::vec4 core{2.15f, 1.02f, 0.10f, 1.0f};
    float stepFps = 10.0f;
    float pixelGrid = 40.0f;
    float flowSpeed = 0.24f;
};

// The bloom knobs, mirrored on the Portal tab. Bloom is a GLOBAL post-process
// -- one pass over the whole frame, owned by the render profile -- but it is
// what turns the portal's above-1.0 tones into glow, so tuning the portal
// without it means tuning half the effect. One copy for every profile, because
// there is only one bloom and a per-profile copy would let the selector appear
// to change something it does not own.
struct BloomTuning {
    bool enabled = true;
    float threshold = 0.56f;
    float intensity = 0.72f;
};

// The Portal and VFX panels. An app registers the materials it actually has --
// the game two portals and three liquids, the demo whichever of those its
// showcase puts on screen -- and the panels are skipped entirely when nothing
// is registered, so an app that owns no surface shader pays nothing.
class SurfacePanels {
public:
    // `label` is what the selector shows; `material` is the identity, because
    // the knobs are shader uniforms shared by every mesh wearing it -- which is
    // also why the tuning survives a level rebuild. `texture` is only used by
    // the Portal tab's "copy as material", which has to emit a whole material
    // block rather than a bare param list.
    void addPortal(std::string label, std::string material,
                   PortalTuning tuning = {}, std::string texture = {});
    void addLiquid(std::string label, std::string material,
                   LiquidTuning tuning = {});
    void addLava(std::string label, std::string material,
                 LavaTuning tuning = {});

    // An app-owned section drawn at the bottom of the Portal tab, called with
    // the selected profile's index. The game uses it for the portal prop's
    // dressing -- the light it throws into the room and the wisps drifting in
    // front of it -- which is level state, not shader state.
    void setPortalDressing(std::function<void(int)> draw);

    // Seeds the bloom mirror from the profile the app starts in, so the first
    // touch of that slider does not jump the whole frame.
    void setBloom(BloomTuning bloom) { mBloom = bloom; }

    void install(DebugTools& tools);
    // Per-frame, like every other panel dependency here: a renderer pointer
    // captured once would outlive an app that rebuilds one.
    void setRenderer(Renderer* renderer) { mRenderer = renderer; }

    bool empty() const { return mPortals.empty() && mVfx.empty(); }

private:
    struct PortalProfile {
        std::string label;
        std::string material;
        std::string texture;
        PortalTuning tuning;
    };
    // Water, slime and lava share one selector because they share a tab; they
    // do not share a struct, so the kind decides which half of the tab draws.
    struct VfxProfile {
        enum class Kind { Liquid, Lava };
        Kind kind = Kind::Liquid;
        std::string label;
        std::string material;
        LiquidTuning liquid;
        LavaTuning lava;
    };

    void drawPortalTab();
    void drawVfxTab();

    Renderer* mRenderer = nullptr;
    std::vector<PortalProfile> mPortals;
    std::vector<VfxProfile> mVfx;
    std::function<void(int)> mPortalDressing;
    BloomTuning mBloom;
    int mPortalIdx = 0;
    int mVfxIdx = 0;
};

} // namespace eng
