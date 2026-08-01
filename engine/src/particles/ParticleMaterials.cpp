#include "ParticleMaterials.h"

#include <eng/Log.h>
#include <eng/assets/AssetRoot.h>

#include <OgreMaterial.h>
#include <OgreMaterialManager.h>
#include <OgrePass.h>
#include <OgreResourceGroupManager.h>
#include <OgreTechnique.h>
#include <OgreTextureUnitState.h>

#define TOML_EXCEPTIONS 0
#include <tomlplusplus/toml.hpp>

#include <algorithm>
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
    eng::log::warn("ParticleMaterials: texture '%s' has unknown blend '%s'; "
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
            eng::log::error("ParticleMaterials: parse failed: %s",
                            file.string().c_str());
            continue;
        }
        if (const toml::table* textures = parsed.table()["texture"].as_table())
            parseInto(*textures, out);
    }
    return out;
}

// The scripted material a generated one is a copy of. Blend is the only axis
// that changes, and it changes which *base* is copied rather than a flag on the
// copy, because the two differ in sort order as well as in blend mode: the
// additive batch skips back-to-front sorting entirely.
const char* baseMaterialFor(eng::ParticleBlend blend)
{
    return blend == eng::ParticleBlend::Additive ? "Engine/Particles/SpriteAdditive"
                                                 : "Engine/Particles/SpriteAlpha";
}

// One material per texture, copied from the scripted sprite material in
// assets/engine/materials/particles.material with only the texture swapped.
//
// It is a COPY of a program-backed material, not a pass built from scratch.
// A hand-built pass has no vertex or fragment program, and the instanced
// particle batch draws through Particles/SpriteVS -- which reads the per-
// instance position/size/colour stream the batch binds. Without it GL3Plus
// throws at the first draw ("RenderSystem does not support FixedFunction ...
// has no Vertex Shader"), and it throws at *draw*, not at load, so the effect
// has to actually be spawned before anything notices.
//
// Everything else about a particle pass -- unlit, no depth write, no culling --
// is already stated in the scripted material, so it is stated once there rather
// than twice here.
void buildMaterial(const eng::ParticleTextureDesc& desc,
                   const std::string& textureFile)
{
    using eng::FlipbookDesc;

    auto& mm = Ogre::MaterialManager::getSingleton();
    const std::string name = eng::particleAutoMaterialName(desc.stem);
    const char* baseName = baseMaterialFor(desc.blend);

    Ogre::MaterialPtr base = mm.getByName(baseName, Ogre::RGN_DEFAULT);
    if (!base) {
        eng::log::error("ParticleMaterials: base material '%s' is missing; "
                        "texture '%s' cannot be given a material",
                        baseName, desc.stem.c_str());
        return;
    }

    Ogre::MaterialPtr mat = mm.getByName(name, Ogre::RGN_DEFAULT);
    if (!mat)
        mat = mm.create(name, Ogre::RGN_DEFAULT);
    // copyDetailsTo, not clone: it replaces this material's techniques while
    // preserving its name and handle, so a batch already holding this
    // MaterialPtr keeps a valid pointer and simply draws with the new settings
    // on the next frame. That is what makes texture hot-reload safe.
    base->copyDetailsTo(mat);

    Ogre::Technique* technique = mat->getTechnique(0);
    Ogre::Pass* pass = technique ? technique->getPass(0) : nullptr;
    if (!pass || pass->getNumTextureUnitStates() == 0) {
        eng::log::error("ParticleMaterials: base material '%s' has no texture "
                        "unit to bind '%s' to", baseName, desc.stem.c_str());
        return;
    }

    Ogre::TextureUnitState* unit = pass->getTextureUnitState(0);
    unit->setTextureName(textureFile);
    // Point filtering with no mip chain is the PSX default. Mipmaps would blur
    // a 32x32 spark into mush the moment it moves away from the camera, and a
    // flipbook atlas would bleed between its frames at the lower levels.
    unit->setTextureFiltering(desc.nearest ? Ogre::TFO_NONE : Ogre::TFO_BILINEAR);
    unit->setTextureAddressingMode(Ogre::TextureUnitState::TAM_CLAMP);

    // The flipbook window. Without this the vertex program kept its default of
    // one full-frame cell, so every flipbook texture ever declared drew its
    // entire sheet -- all sixteen frames at once -- on every particle. The
    // metadata existed and was parsed; nothing had ever reached the shader.
    if (pass->hasVertexProgram()) {
        const FlipbookDesc& fb = desc.flipbook;
        const Ogre::GpuProgramParametersSharedPtr params =
            pass->getVertexProgramParameters();
        // A material whose vertex program has no such constant is legal (a
        // hand-authored one may ignore flipbooks entirely), so this must not
        // throw on a missing name.
        params->setIgnoreMissingParams(true);
        params->setNamedConstant("flipbookCell",
                                 Ogre::Vector2(fb.cellU(), fb.cellV()));
        params->setNamedConstant("flipbookOrigin",
                                 Ogre::Vector2(fb.originU(), fb.originV()));
        params->setNamedConstant("flipbookPerRow",
                                 Ogre::Real(fb.framesPerRow()));
    }

    mat->load();
}

} // namespace

