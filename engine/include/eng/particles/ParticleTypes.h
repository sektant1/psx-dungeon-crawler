#pragma once
#include <string>

namespace eng {

// How a particle is drawn. Both modes go through the same instancing path --
// only the base mesh and the fragment program differ -- so an effect can be
// switched between them from TOML without touching code.
//
// Voxel is not a volumetric renderer. It is an instanced cube with flat
// per-face shading, six fixed tones derived from the face normal. That is
// deliberate: it costs nothing, it needs no dynamic lighting, and it reads as
// chunky gore against a PSX-era image in a way a raymarched puff never would.
enum class ParticleRenderMode { Sprite, Voxel };

// What happens when a particle's swept segment hits world geometry. Only
// particles whose effect opts in are traced at all.
enum class ParticleCollideResponse {
    None,   // never traced
    Die,    // retire on contact
    Bounce, // reflect, scaled by restitution/friction
    Decal,  // retire and leave a projected mark (blood, scorch)
};

enum class ParticleBlend { Alpha, Additive };

// Sub-image animation over the particle's life. rows*cols frames are walked at
// `fps`; when `loop` is false the last frame holds for the remaining lifetime.
struct FlipbookDesc {
    int rows = 1;
    int cols = 1;
    float fps = 0.0f;
    bool loop = true;

    bool active() const { return rows * cols > 1 && fps > 0.0f; }
    int frameCount() const { return rows * cols; }
};

// Per-texture presentation, resolved from assets/particles/textures.toml. The
// generated material is named "Particles/Auto/<stem>".
struct ParticleTextureDesc {
    std::string stem;              // file stem, the name effects reference
    ParticleBlend blend = ParticleBlend::Alpha;
    FlipbookDesc flipbook;
    bool softFade = false;
    bool nearest = true;           // PSX default; bilinear is opt-out
};

inline std::string particleAutoMaterialName(const std::string& stem)
{
    return "Particles/Auto/" + stem;
}

} // namespace eng
