#include "ParticleTextureCatalog.h"

#include <eng/Log.h>
#include <eng/assets/AssetRoot.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace {

// The overrides file is optional: a texture with no entry is not an error, it
// simply takes the defaults. Only a file that exists and fails to parse is
// worth shouting about, since that is a typo the author wants to hear about.
struct Overrides {
    std::unordered_map<std::string, eng::ParticleTextureDesc> byStem;
};

eng::ParticleBlend parseBlend(const std::string& blend, const std::string& stem)
{
    if (blend == "alpha") return eng::ParticleBlend::Alpha;
    if (blend == "additive") return eng::ParticleBlend::Additive;
    eng::log::warn("ParticleTextures: texture '%s' has unknown blend '%s'; "
                   "using alpha",
                   stem.c_str(), blend.c_str());
    return eng::ParticleBlend::Alpha;
}

// Two spellings, one structure. `rows`/`cols` describe a PNG that is nothing
// but a flipbook, which stays the shortest way to say the common thing;
// `sheet`/`cell`/`origin`/`frames` describe a strip inside a shared sheet.
// Both end up as the same window, so the shader and the runtime only ever see
// one form.
void parseFlipbook(const toml::table& fb, eng::ParticleTextureDesc& d)
{
    d.flipbook.fps  = float(fb["fps"].value_or(0.0));
    d.flipbook.loop = fb["loop"].value_or(true);

    const int cols = int(fb["cols"].value_or(0));
    const int rows = int(fb["rows"].value_or(0));
    if (cols > 0 || rows > 0) {
        d.flipbook.sheetCols = std::max(1, cols);
        d.flipbook.sheetRows = std::max(1, rows);
        d.flipbook.frames = d.flipbook.sheetCols * d.flipbook.sheetRows;
        d.flipbook.perRow = d.flipbook.sheetCols;
    }

    // The sheet form overrides whatever the grid form said, so an entry may
    // state both without the result depending on key order.
    d.flipbook.sheetCols = std::max(1, int(fb["sheet_cols"].value_or(
                                        int64_t(d.flipbook.sheetCols))));
    d.flipbook.sheetRows = std::max(1, int(fb["sheet_rows"].value_or(
                                        int64_t(d.flipbook.sheetRows))));
    d.flipbook.originCol = std::max(0, int(fb["origin_col"].value_or(0)));
    d.flipbook.originRow = std::max(0, int(fb["origin_row"].value_or(0)));
    d.flipbook.frames = std::max(1, int(fb["frames"].value_or(
                                     int64_t(d.flipbook.frames))));
    d.flipbook.perRow = std::max(0, int(fb["per_row"].value_or(
                                     int64_t(d.flipbook.perRow))));
}

// Merged, not replaced. A later file that only wants to change one texture's
// blend mode says so and nothing else; replacing the whole entry would silently
// drop the sheet and the flipbook the generated import declared, and the
// texture would fall back to drawing its entire sheet.
void parseInto(const toml::table& textures, Overrides& out)
{
    for (const auto& [key, node] : textures) {
        const toml::table* t = node.as_table();
        if (!t) continue;
        const std::string stem(key.str());
        eng::ParticleTextureDesc& d = out.byStem[stem];
        d.stem = stem;
        d.file = (*t)["sheet"].value_or(d.file);
        if (const auto blend = (*t)["blend"].value<std::string>())
            d.blend = parseBlend(*blend, stem);
        d.softFade = (*t)["soft_fade"].value_or(d.softFade);
        d.nearest = (*t)["nearest"].value_or(d.nearest);
        if (const toml::table* fb = (*t)["flipbook"].as_table())
            parseFlipbook(*fb, d);
    }
}

// Every *.toml directly in the particles root contributes texture entries, so
// a generated import (hundreds of strips carved out of a bought sheet) can land
// in its own file instead of being pasted into the hand-authored one and lost
// the next time the importer runs.
Overrides parseOverrides(const std::string& root)
{
    Overrides out;
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec))
        return out;

    std::vector<std::filesystem::path> files;
    for (auto it = std::filesystem::directory_iterator(root, ec);
         it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file() && it->path().extension() == ".toml")
            files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());

    for (const std::filesystem::path& file : files) {
        toml::parse_result parsed = toml::parse_file(file.string());
        if (!parsed) {
            eng::log::error("ParticleTextures: parse failed: %s",
                            file.string().c_str());
            continue;
        }
        if (const toml::table* textures = parsed.table()["texture"].as_table())
            parseInto(*textures, out);
    }
    return out;
}

} // namespace

