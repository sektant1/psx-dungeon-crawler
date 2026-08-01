#include <eng/render/Warmup.h>

#include <eng/Log.h>

#include <OgreMaterial.h>
#include <OgreRenderSystem.h>
#include <OgreRenderSystemCapabilities.h>
#include <OgreRoot.h>
#include <OgreMaterialManager.h>
#include <OgreMesh.h>
#include <OgreMeshManager.h>
#include <OgrePass.h>
#include <OgreTechnique.h>
#include <OgreTexture.h>
#include <OgreTextureManager.h>
#include <OgreTextureUnitState.h>

#include <memory>
#include <string>
#include <vector>

namespace eng {
namespace {

// A resumable walk over one resource manager. State lives in a shared_ptr so
// the step's lambda stays copyable (std::function requires that) while the
// cursor survives between slices.
struct Cursor {
    std::vector<Ogre::ResourcePtr> items;
    size_t next = 0;
    bool collected = false;
    int failures = 0;
};

std::vector<Ogre::ResourcePtr> snapshot(Ogre::ResourceManager& manager)
{
    std::vector<Ogre::ResourcePtr> out;
    auto it = manager.getResourceIterator();
    while (it.hasMoreElements())
        out.push_back(it.getNext());
    return out;
}

// The blind spot getBestTechnique() leaves, and the reason it is worth its own
// pass here: Technique::checkHardwareSupport only rejects a shaderless pass if
// Pass::isProgrammable() is true, and that is false when the pass has *no*
// programs at all. So a technique with a fully fixed-function pass is reported
// as supported, and the failure surfaces instead as an exception out of
// SceneManager::_setPass -- at the first DRAW, not at load. An effect that is
// never spawned during a capture takes the crash to a player rather than to CI.
//
// This matters for materials built in C++ rather than scripted (the generated
// Particles/Auto/<stem> ones), where nothing parses a *_program_ref to get the
// binding wrong in the first place.
void unshadedPassCheck(const Ogre::Material& material,
                       const Ogre::Technique& technique, int& failures)
{
    // Ogre ships its own shaderless built-ins ("BaseWhite", "DefaultSettings",
    // the shadow caster/receiver pair). Nothing here ever draws them, and they
    // are not ours to fix, so they would be five permanent errors teaching
    // everyone to ignore this message. Same prefixes Renderer::materialNames
    // filters on, for the same reason.
    const std::string& name = material.getName();
    if (name.rfind("Ogre/", 0) == 0 || name.rfind("BaseWhite", 0) == 0 ||
        name == "DefaultSettings")
        return;

    Ogre::RenderSystem* rs = Ogre::Root::getSingleton().getRenderSystem();
    const Ogre::RenderSystemCapabilities* caps =
        rs ? rs->getCapabilities() : nullptr;
    // With fixed function available a shaderless pass is legal, so there is
    // nothing to report.
    if (!caps || caps->hasCapability(Ogre::RSC_FIXED_FUNCTION))
        return;

    const auto& passes = technique.getPasses();
    for (size_t i = 0; i < passes.size(); ++i) {
        const Ogre::Pass* pass = passes[i];
        if (!pass || (pass->hasVertexProgram() && pass->hasFragmentProgram()))
            continue;
        ++failures;
        log::error("Warmup: material '%s' pass %zu has no %s program; this "
                   "render system has no fixed function and will throw the "
                   "first time it is drawn",
                   material.getName().c_str(), i,
                   pass->hasVertexProgram() ? "fragment" : "vertex");
    }
}

} // namespace

void addRenderWarmup(LoadPlan& plan, const WarmupOptions& options)
{
    const int chunk = options.chunk > 0 ? options.chunk : 1;

    if (options.materials) {
        auto cursor = std::make_shared<Cursor>();
        plan.addResumable(
            "Compiling materials",
            [cursor, chunk]() {
                if (!cursor->collected) {
                    cursor->items =
                        snapshot(Ogre::MaterialManager::getSingleton());
                    cursor->collected = true;
                }
                for (int n = 0; n < chunk && cursor->next < cursor->items.size();
                     ++n, ++cursor->next) {
                    auto material = Ogre::static_pointer_cast<Ogre::Material>(
                        cursor->items[cursor->next]);
                    if (!material)
                        continue;
                    // load() parses and compiles; getBestTechnique() is what
                    // actually reports "no supportable Techniques", which is
                    // how a broken shader surfaces here instead of on screen.
                    material->load();
                    Ogre::Technique* best = material->getBestTechnique();
                    if (!best) {
                        ++cursor->failures;
                        log::warn("Warmup: material '%s' has no supportable "
                                  "technique",
                                  material->getName().c_str());
                        continue;
                    }
                    unshadedPassCheck(*material, *best, cursor->failures);
                }
                const bool done = cursor->next >= cursor->items.size();
                if (done)
                    log::info("Warmup: %zu materials, %d unsupported",
                              cursor->items.size(), cursor->failures);
                return done;
            },
            6.0f);
    }

    if (options.textures) {
        auto cursor = std::make_shared<Cursor>();
        plan.addResumable(
            "Uploading textures",
            [cursor, chunk]() {
                if (!cursor->collected) {
                    cursor->items =
                        snapshot(Ogre::TextureManager::getSingleton());
                    cursor->collected = true;
                }
                for (int n = 0; n < chunk && cursor->next < cursor->items.size();
                     ++n, ++cursor->next) {
                    Ogre::ResourcePtr& res = cursor->items[cursor->next];
                    // Render targets (the editor viewport, thumbnails) are
                    // manual textures with no file behind them; load() on one
                    // is a no-op at best and a warning at worst.
                    if (!res || res->isManuallyLoaded())
                        continue;
                    res->load();
                }
                const bool done = cursor->next >= cursor->items.size();
                if (done)
                    log::info("Warmup: %zu textures resident",
                              cursor->items.size());
                return done;
            },
            4.0f);
    }

    if (options.meshes) {
        auto cursor = std::make_shared<Cursor>();
        plan.addResumable(
            "Uploading meshes",
            [cursor, chunk]() {
                if (!cursor->collected) {
                    cursor->items = snapshot(Ogre::MeshManager::getSingleton());
                    cursor->collected = true;
                }
                for (int n = 0; n < chunk && cursor->next < cursor->items.size();
                     ++n, ++cursor->next) {
                    Ogre::ResourcePtr& res = cursor->items[cursor->next];
                    if (res && !res->isManuallyLoaded())
                        res->load();
                }
                return cursor->next >= cursor->items.size();
            },
            2.0f);
    }
}

} // namespace eng
