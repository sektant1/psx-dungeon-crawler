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

} // namespace eng::ecs
