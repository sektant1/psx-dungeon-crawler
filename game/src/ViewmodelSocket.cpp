#include "ViewmodelSocket.h"

#include <eng/Log.h>
#include <eng/Renderer.h>
#include <eng/animation/SkeletalAnimation.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace game {

namespace {

bool finite(const glm::vec3& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

glm::mat4 socketLocalMatrix(const glm::mat4& jointModel,
                            const ViewmodelSocketDef& socket)
{
    const glm::mat4 offset =
        glm::translate(glm::mat4(1.0f), socket.offset) *
        glm::mat4_cast(glm::quat(glm::radians(socket.rotationDegrees))) *
        glm::scale(glm::mat4(1.0f), glm::vec3(socket.scale));
    return jointModel * offset;
}

SocketTransform socketTransform(const glm::mat4& jointModel,
                                const ViewmodelSocketDef& socket)
{
    const glm::mat4 m = socketLocalMatrix(jointModel, socket);

    SocketTransform out;
    out.position = glm::vec3(m[3]);
    // Column lengths are the scale; dividing them out leaves a pure rotation.
    // A skeletal pose carries no shear, so this is exact rather than a
    // best-effort polar decomposition.
    glm::vec3 axisScale(glm::length(glm::vec3(m[0])),
                        glm::length(glm::vec3(m[1])),
                        glm::length(glm::vec3(m[2])));
    for (int i = 0; i < 3; ++i)
        if (axisScale[i] < 1e-6f)
            axisScale[i] = 1e-6f;
    glm::mat3 rotation(glm::vec3(m[0]) / axisScale.x,
                       glm::vec3(m[1]) / axisScale.y,
                       glm::vec3(m[2]) / axisScale.z);
    out.orientation = glm::normalize(glm::quat_cast(rotation));
    out.scale = axisScale;
    return out;
}

bool validViewmodelSocket(const ViewmodelSocketDef& socket)
{
    return !socket.name.empty() && !socket.joint.empty() &&
           finite(socket.offset) && finite(socket.rotationDegrees) &&
           std::isfinite(socket.scale) && socket.scale > 0.0f;
}

void ViewmodelSocketSet::build(eng::Renderer& renderer,
                               eng::NodeHandle rigNode,
                               const eng::animation::AnimationRig& rig,
                               const std::vector<ViewmodelSocketDef>& sockets)
{
    clear(renderer);
    if (!rigNode.valid())
        return;

    for (const ViewmodelSocketDef& def : sockets) {
        if (!validViewmodelSocket(def)) {
            eng::log::warn("Viewmodel socket '%s': rejected definition",
                           def.name.c_str());
            continue;
        }
        const int joint = rig.jointIndex(def.joint);
        if (joint < 0) {
            // Named, not silent: an author who mistyped a joint gets a line
            // instead of a weapon that hangs in mid-air for reasons nobody
            // can see from the game.
            eng::log::warn("Viewmodel socket '%s': rig has no joint '%s'",
                           def.name.c_str(), def.joint.c_str());
            continue;
        }
        Live live;
        live.def = def;
        live.joint = joint;
        live.node = renderer.createNode(rigNode, glm::vec3(0.0f),
                                        "viewmodel-socket-" + def.name);
        if (!live.node.valid())
            continue;
        mSockets.push_back(std::move(live));
    }
}

void ViewmodelSocketSet::clear(eng::Renderer& renderer)
{
    for (Live& live : mSockets)
        if (live.node.valid())
            renderer.destroyNode(live.node);
    mSockets.clear();
}

void ViewmodelSocketSet::update(eng::Renderer& renderer,
                                std::span<const glm::mat4> jointModelMatrices)
{
    for (const Live& live : mSockets) {
        if (!live.node.valid() || live.joint < 0 ||
            std::size_t(live.joint) >= jointModelMatrices.size())
            continue;
        const SocketTransform t = socketTransform(
            jointModelMatrices[std::size_t(live.joint)], live.def);
        renderer.setPosition(live.node, t.position);
        renderer.setOrientation(live.node, t.orientation);
        renderer.setScale(live.node, t.scale);
    }
}

eng::NodeHandle ViewmodelSocketSet::node(const std::string& name) const
{
    const auto it = std::find_if(
        mSockets.begin(), mSockets.end(),
        [&](const Live& live) { return live.def.name == name; });
    return it == mSockets.end() ? eng::NodeHandle{} : it->node;
}

bool ViewmodelSocketSet::worldTransform(const eng::Renderer& renderer,
                                        const std::string& name,
                                        eng::NodeTransform& out) const
{
    const eng::NodeHandle handle = node(name);
    return handle.valid() && renderer.nodeWorldTransform(handle, out);
}

std::vector<std::string> ViewmodelSocketSet::names() const
{
    std::vector<std::string> out;
    out.reserve(mSockets.size());
    for (const Live& live : mSockets)
        out.push_back(live.def.name);
    return out;
}

} // namespace game
