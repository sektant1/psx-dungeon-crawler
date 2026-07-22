#pragma once
#include <eng/Handles.h>
#include <eng/ParticleEffectDesc.h>

#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Ogre { class SceneManager; class ParticleSystem; class SceneNode; }

namespace eng {

// Owns effect templates + a per-effect pool of Ogre ParticleSystems. Ogre is the
// simulator; this only configures/pools/recycles systems from POD descs.
class Particles {
public:
    void init(Ogre::SceneManager* sm);

    ParticleEffectId registerEffect(const ParticleEffectDesc& desc);
    // Resolve a registered effect by its desc.name (invalid id if unknown).
    ParticleEffectId find(const std::string& name) const;

    ParticlesHandle spawn(ParticleEffectId fx, Ogre::SceneNode* parent,
                          glm::vec3 localPos);
    void stop(ParticlesHandle h);
    void despawn(ParticlesHandle h);

    void setQuality(float q);
    void update(float dt);
    void clear();

private:
    struct Effect {
        ParticleEffectDesc desc;
        std::vector<Ogre::ParticleSystem*> free;
    };
    struct Live {
        uint32_t effect = 0;
        Ogre::ParticleSystem* ps = nullptr;
        float age = 0.0f;
        float maxLife = 0.0f;
        bool  oneShot = false;
        bool  active = false;
    };

    Ogre::ParticleSystem* build(const ParticleEffectDesc& d);
    void applyQuota(Ogre::ParticleSystem* ps, const ParticleEffectDesc& d);

    Ogre::SceneManager* mSm = nullptr;
    std::vector<Effect> mEffects;
    std::unordered_map<std::string, uint32_t> mByName; // name -> ParticleEffectId
    std::unordered_map<uint32_t, Live> mLive;
    uint32_t mNextHandle = 1;
    uint32_t mNextName = 1;
    float mQuality = 1.0f;
};

} // namespace eng
