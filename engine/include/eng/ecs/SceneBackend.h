#pragma once
#include <eng/Handles.h>
#include <eng/Renderer.h> // eng::LightDesc

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
};

} // namespace eng::ecs
