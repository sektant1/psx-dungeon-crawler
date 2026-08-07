#pragma once
#include <eng/particles/ParticleTypes.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace eng {

// The renderer-neutral half of the particle texture import: scan a directory of
// PNGs, merge the *.toml overrides over them, and answer stem -> description.
// Nothing here knows what a material is, which is why both the old backend
// (via ParticleMaterials, which adds material generation on top) and the RHI
// backend (which binds the PNG directly) can share one catalogue instead of
// parsing the same tables twice.
//
// A texture may be declared without a file of its own: an entry naming a
// `sheet` carves one animation strip out of a shared PNG. That is how a bought
// effect pack with hundreds of animations packed into a dozen sheets becomes
// hundreds of named textures without hundreds of files.
class ParticleTextureCatalog {
public:
    // Pack-qualified: the particle tables are engine content, and a game or
    // editor pack that happened to carry a `particles/` folder must not be able
    // to take the directory over by outranking the engine in the mount order.
    static std::string defaultRoot();

    // Scans `root`/textures for *.png and parses every *.toml directly in
    // `root`, in name order, with later files winning a duplicate stem.
    void load(const std::string& root = defaultRoot());

    // Re-runs the scan against the root last passed to load(), picking up new
    // files and edited overrides. Returns false if load() was never called.
    bool reload();

    // Null when no such texture was declared.
    const ParticleTextureDesc* find(const std::string& stem) const;

    // Stable ordering (directory scan order is not), for editor listings.
    const std::vector<ParticleTextureDesc>& all() const { return mDescs; }

    const std::string& root() const { return mRoot; }
    bool loaded() const { return mLoaded; }

    // The PNG leaf a description binds: its own file, or "<stem>.png".
    static std::string fileFor(const ParticleTextureDesc& desc)
    {
        return desc.file.empty() ? desc.stem + ".png" : desc.file;
    }

    // Absolute path for a description's PNG. Entries name a bare leaf
    // ("bullet16.png") even when the file lives in a subdirectory: textures
    // resolve by leaf name across the recursively registered resource
    // directories, and the scan indexes every PNG under textures/ the same way
    // so a backend that opens files directly agrees with that contract.
    // Empty when no such file was found.
    std::string pathFor(const ParticleTextureDesc& desc) const;

private:
    void scan();

    std::string mRoot;
    bool mLoaded = false;
    std::vector<ParticleTextureDesc> mDescs;
    std::unordered_map<std::string, size_t> mByStem;
    // Leaf name -> absolute path, for every PNG under textures/.
    std::unordered_map<std::string, std::string> mFilesByLeaf;
};

} // namespace eng
