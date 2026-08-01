#pragma once
#include <eng/ecs/SceneBackend.h>

namespace eng {
class Renderer;
}

namespace eng::ecs {

// SceneBackend implemented over eng::Renderer. Pure pass-through: every call
// forwards to the matching Renderer method. This is the seam that makes the
// renderer a view of the registry.
class RendererSceneBackend : public SceneBackend {
public:
    explicit RendererSceneBackend(Renderer& r) : mR(r) {}

    NodeHandle createNode(NodeHandle parent, glm::vec3 pos,
                          const std::string& name) override;
    void setPosition(NodeHandle, glm::vec3) override;
    void setOrientation(NodeHandle, glm::quat) override;
    void setScale(NodeHandle, glm::vec3) override;
    void destroyNode(NodeHandle) override;
    void attachMesh(NodeHandle, MeshHandle, const std::string& material,
                    bool castShadows) override;
    LightHandle attachLight(NodeHandle, const LightDesc&) override;
    void setLightColour(LightHandle, glm::vec3) override;
    void setNodeMaterial(NodeHandle, const std::string&) override;
    void setNodeShaderParams(NodeHandle, const ShaderParams&) override;
    void clearNodeShaderParams(NodeHandle) override;
    void setNodeShaderBlock(NodeHandle, const ShaderBlock&) override;
    void setNodeVisible(NodeHandle, bool) override;
    void setCameraNode(NodeHandle) override;
    void setCameraLens(float fovDegrees, float nearClip, float farClip) override;
    ParticlesHandle attachParticles(NodeHandle, const std::string& effect,
                                    glm::vec3 localOffset) override;
    void detachParticles(ParticlesHandle) override;

private:
    Renderer& mR;
};

} // namespace eng::ecs
