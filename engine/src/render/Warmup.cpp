#include <eng/render/Warmup.h>

#include <eng/Log.h>

#include <OgreMaterial.h>
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
                    if (!material->getBestTechnique()) {
                        ++cursor->failures;
                        log::warn("Warmup: material '%s' has no supportable "
                                  "technique",
                                  material->getName().c_str());
                    }
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
