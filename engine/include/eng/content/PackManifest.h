#pragma once

#include <eng/content/AssetType.h>
#include <eng/content/ContentHash.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// What the Asset Conditioning Pipeline hands to the game.
//
// One file, `pack.manifest`, at the root of the cooked directory: every asset
// the pipeline produced, what it was made from, and the build key that says
// whether it is current. It is the boundary the diagram draws as the arrow from
// the grey ACP box to GAME -- everything upstream of it is build machinery the
// runtime never links.
//
// TOML rather than a binary index. It is read once at start-up and it is the
// first thing anyone looks at when an asset does not show up, so being able to
// grep it is worth more than the microseconds a binary format would save. The
// assets it points at are binary; the index over them does not have to be.
namespace eng::content {

inline constexpr const char* kPackManifestName = "pack.manifest";

struct PackEntry {
    Hash guid = 0;
    AssetType type = AssetType::Unknown;
    // The source this came from, content-root-relative: "meshes/props/lamp.obj".
    // This is the key a runtime lookup uses, because that is the name the rest
    // of the engine has always used for an asset.
    std::string source;
    // The produced file, relative to the pack directory:
    // "meshes/props/lamp.rmesh". Equal to `source` for the rows the diagram
    // routes straight through (Material, Sound, Script).
    std::string output;
    // sourceHash + settings + exporter version. Recorded so the pipeline can
    // answer "is this current?" without re-reading every source, and so a
    // shipped pack can be diffed against the tree it claims to be built from.
    Hash buildKey = 0;
    uint64_t outputBytes = 0;
    // Other content-root-relative files this asset read: an .obj's .mtl, a
    // sound bank's clips, a kit template's meshes. Recorded so the next run can
    // key on them without re-running the exporter to find out what they were,
    // which is the difference between an incremental build that is correct and
    // one that only looks it.
    std::vector<std::string> dependencies;
};

class PackManifest {
public:
    // Absolute directory the outputs live in. Set by load(); the entries'
    // `output` paths are relative to it.
    const std::filesystem::path& directory() const { return mDirectory; }
    const std::vector<PackEntry>& entries() const { return mEntries; }

    void setDirectory(std::filesystem::path directory);
    void add(PackEntry entry);
    // Upsert by source. What a partial build needs: it starts from the previous
    // manifest so the assets it did not look at stay in the index, and replaces
    // just the ones it rebuilt.
    void set(PackEntry entry);
    void clear();

    const PackEntry* bySource(std::string_view logical) const;
    const PackEntry* byGuid(Hash guid) const;

    // The absolute path of an asset's output, or an empty path when the pack
    // has no entry for it. This is the one call the runtime makes.
    std::filesystem::path resolve(std::string_view logical) const;

    bool load(const std::filesystem::path& directory, std::string& error);
    bool save(const std::filesystem::path& directory, std::string& error) const;

private:
    void reindex();

    std::filesystem::path mDirectory;
    std::vector<PackEntry> mEntries;
    std::map<std::string, size_t, std::less<>> mBySource;
    std::map<Hash, size_t> mByGuid;
};

} // namespace eng::content
