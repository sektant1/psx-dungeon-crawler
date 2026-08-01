#pragma once
#include <eng/particles/ParticleTypes.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eng {

// Turns a directory of PNGs into Ogre materials so that dropping a file into
// assets/particles/textures/ is the entire authoring workflow. Effects then
// reference the file *stem* ("ember"), never an Ogre material name, which keeps
// particles.toml free of renderer vocabulary and means a texture can change its
// blend mode without every effect that uses it being edited.
//
// A texture may also be declared without a file of its own: an entry naming a
// `sheet` carves one animation strip out of a shared PNG. That is how a bought
// effect pack with a couple of hundred animations packed into a dozen sheets
// becomes a couple of hundred named textures without a couple of hundred files
// and a couple of hundred texture bindings.
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

    // Scans `root`/textures for *.png and parses every *.toml directly in
    // `root`. Safe to call before any effect loads. Materials themselves are
    // NOT created here: a pack can declare hundreds of strips, and paying an
    // Ogre material for every one of them at boot would be a startup cost for
    // content the level never spawns. They are built on first use instead.
    void load(const std::string& root = defaultRoot());

    // Re-runs the scan against the root last passed to load(), picking up new
    // files and edited overrides. Materials already built are rebuilt in place
    // so that anything holding a MaterialPtr keeps working across a hot-reload;
    // the rest stay unbuilt. Returns false if load() was never called.
    bool reload();

    // Null when no such texture was declared. Callers that only need
    // presentation metadata (flipbook, soft fade) should go through this rather
    // than re-reading the TOML.
    const ParticleTextureDesc* find(const std::string& stem) const;

    // Ogre material name for a stem, creating the material if this is its first
    // use. Empty when the stem is unknown -- an empty name lets the caller fall
    // back to its own prototype material instead of this class deciding on a
    // fallback it cannot see.
    std::string materialFor(const std::string& stem);

    // Stable ordering (directory scan order is not), for editor listings.
    const std::vector<ParticleTextureDesc>& all() const { return mDescs; }

private:
    void scan();
    // Creates or refreshes the Ogre material for one texture and records it as
    // built. Idempotent.
    void build(const ParticleTextureDesc& desc);

    std::string mRoot;
    bool mLoaded = false;
    std::vector<ParticleTextureDesc> mDescs;
    std::unordered_map<std::string, size_t> mByStem;
    std::unordered_set<std::string> mBuilt;
};

} // namespace eng
