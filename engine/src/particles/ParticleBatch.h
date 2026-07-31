#pragma once
#include <eng/particles/ParticleTypes.h>

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace Ogre { class SceneManager; }

namespace eng {

// One particle as the renderer consumes it. The simulation owns an SoA pool;
// this is the AoS view it hands over per (material, render mode) group, and it
// is uploaded to the GPU verbatim, so the field order is part of the vertex
// declaration in ParticleBatch.cpp. Do not reorder without updating it.
struct ParticleInstance {
    glm::vec3 pos;
    float     size;
    glm::vec4 colour;
    float     rot;      // radians; sprites spin on screen, voxels spin about Y
    float     frame;    // flipbook frame index, ignored by the voxel batch
};

// ParticleRenderMode and ParticleBlend come from ParticleTypes.h, which is the
// single vocabulary shared by the TOML schema, the simulation, and this
// renderer. Blending is a property of the material, but the batch needs to know
// it too: it decides whether the instance stream must be depth-sorted. Additive
// output is order-independent, so sorting it is pure cost.

struct ParticleBatchDesc {
    std::string       material   = "Particles/Sprite/Alpha";
    ParticleRenderMode mode      = ParticleRenderMode::Sprite;
    ParticleBlend      blend     = ParticleBlend::Alpha;
    std::size_t        capacity  = 2048;
    // Ogre::RENDER_QUEUE_MAIN. Blended materials still land in the transparent
    // sub-list of this group, which is where particles belong relative to
    // world geometry.
    uint8_t            renderQueue = 50;
};

// A single draw call worth of particles. The caller drives it once per frame:
//
//     batch->beginFrame();
//     batch->push(instances, count);
//     if (batch->needsSorting()) batch->sort(cameraWorldPos);
//     batch->endFrame();
//
// endFrame() is the only point that touches the GPU, so the caller may push
// from several sources before committing.
class IParticleBatch {
public:
    virtual ~IParticleBatch() = default;

    // Growing reallocates the instance buffer, so size it once from the effect
    // quota rather than per frame. Instances beyond capacity are dropped.
    virtual void setCapacity(std::size_t maxInstances) = 0;
    virtual std::size_t capacity() const = 0;

    virtual void beginFrame() = 0;
    virtual void push(const ParticleInstance& instance) = 0;
    virtual void push(const ParticleInstance* instances, std::size_t count) = 0;

    // Back-to-front against the camera. A no-op for additive batches, so it is
    // safe to call unconditionally; needsSorting() lets the caller skip the
    // work entirely.
    virtual void sort(const glm::vec3& cameraWorldPos) = 0;
    virtual bool needsSorting() const = 0;

    // Uploads the staged instances, recomputes the bounding volume and hides
    // the renderable when the batch is empty.
    virtual void endFrame() = 0;

    virtual std::size_t count() const = 0;
    virtual void setVisible(bool visible) = 0;

    // Detaches from the scene. The destructor does this too; destroy() exists
    // so a batch can be torn down before the SceneManager goes away.
    virtual void destroy() = 0;
};

// Creates and attaches the batch to the scene manager's root node. Instance
// positions are therefore world-space: the batch never inherits a transform.
// Returns null if the scene manager is null.
std::unique_ptr<IParticleBatch> createParticleBatch(Ogre::SceneManager* sceneManager,
                                                    const ParticleBatchDesc& desc);

} // namespace eng
