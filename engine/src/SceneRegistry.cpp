#include "SceneRegistry.h"

#include <algorithm>

namespace eng {

void SceneRegistry::addNode(NodeHandle node, NodeHandle parent, std::string name)
{
    mNodes[node.id] = NodeRecord{node, parent, std::move(name), {}, {}};
    if (parent.valid()) {
        auto it = mNodes.find(parent.id);
        if (it != mNodes.end()) {
            it->second.children.push_back(node);
            return;
        }
    }
    mRoots.push_back(node);
}

void SceneRegistry::addAttachment(NodeHandle node, AttachRecord rec)
{
    auto it = mNodes.find(node.id);
    if (it != mNodes.end())
        it->second.attachments.push_back(std::move(rec));
}

void SceneRegistry::setMeshMaterial(NodeHandle node, const std::string& material)
{
    auto it = mNodes.find(node.id);
    if (it == mNodes.end())
        return;
    for (AttachRecord& a : it->second.attachments)
        if (a.kind == NodeAttachKind::Mesh) {
            a.label = material;
            return;
        }
}

void SceneRegistry::removeNode(NodeHandle node)
{
    auto it = mNodes.find(node.id);
    if (it == mNodes.end())
        return;

    NodeHandle parent = it->second.parent;
    // Copy children before erasing: recursion mutates the map.
    std::vector<NodeHandle> children = it->second.children;
    for (NodeHandle child : children)
        removeNode(child);

    if (parent.valid()) {
        auto pit = mNodes.find(parent.id);
        if (pit != mNodes.end()) {
            auto& kids = pit->second.children;
            kids.erase(std::remove_if(kids.begin(), kids.end(),
                       [&](NodeHandle h){ return h.id == node.id; }),
                       kids.end());
        }
    }
    mRoots.erase(std::remove_if(mRoots.begin(), mRoots.end(),
                 [&](NodeHandle h){ return h.id == node.id; }),
                 mRoots.end());

    mNodes.erase(node.id);
}

void SceneRegistry::clear()
{
    mNodes.clear();
    mRoots.clear();
}

const NodeRecord* SceneRegistry::find(NodeHandle node) const
{
    auto it = mNodes.find(node.id);
    return it == mNodes.end() ? nullptr : &it->second;
}

std::string SceneRegistry::autoName(NodeHandle node) const
{
    return "Node " + std::to_string(node.id);
}

} // namespace eng
