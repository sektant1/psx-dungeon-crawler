#pragma once
#include <eng/Handles.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

namespace eng {

class Renderer;
struct LightDesc;

// Attachment kind, shared with the internal SceneRegistry (which includes this
// header and reuses this enum). Public so editor tooling can switch on it.
enum class NodeAttachKind { Mesh, Light, Sprite, Particles };

struct AttachmentInfo { NodeAttachKind kind; uint64_t handle; std::string label; };

struct NodeInfo {
    NodeHandle handle; NodeHandle parent; std::string name;
    glm::vec3 position{0.0f}; glm::quat orientation{1,0,0,0}; glm::vec3 scale{1.0f};
    bool visible = true;
    std::vector<AttachmentInfo> attachments;
};

// Read-only view over the live scene graph for editor tooling. Transforms are
// read live from the Renderer; edits go back through Renderer's setters.
class SceneView {
public:
    std::vector<NodeHandle> roots() const;
    std::vector<NodeHandle> childrenOf(NodeHandle) const;
    bool info(NodeHandle, NodeInfo& out) const;
    bool lightInfo(LightHandle, LightDesc& out) const;
private:
    friend class Renderer;
    explicit SceneView(const Renderer& r) : mRenderer(&r) {}
    const Renderer* mRenderer;
};

} // namespace eng
