#pragma once
#include <eng/Handles.h>
#include <eng/SceneView.h> // public eng::NodeAttachKind, reused by AttachRecord
#include <string>
#include <vector>
#include <unordered_map>

namespace eng {

struct AttachRecord {
    NodeAttachKind kind;
    uint64_t handle = 0;   // LightHandle/SpriteHandle/ParticlesHandle id, or 0 for mesh
    std::string label;     // material name / effect name
};

struct NodeRecord {
    NodeHandle handle{};
    NodeHandle parent{};
    std::string name;
    std::vector<AttachRecord> attachments;
    std::vector<NodeHandle> children;
};

// Editor-facing mirror of the scene graph, kept in sync by Renderer. Holds no
// Ogre types. Keyed by handle id.
class SceneRegistry {
public:
    void addNode(NodeHandle node, NodeHandle parent, std::string name);
    void addAttachment(NodeHandle node, AttachRecord rec);
    void setMeshMaterial(NodeHandle node, const std::string& material); // first mesh
    void removeNode(NodeHandle node);   // unlinks from parent + recurses into children
    void clear();
    const NodeRecord* find(NodeHandle) const;
    const std::vector<NodeHandle>& roots() const { return mRoots; }
    std::string autoName(NodeHandle node) const; // "Node <id>"
private:
    std::unordered_map<uint64_t, NodeRecord> mNodes;
    std::vector<NodeHandle> mRoots;
};

} // namespace eng
