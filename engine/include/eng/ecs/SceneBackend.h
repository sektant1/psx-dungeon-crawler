#pragma once
#include <eng/Handles.h>
#include <eng/LightDesc.h> // eng::LightDesc
#include <eng/ShaderBlock.h>
#include <eng/ecs/components/ShaderParams.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>

namespace eng::ecs {

// Narrow write-target SceneSync drives. eng::Renderer implements this via
// RendererSceneBackend; tests implement a recording mock. Keeping SceneSync
// behind this seam makes it unit-testable without a live renderer.
class SceneBackend {
public:
    virtual ~SceneBackend() = default;
    virtual NodeHandle createNode(NodeHandle parent, glm::vec3 pos,
                                  const std::string& name) = 0;
    virtual void setPosition(NodeHandle, glm::vec3) = 0;
    virtual void setOrientation(NodeHandle, glm::quat) = 0;
    virtual void setScale(NodeHandle, glm::vec3) = 0;
    virtual void destroyNode(NodeHandle) = 0;
    virtual void attachMesh(NodeHandle, MeshHandle, const std::string& material,
                            bool castShadows) = 0;
    virtual LightHandle attachLight(NodeHandle, const LightDesc&) = 0;
    // Retint an existing light (linear, energy pre-multiplied) -- driven per
    // frame for animated lights.
    virtual void setLightColour(LightHandle, glm::vec3) = 0;

    // --- optional attachments --------------------------------------------
    // Defaulted rather than pure: a test double implements what it asserts on,
    // and a backend that cannot do particles is a backend without particles,
    // not a compile error. Every one of these is driven by a component, so the
    // scene decides what an entity has instead of C++ deciding per call site.

    // Draw this node's meshes with `materialName` instead of their own
    // (MaterialOverride).
    virtual void setNodeMaterial(NodeHandle, const std::string&) {}
    // Per-entity shader uniforms (ShaderParams). The backend gives this node's
    // meshes their own copy of whatever material they wear and sets the
    // uniforms on that copy, so one entity can glow without its neighbours
    // changing -- which is what a shared named material otherwise forces.
    //
    // Called only when a value changed, so an implementation may do the
    // expensive part (cloning) on the first call and only set constants after.
    virtual void setNodeShaderParams(NodeHandle, const ShaderParams&) {}
    // Put the shared material back and drop the copy. Removing the component
    // has to release the clone, not merely push neutral values onto it: the
    // two look identical and only one of them stops paying for a material and
    // a broken batch.
    virtual void clearNodeShaderParams(NodeHandle) {}
    // The general form: any block of uniforms, described by its own field
    // table. This is what lets a shader have a component without the backend
    // knowing the shader exists (see eng/ShaderBlock.h).
    virtual void setNodeShaderBlock(NodeHandle, const ShaderBlock&) {}
    // Show or hide the node and everything under it (Visibility). Pushed only
    // when the value changes.
    virtual void setNodeVisible(NodeHandle, bool) {}

    // --- camera ----------------------------------------------------------
    // Look through this node, and with this lens (Camera). Two calls rather
    // than one because they change at different rates: the node changes when
    // the shot does, the lens whenever an author drags a slider, and attaching
    // a camera is the more expensive of the two.
    virtual void setCameraNode(NodeHandle) {}
    virtual void setCameraLens(float fovDegrees, float nearClip, float farClip)
    {
        (void)fovDegrees;
        (void)nearClip;
        (void)farClip;
    }
    // Start `effect` on this node and return its handle; an unknown name or an
    // unsupported backend returns an invalid one (ParticleEmitter).
    virtual ParticlesHandle attachParticles(NodeHandle, const std::string& effect,
                                            glm::vec3 localOffset)
    {
        (void)effect;
        (void)localOffset;
        return {};
    }
    // Stop emitting and release. Live particles are allowed to finish.
    virtual void detachParticles(ParticlesHandle) {}
};

} // namespace eng::ecs
