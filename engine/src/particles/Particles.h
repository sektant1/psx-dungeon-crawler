#pragma once
#include <eng/Handles.h>
#include <eng/particles/DecalSystem.h>
#include <eng/particles/ParticleEffectDesc.h>

#include "ParticleBatch.h"
#include "ParticleMaterials.h"
#include "ParticleSim.h"

#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Ogre { class SceneManager; class SceneNode; }

namespace eng {

// Owns the particle runtime and joins its four independent halves: ParticleSim
// simulates, ParticleBatch draws, ParticleMaterials turns a texture stem into a
// material, and DecalSystem consumes the marks colliding particles leave.
//
// Nothing above this class knows any of that exists. Renderer's spawn/stop/
// despawn facade and its handle semantics are identical to the Ogre-backed
// implementation this replaces, which is why no gameplay call site moved.
class Particles {
public:
    void init(Ogre::SceneManager* sm);

    // Optional. Without a collider, effects that ask for collision simply never
    // hit anything -- the engine has no business reaching into Jolt from here,
    // so the application supplies the adapter.
    void setCollider(IParticleCollider* collider) { mSim.setCollider(collider); }
    void setRayBudget(uint32_t rays) { mSim.setRayBudget(rays); }

    ParticleEffectId registerEffect(const ParticleEffectDesc& desc);
    // Resolve a registered effect by its desc.name (invalid id if unknown).
    ParticleEffectId find(const std::string& name) const;

    ParticlesHandle spawn(ParticleEffectId fx, Ogre::SceneNode* parent,
                          glm::vec3 localPos, bool ownsParent = false);
    ParticlesHandle spawn(ParticleEffectId fx, Ogre::SceneNode* parent,
                          glm::vec3 localPos,
                          const ParticleSpawnOptions& options,
                          bool ownsParent = false);
    void stop(ParticlesHandle h);
    void despawn(ParticlesHandle h);

    void setQuality(float q);
    std::vector<uint32_t> update(float dt);
    void clear();
    // Release everything that points into Ogre, while Ogre is still alive.
    // Engine tears the render core down explicitly before this object is
    // destroyed, so relying on the destructor would touch a dead SceneManager.
    void shutdown();

    // Live counters for the debug panel. No gameplay reads these.
    uint32_t liveParticles() const { return mSim.liveCount(); }
    uint32_t raysLastFrame() const { return mSim.raysLastFrame(); }
    size_t batchCount() const { return mBatches.size(); }
    ParticleMaterials& materials() { return mMaterials; }
    DecalSystem& decals() { return mDecals; }

private:
    // One effect as the runtime sees it: the authored description, the slot it
    // registered into in the sim, and the batch it draws through.
    struct Effect {
        ParticleEffectDesc desc;
        uint16_t simEffect = 0;
        size_t batch = 0;
        FlipbookDesc flipbook;
    };

    struct Live {
        uint32_t effect = 0;    // ParticleEffectId value, 1-based
        uint32_t instance = ParticleSim::kInvalidInstance;
        Ogre::SceneNode* parent = nullptr;
        bool ownsParent = false;
    };

    struct Batch {
        std::string material;
        ParticleRenderMode mode = ParticleRenderMode::Sprite;
        ParticleBlend blend = ParticleBlend::Alpha;
        std::unique_ptr<IParticleBatch> batch;
    };

    size_t batchFor(const std::string& material, ParticleRenderMode mode,
                    ParticleBlend blend, size_t capacity);
    // Re-derive every batch's capacity from the effects mapped to it. Called
    // after any registration, since effects sharing a material share one
    // instance stream and must all fit in it at once.
    void resizeBatches();
    std::string resolveMaterial(const ParticleEffectDesc& desc,
                                FlipbookDesc& flipbookOut,
                                ParticleBlend& blendOut) const;
    void pushTransforms();
    void fillBatches();
    glm::vec3 cameraPosition() const;

    Ogre::SceneManager* mSm = nullptr;
    ParticleSim mSim;
    ParticleMaterials mMaterials;
    DecalSystem mDecals;

    std::vector<Batch> mBatches;
    std::vector<Effect> mEffects;
    std::unordered_map<std::string, uint32_t> mByName; // name -> ParticleEffectId
    std::unordered_map<uint32_t, Live> mLive;
    std::vector<DecalRequest> mDecalRequests;
    std::vector<ParticleInstance> mStaging;
    uint32_t mNextHandle = 1;
    float mQuality = 1.0f;
};

} // namespace eng
