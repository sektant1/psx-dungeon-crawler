#include "ParticleMaterials.h"

#include <eng/Log.h>

#include <OgreMaterial.h>
#include <OgreMaterialManager.h>
#include <OgrePass.h>
#include <OgreResourceGroupManager.h>
#include <OgreTechnique.h>
#include <OgreTextureUnitState.h>

#include <unordered_set>

namespace {

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

void ParticleMaterials::load(const std::string& root)
{
    mCatalog.load(root);
    rebuildBuilt();
}

bool ParticleMaterials::reload()
{
    if (!mCatalog.reload())
        return false;
    rebuildBuilt();
    return true;
}

// Rebuild only what a previous scan had already materialised. Everything else
// waits for its first use, which is what keeps a several-hundred-entry effect
// pack off the boot path.
void ParticleMaterials::rebuildBuilt()
{
    const std::unordered_set<std::string> wasBuilt = std::move(mBuilt);
    mBuilt.clear();
    for (const std::string& stem : wasBuilt)
        if (const ParticleTextureDesc* desc = mCatalog.find(stem))
            build(*desc);
}

void ParticleMaterials::build(const ParticleTextureDesc& desc)
{
    buildMaterial(desc, ParticleTextureCatalog::fileFor(desc));
    mBuilt.insert(desc.stem);
}

const ParticleTextureDesc* ParticleMaterials::find(const std::string& stem) const
{
    return mCatalog.find(stem);
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
