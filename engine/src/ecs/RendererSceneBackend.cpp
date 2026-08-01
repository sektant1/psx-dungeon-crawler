#include "RendererSceneBackend.h"

#include <eng/Renderer.h>

namespace eng::ecs {

NodeHandle RendererSceneBackend::createNode(NodeHandle parent, glm::vec3 pos,
                                            const std::string& name)
{
    const NodeHandle p = parent.valid() ? parent : kRootNode;
    return mR.createNode(p, pos, name);
}
void RendererSceneBackend::setPosition(NodeHandle n, glm::vec3 p) { mR.setPosition(n, p); }
void RendererSceneBackend::setOrientation(NodeHandle n, glm::quat q) { mR.setOrientation(n, q); }
void RendererSceneBackend::setScale(NodeHandle n, glm::vec3 s) { mR.setScale(n, s); }
void RendererSceneBackend::destroyNode(NodeHandle n) { mR.destroyNode(n); }
void RendererSceneBackend::attachMesh(NodeHandle n, MeshHandle m,
                                      const std::string& material, bool cast)
{
    mR.attachMesh(n, m, material, cast);
}
LightHandle RendererSceneBackend::attachLight(NodeHandle n, const LightDesc& d)
{
    return mR.attachLight(n, d);
}
void RendererSceneBackend::setLightColour(LightHandle l, glm::vec3 c)
{
    mR.setLightColour(l, c);
}
void RendererSceneBackend::setNodeMaterial(NodeHandle n, const std::string& m)
{
    mR.setNodeMaterial(n, m);
}
void RendererSceneBackend::setNodeShaderParams(NodeHandle n,
                                               const ShaderParams& p)
{
    mR.setNodeShaderParams(n, p);
}
void RendererSceneBackend::clearNodeShaderParams(NodeHandle n)
{
    mR.clearNodeShaderParams(n);
}
void RendererSceneBackend::setNodeShaderBlock(NodeHandle n,
                                              const ShaderBlock& block)
{
    mR.setNodeShaderBlock(n, block);
}
void RendererSceneBackend::setNodeVisible(NodeHandle n, bool show)
{
    mR.setNodeVisible(n, show);
}
void RendererSceneBackend::setCameraNode(NodeHandle n)
{
    mR.attachCamera(n);
}
void RendererSceneBackend::setCameraLens(float fovDegrees, float nearClip,
                                         float farClip)
{
    mR.setCameraFov(fovDegrees);
    mR.setCameraClip(nearClip, farClip);
}
ParticlesHandle RendererSceneBackend::attachParticles(NodeHandle n,
                                                      const std::string& effect,
                                                      glm::vec3 localOffset)
{
    // By name, not by id: the component stores what the author typed, and an
    // effect that is not in the library yet must not be an error here -- the
    // renderer already returns an invalid handle for an unknown name, which
    // SceneSync treats as "not attached yet" and retries next frame.
    return mR.spawnParticles(effect, n, localOffset);
}
void RendererSceneBackend::detachParticles(ParticlesHandle h)
{
    // stop, not despawn: particles already in flight finish their lives instead
    // of vanishing mid-air when the emitter is removed.
    mR.stopParticles(h);
}

} // namespace eng::ecs