namespace eng {

std::string ParticleTextureCatalog::defaultRoot()
{
    return assets::resolve("particles").string();
}

void ParticleTextureCatalog::load(const std::string& root)
{
    mRoot = root;
    mLoaded = true;
    scan();
}

bool ParticleTextureCatalog::reload()
{
    if (!mLoaded) {
        log::warn("ParticleTextures: reload() before load(); ignoring");
        return false;
    }
    scan();
    return true;
}

const ParticleTextureDesc*
ParticleTextureCatalog::find(const std::string& stem) const
{
    auto it = mByStem.find(stem);
    return it == mByStem.end() ? nullptr : &mDescs[it->second];
}

std::string ParticleTextureCatalog::pathFor(const ParticleTextureDesc& desc) const
{
    const std::string leaf = fileFor(desc);
    // The table may spell a path ("sheets/bullet16.png"); index on the leaf.
    const std::string key = std::filesystem::path(leaf).filename().string();
    const auto found = mFilesByLeaf.find(key);
    return found == mFilesByLeaf.end() ? std::string() : found->second;
}

void ParticleTextureCatalog::scan()
{
    mDescs.clear();
    mByStem.clear();
    mFilesByLeaf.clear();

    const std::string textureDir = mRoot + "/textures";
    const Overrides overrides = parseOverrides(mRoot);

    // Index every PNG under textures/, at any depth: shared effect sheets live
    // in a subdirectory but are referenced by bare leaf name.
    {
        std::error_code walkError;
        for (auto it = std::filesystem::recursive_directory_iterator(
                 textureDir, walkError);
             it != std::filesystem::recursive_directory_iterator();
             it.increment(walkError)) {
            if (walkError)
                break;
            if (!it->is_regular_file())
                continue;
            std::string ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            if (ext != ".png")
                continue;
            // First wins, matching the sorted top-level walk below: a texture
            // that owns a file must not be shadowed by a same-named sheet.
            mFilesByLeaf.emplace(it->path().filename().string(),
                                 it->path().string());
        }
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(textureDir, ec)) {
        // Not fatal. A project may legitimately ship no particle textures yet,
        // and effects that name a hand-authored material still work.
        log::warn("ParticleTextures: no texture directory at '%s'",
                  textureDir.c_str());
    } else {
        // Sorted, because directory order is filesystem-dependent and the
        // editor's texture list should not reshuffle between runs.
        std::vector<std::filesystem::path> files;
        for (auto it = std::filesystem::directory_iterator(textureDir, ec);
             it != std::filesystem::directory_iterator(); it.increment(ec)) {
            if (ec) {
                log::error("ParticleTextures: walking '%s': %s",
                           textureDir.c_str(), ec.message().c_str());
                break;
            }
            if (!it->is_regular_file())
                continue;
            std::string ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            if (ext == ".png")
                files.push_back(it->path());
        }
        std::sort(files.begin(), files.end());

        for (const std::filesystem::path& file : files) {
            ParticleTextureDesc desc;
            desc.stem = file.stem().string();
            auto o = overrides.byStem.find(desc.stem);
            if (o != overrides.byStem.end())
                desc = o->second;
            desc.stem = file.stem().string(); // the file on disk owns the name
            desc.file = file.filename().string();

            if (mByStem.count(desc.stem)) {
                log::error("ParticleTextures: duplicate texture stem '%s'; "
                           "ignoring '%s'",
                           desc.stem.c_str(), file.string().c_str());
                continue;
            }
            mByStem[desc.stem] = mDescs.size();
            mDescs.push_back(desc);
        }
    }

    // Entries that carve a strip out of a shared sheet. They have no PNG of
    // their own, so the directory walk above cannot have produced them.
    //
    // An entry that names neither a sheet nor an existing file is almost always
    // a rename or a typo. Report it and carry on: refusing to boot over a stale
    // table entry would make the workflow worse than the one it replaces.
    size_t sheetBacked = 0;
    for (const auto& [stem, desc] : overrides.byStem) {
        if (mByStem.count(stem))
            continue;
        if (desc.file.empty()) {
            log::warn("ParticleTextures: an entry for '%s' names no sheet and "
                      "no %s.png exists",
                      stem.c_str(), stem.c_str());
            continue;
        }
        mByStem[stem] = mDescs.size();
        mDescs.push_back(desc);
        ++sheetBacked;
    }
    // Directory order is filesystem-dependent and the map above is unordered,
    // so sort once here: editor listings and log output must not reshuffle
    // between runs.
    std::sort(mDescs.begin(), mDescs.end(),
              [](const ParticleTextureDesc& a, const ParticleTextureDesc& b) {
                  return a.stem < b.stem;
              });
    mByStem.clear();
    for (size_t i = 0; i < mDescs.size(); ++i)
        mByStem[mDescs[i].stem] = i;

    log::info("ParticleTextures: %zu textures (%zu from shared sheets) from %s",
              mDescs.size(), sheetBacked, mRoot.c_str());
}

} // namespace eng
