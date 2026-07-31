#include "ParticleMaterials.h"

#include <eng/Log.h>

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

Overrides parseOverrides(const std::string& path)
{
    Overrides out;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return out;

    toml::parse_result parsed = toml::parse_file(path);
    if (!parsed) {
        eng::log::error("ParticleMaterials: parse failed: %s", path.c_str());
        return out;
    }
    const toml::table* textures = parsed.table()["texture"].as_table();
    if (!textures)
        return out;

    for (const auto& [key, node] : *textures) {
        const toml::table* t = node.as_table();
        if (!t) continue;
        eng::ParticleTextureDesc d;
        d.stem = std::string(key.str());
        d.blend = parseBlend((*t)["blend"].value_or(std::string("alpha")), d.stem);
        d.softFade = (*t)["soft_fade"].value_or(false);
        d.nearest = (*t)["nearest"].value_or(true);
        if (const toml::table* fb = (*t)["flipbook"].as_table()) {
            d.flipbook.rows = int((*fb)["rows"].value_or(1));
            d.flipbook.cols = int((*fb)["cols"].value_or(1));
            d.flipbook.fps  = float((*fb)["fps"].value_or(0.0));
            d.flipbook.loop = (*fb)["loop"].value_or(true);
            if (d.flipbook.rows < 1 || d.flipbook.cols < 1) {
                eng::log::warn("ParticleMaterials: texture '%s' has a flipbook "
                               "with rows/cols < 1; ignoring the flipbook",
                               d.stem.c_str());
                d.flipbook = eng::FlipbookDesc{};
            }
        }
        out.byStem[d.stem] = d;
    }
    return out;
}

// One material per texture, created rather than scripted. Every parameter here
// is a property of *all* particles, not of one effect: particles are unlit
// because they carry their own colour ramp, they never write depth because they
// are transparent and would occlude each other, and culling is off because a
// billboard or a decal quad may legitimately face away from the camera.
void buildMaterial(const eng::ParticleTextureDesc& desc,
                   const std::string& textureFile)
{
    auto& mm = Ogre::MaterialManager::getSingleton();
    const std::string name = eng::particleAutoMaterialName(desc.stem);

    Ogre::MaterialPtr mat = mm.getByName(name, Ogre::RGN_DEFAULT);
    if (!mat)
        mat = mm.create(name, Ogre::RGN_DEFAULT);
    // Rebuilding the techniques in place, rather than removing and recreating
    // the material, is what makes hot-reload safe: a batch that already holds
    // this MaterialPtr keeps a valid pointer and simply draws with the new
    // settings on the next frame.
    mat->removeAllTechniques();

    Ogre::Pass* pass = mat->createTechnique()->createPass();
    pass->setLightingEnabled(false);
    pass->setDepthWriteEnabled(false);
    pass->setDepthCheckEnabled(true);
    pass->setCullingMode(Ogre::CULL_NONE);
    pass->setManualCullingMode(Ogre::MANUAL_CULL_NONE);
    pass->setSceneBlending(desc.blend == eng::ParticleBlend::Additive
                               ? Ogre::SBT_ADD
                               : Ogre::SBT_TRANSPARENT_ALPHA);

    Ogre::TextureUnitState* unit = pass->createTextureUnitState(textureFile);
    // Point filtering with no mip chain is the PSX default. Mipmaps would blur
    // a 32x32 spark into mush the moment it moves away from the camera, and a
    // flipbook atlas would bleed between its frames at the lower levels.
    unit->setTextureFiltering(desc.nearest ? Ogre::TFO_NONE : Ogre::TFO_BILINEAR);
    unit->setTextureAddressingMode(Ogre::TextureUnitState::TAM_CLAMP);

    mat->load();
}

} // namespace

namespace eng {

std::string ParticleMaterials::defaultRoot()
{
    return std::string(ENG_ASSET_DIR) + "/particles";
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
    const Overrides overrides = parseOverrides(mRoot + "/textures.toml");

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

            if (mByStem.count(desc.stem)) {
                log::error("ParticleMaterials: duplicate texture stem '%s'; "
                           "ignoring '%s'",
                           desc.stem.c_str(), file.string().c_str());
                continue;
            }
            buildMaterial(desc, file.filename().string());
            mByStem[desc.stem] = mDescs.size();
            mDescs.push_back(desc);
        }
    }

    // An override for a file nobody dropped is almost always a rename or a
    // typo. Report it and carry on: refusing to boot over a stale table entry
    // would make the workflow worse than the one it replaces.
    for (const auto& [stem, desc] : overrides.byStem)
        if (!mByStem.count(stem))
            log::warn("ParticleMaterials: textures.toml has an entry for '%s' "
                      "but no %s.png exists",
                      stem.c_str(), stem.c_str());

    log::info("ParticleMaterials: %zu textures from %s", mDescs.size(),
              mRoot.c_str());
}

const ParticleTextureDesc* ParticleMaterials::find(const std::string& stem) const
{
    auto it = mByStem.find(stem);
    return it == mByStem.end() ? nullptr : &mDescs[it->second];
}

std::string ParticleMaterials::materialFor(const std::string& stem) const
{
    return find(stem) ? particleAutoMaterialName(stem) : std::string();
}

} // namespace eng
