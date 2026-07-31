#pragma once
#include <eng/particles/ParticleTypes.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace eng {

// Turns a directory of PNGs into Ogre materials so that dropping a file into
// assets/particles/textures/ is the entire authoring workflow. Effects then
// reference the file *stem* ("ember"), never an Ogre material name, which keeps
// particles.toml free of renderer vocabulary and means a texture can change its
// blend mode without every effect that uses it being edited.
//
// The generated material is deliberately minimal -- unlit, depth-write off,
// depth-check on, culling off, point filtering -- because that is what every
// particle wants and because the PSX look forbids bilinear smoothing by
// default. Anything more elaborate stays the job of a hand-authored material,
// which an effect can still name explicitly.
class ParticleMaterials {
public:
    // Directory that holds textures/ and textures.toml. Defaults to the engine
    // asset root's particles/ folder, which RenderCore has already registered
    // as an Ogre resource location, so the PNGs resolve by leaf name without
    // any extra plumbing. The path is a build-time define, never a literal.
    static std::string defaultRoot();

    // Scans `root`/textures for *.png, parses `root`/textures.toml, and creates
    // or updates one material per file. Safe to call before any effect loads.
    void load(const std::string& root = defaultRoot());

    // Re-runs the scan against the root last passed to load(), picking up new
    // files and edited overrides. Existing materials are updated in place so
    // that anything already holding a MaterialPtr keeps working across a
    // hot-reload. Returns false if load() was never called.
    bool reload();

    // Null when no such PNG was found. Callers that only need presentation
    // metadata (flipbook, soft fade) should go through this rather than
    // re-reading the TOML.
    const ParticleTextureDesc* find(const std::string& stem) const;

    // Ogre material name for a stem, or an empty string when the stem is
    // unknown -- an empty name lets the caller fall back to its own prototype
    // material instead of this class deciding on a fallback it cannot see.
    std::string materialFor(const std::string& stem) const;

    // Stable ordering (directory scan order is not), for editor listings.
    const std::vector<ParticleTextureDesc>& all() const { return mDescs; }

private:
    void scan();

    std::string mRoot;
    bool mLoaded = false;
    std::vector<ParticleTextureDesc> mDescs;
    std::unordered_map<std::string, size_t> mByStem;
};

} // namespace eng
