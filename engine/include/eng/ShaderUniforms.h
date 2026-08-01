#pragma once
#include <glm/glm.hpp>

namespace eng {

// The per-entity uniforms this engine's shader family exposes.
//
// Lives here rather than with the ECS components because it is a *rendering*
// type: every field names a constant declared in `assets/shaders/psx.frag`, and
// the renderer sets it. `eng::ecs::ShaderParams` is this struct under the name
// the scene uses, the same way `LightRef` carries a `LightDesc`.
//
// What each field drives:
//
//   tint         `modulateColor`, multiplied into the albedo before lighting.
//                White is "unchanged". This is the damage flash, the quest
//                highlight, the faction recolour.
//   opacity      the alpha of that same uniform. Below 1 the material must
//                already blend, or the fragment is simply scissored.
//   rimColour    `rimColour.rgb` -- a sheen at glancing angles (fresnel), which
//                is what reads as "magical" or "wet" on a low-poly silhouette.
//   rimStrength  `rimColour.a`. 0 is off, and off is the default: a rim on
//                everything is a scene with no focus.
//   rimPower     the falloff exponent. Low is a broad wash, high a thin edge.
//   alphaScissor cutout threshold: above it the fragment is kept, below it
//                discarded. Foliage, grates, a dissolve driven by gameplay.
//
// Deliberately NOT a free-form map of name -> value. A string-keyed uniform bag
// looks more general and is worse: a typo is silent (the shader keeps its
// default), an inspector cannot know what to draw, and the set of valid names
// depends on which shader variant the entity happens to use. These six are what
// the shipped family declares, which is what makes them editable, saveable and
// checkable.
struct ShaderUniforms {
    glm::vec3 tint{1.0f, 1.0f, 1.0f};
    float opacity = 1.0f;
    glm::vec3 rimColour{0.55f, 0.75f, 1.0f};
    float rimStrength = 0.0f;
    float rimPower = 3.0f;
    float alphaScissor = 0.0f;
};

} // namespace eng
