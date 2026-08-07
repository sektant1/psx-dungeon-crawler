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

// Sub-image animation over the particle's life.
//
// The texture is cut into a sheetCols x sheetRows grid of cells. One flipbook
// is a run of `frames` cells starting at (originCol, originRow), advancing
// along the row and wrapping down after `perRow` of them. That indirection is
// what lets many animations share one PNG: a purchased effect sheet packs
// dozens of strips into a single image, and slicing it into dozens of files
// just to name them would multiply both the disk and the texture bindings for
// nothing.
//
// The degenerate case is the old whole-sheet grid -- origin (0,0), frames =
// sheetCols*sheetRows, perRow = sheetCols -- which is exactly what the TOML
// `rows`/`cols` keys still produce.
struct FlipbookDesc {
    int sheetCols = 1, sheetRows = 1;
    int originCol = 0, originRow = 0;
    int frames = 1;
    int perRow = 0;      // 0 = a single unwrapped strip of `frames`
    float fps = 0.0f;
    bool loop = true;

    bool active() const { return frames > 1 && fps > 0.0f; }
    int frameCount() const { return frames > 0 ? frames : 1; }
    int framesPerRow() const { return perRow > 0 ? perRow : frameCount(); }

    // What the vertex program consumes. Kept here rather than in the material
    // builder so the UV math has one definition that a test can reach without
    // a live renderer.
    float cellU() const { return 1.0f / float(sheetCols > 0 ? sheetCols : 1); }
    float cellV() const { return 1.0f / float(sheetRows > 0 ? sheetRows : 1); }
    float originU() const { return float(originCol) * cellU(); }
    float originV() const { return float(originRow) * cellV(); }
};

// Per-texture presentation, resolved from the *.toml files in
// assets/particles/. The generated material is named "Particles/Auto/<stem>".
struct ParticleTextureDesc {
    std::string stem;              // the name effects reference
    // File the material binds. Empty means "<stem>.png", which is the case for
    // every texture discovered by scanning the directory; a sheet-backed entry
    // names a PNG that several stems share.
    std::string file;
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
