#pragma once
#include <eng/Reflect.h>

#include <glm/glm.hpp>

namespace eng::ecs {

// The portal shader's knobs, on one entity.
//
// The first shader block (see eng/ShaderBlock.h) and the reason that mechanism
// exists: `portal.frag` has thirty uniforms, they were tunable only through a
// debug panel that wrote them onto the *shared* material -- so every portal in
// the level moved together -- and none of it could be saved in a scene. A level
// with a slow green descent and a fast violet ascent was not authorable; it was
// two materials, hand-written.
//
// **Every field is named exactly after its uniform.** That is the contract the
// generic push relies on: the renderer reads the field table and calls
// setNamedConstant with the field's own name, so there is no mapping table to
// get wrong. It also makes this header a readable index of what the shader
// takes -- `assets/shaders/portal_pattern.glsl` documents what each one does,
// and the defaults here are the ones in `assets/programs/vfx.program`, so a
// component added and left alone renders exactly as the material always did.
//
// Absent uniforms are skipped silently: the same component can sit on a portal
// and on the wisp material beside it, and each takes the constants it declares.
struct PortalParams {
    // --- motion ---
    float portalFlowSpeed = 0.35f;  // inward drift; negative flows outward
    float portalSwirlSpeed = 0.09f; // rotation of the whole field
    float portalTwist = 0.18f;      // spiral tightness (shear near the centre)
    float portalArms = 3.0f;        // arm count; whole numbers stay seamless
    float portalArmWidth = 0.5f;    // arm vs gap; 0.5 is the raw sine

    // --- depth ---
    float portalDepthScale = 0.70f; // log-polar compression: tunnel length
    float portalParallax = 0.35f;   // metres of depth per layer
    float portalFieldWeight = 0.45f; // flow field vs analytic spiral, 0..1

    // --- the eye ---
    float portalCoreRadius = 0.10f; // event horizon, in fractions of height
    float portalCoreBoost = 0.85f;  // how hard the core burns through

    // --- containment ring ---
    float portalRimRadius = 0.44f;
    float portalRimWidth = 0.035f;
    float portalRimIntensity = 0.70f; // 0 removes it
    float portalEdgeFade = 0.045f;    // fades the quad's square corners

    // --- palette ---
    // Four stops the pattern ramps through, darkest to hottest. Shared with the
    // other surface shaders (liquid, lava), which is why they are `surface*`
    // rather than `portal*`.
    glm::vec3 surfaceDark{0.05f, 0.24f, 0.04f};
    glm::vec3 surfaceMid{0.20f, 1.00f, 0.10f};
    glm::vec3 surfaceBright{0.80f, 1.70f, 0.24f};
    glm::vec3 surfaceCore{1.30f, 2.20f, 0.55f};

    // --- presentation ---
    float surfaceStepFps = 12.0f;    // the stop-motion rate of the membrane
    float surfaceTexelSize = 0.055f; // metres per portal pixel
    float surfacePixelGrid = 48.0f;
    float surfaceDither = 0.60f;
    float surfaceBrightness = 1.0f;

    // --- bloom ---
    // What the portal *blooms*, separate from what colour it is: bloom has no
    // colour of its own, it blurs whatever exceeds its threshold. Without this
    // the palette would have to be both the portal's colour and its glow.
    glm::vec3 surfaceGlowColour{0.55f, 1.90f, 0.35f};
    float surfaceGlowStrength = 0.85f;
    float surfaceGlowThreshold = 0.55f;
};

} // namespace eng::ecs

namespace eng {
// Declared here, defined in ComponentRegistry.cpp. Without the declaration a
// translation unit that only *uses* the block -- SceneSync -- instantiates the
// primary template instead of referring to the specialisation, and the failure
// is an undefined symbol at link rather than anything at the call site.
template <> FieldSpan fieldsOf<ecs::PortalParams>();
} // namespace eng
