#pragma once
#include <eng/Handles.h>
#include <eng/particles/ParticleEffectDesc.h>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace eng {
class Renderer;

// Parses particles.toml into ParticleEffectDescs, registers them with the
// Renderer, and resolves names -> ParticleEffectId. Holds live descs so the
// editor panel can edit them in place and write them back out.
//
// Identity is the effect *name*, never the array index: the Renderer keys its
// effect table by name too, so re-registering an edited or reloaded desc keeps
// the same ParticleEffectId and every live instance holding it stays valid.
// That property is what makes hot-reload safe, and it is why reordering
// [[effect]] blocks in the file is harmless.
class ParticleLibrary {
public:
    bool load(eng::Renderer& r, const std::string& tomlPath);

    // Re-reads the file the last load() came from, but only when its mtime
    // moved. Cheap enough to call every frame, which is the point: the editor
    // should not need a reload button for the common case of editing the TOML
    // in another window. Returns true only when a reparse actually happened
    // and succeeded; a parse error leaves the previous descs in place, because
    // a half-saved file must not blank the effect table mid-session.
    bool reloadIfChanged(eng::Renderer& r);

    // Serialises the current descs back to TOML. Round-trip exact: every field
    // is written unconditionally with enough precision to reparse to the same
    // bits, so the editor's save-back cannot silently drop a value it did not
    // happen to display. Comments and hand-authored formatting in the source
    // file are *not* preserved -- saving from the panel rewrites the file.
    bool save(const std::string& path) const;

    eng::ParticleEffectId id(const std::string& name) const;

    std::vector<ParticleEffectDesc>& descs() { return mDescs; }
    const std::vector<ParticleEffectDesc>& descs() const { return mDescs; }
    const std::vector<eng::ParticleEffectId>& ids() const { return mIds; }

    // Path the last successful load() read, so callers (and save()) do not have
    // to remember it themselves.
    const std::string& path() const { return mPath; }

    void reregister(eng::Renderer& r, size_t index);
    // Renderer::clearScene drops GPU batches and effect ids. Definitions remain
    // here, so a rebuilt level can restore them without reparsing source data.
    void reregisterAll(eng::Renderer& r);

private:
    // Shared by load() and reloadIfChanged(): parse the file into descs without
    // touching any member state, so a failure is a no-op for the caller.
    static bool parseFile(const std::string& path,
                          std::vector<eng::ParticleEffectDesc>& out);
    void adopt(eng::Renderer& r, std::vector<eng::ParticleEffectDesc>& parsed);

    std::vector<eng::ParticleEffectDesc> mDescs;
    std::vector<eng::ParticleEffectId>   mIds;   // parallel to mDescs
    std::unordered_map<std::string, size_t> mByName;

    std::string mPath;
    std::filesystem::file_time_type mMtime{};
};

} // namespace eng