namespace eng {

std::string ParticleMaterials::defaultRoot()
{
    // Pack-qualified: the particle tables are engine content, and a game or
    // editor pack that happened to carry a `particles/` folder must not be able
    // to take the whole directory over by outranking the engine in the mount
    // order.
    return assets::resolve("particles").string();
}

void ParticleMaterials::load(const std::string& root)
{
    mRoot = root;
    mLoaded = true;
    scan();
}

bool ParticleMaterials::reload()
{
    if (!mLoaded) {
        log::warn("ParticleMaterials: reload() before load(); ignoring");
        return false;
    }
    scan();
    return true;
}

void ParticleMaterials::scan()
{
    mDescs.clear();
    mByStem.clear();

    const std::string textureDir = mRoot + "/textures";
    const Overrides overrides = parseOverrides(mRoot);

    std::error_code ec;
    if (!std::filesystem::is_directory(textureDir, ec)) {
        // Not fatal. A project may legitimately ship no particle textures yet,
        // and effects that name a hand-authored material still work.
        log::warn("ParticleMaterials: no texture directory at '%s'",
                  textureDir.c_str());
    } else {
        // Sorted, because directory order is filesystem-dependent and the
        // editor's texture list should not reshuffle between runs.
        std::vector<std::filesystem::path> files;
        for (auto it = std::filesystem::directory_iterator(textureDir, ec);
             it != std::filesystem::directory_iterator(); it.increment(ec)) {
            if (ec) {
                log::error("ParticleMaterials: walking '%s': %s",
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
                log::error("ParticleMaterials: duplicate texture stem '%s'; "
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
            log::warn("ParticleMaterials: an entry for '%s' names no sheet and "
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

    // Rebuild only what a previous scan had already materialised. Everything
    // else waits for its first use, which is what keeps a several-hundred-entry
    // effect pack off the boot path.
    const std::unordered_set<std::string> wasBuilt = std::move(mBuilt);
    mBuilt.clear();
    for (const std::string& stem : wasBuilt)
        if (const ParticleTextureDesc* desc = find(stem))
            build(*desc);

    log::info("ParticleMaterials: %zu textures (%zu from shared sheets) from %s",
              mDescs.size(), sheetBacked, mRoot.c_str());
}

void ParticleMaterials::build(const ParticleTextureDesc& desc)
{
    const std::string& file =
        desc.file.empty() ? desc.stem + ".png" : desc.file;
    buildMaterial(desc, file);
    mBuilt.insert(desc.stem);
}

const ParticleTextureDesc* ParticleMaterials::find(const std::string& stem) const
{
    auto it = mByStem.find(stem);
    return it == mByStem.end() ? nullptr : &mDescs[it->second];
}

std::string ParticleMaterials::materialFor(const std::string& stem)
{
    const ParticleTextureDesc* desc = find(stem);
    if (!desc)
        return {};
    if (!mBuilt.count(stem))
        build(*desc);
    return particleAutoMaterialName(stem);
}

} // namespace eng
