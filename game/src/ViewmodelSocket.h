#pragma once

#include <eng/Handles.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <span>
#include <string>
#include <vector>

namespace eng {
class Renderer;
struct NodeTransform;
namespace animation { class AnimationRig; }
} // namespace eng

namespace game {

// A named point on the first-person rig that things hang off.
//
// Before this existed the only code that read a joint's frame was
// FirstPersonHands::muzzleJointWorld, which did the matrix arithmetic inline --
// so "put this mesh in that hand" had no primitive under it, and a weapon
// viewmodel was impossible to attach at all. A socket is that primitive: an
// ordinary scene-graph node whose local transform is rewritten each frame from
// the joint's model matrix, so anything parentable is attachable.
//
// The muzzle is one of these rather than a special case, and a torch, a shield
// or a spell effect at a fingertip would be too.
struct ViewmodelSocketDef {
    // What a weapon (or the editor's combo box) names it by.
    std::string name;
    // The skeleton joint it rides. Resolved once, against the cooked rig.
    std::string joint;
    // Joint space, applied after the joint's own pose. This is the authored
    // half: the joint says where the hand is, the offset says where in the hand.
    glm::vec3 offset{0.0f};
    glm::vec3 rotationDegrees{0.0f};
    float scale = 1.0f;
};

// jointModel * offset/rotation/scale, as a matrix in the rig node's space.
//
// A free function over two plain values on purpose: it is the whole of the
// socket's maths, and keeping it renderer-free is what makes it testable
// without a GPU, a skeleton or a cooked asset (game/tests/ViewmodelSocketTests).
glm::mat4 socketLocalMatrix(const glm::mat4& jointModel,
                            const ViewmodelSocketDef& socket);

// The same thing decomposed into what Renderer::setPosition/Orientation/Scale
// take. Assumes the joint pose carries no shear, which a skeletal pose does not.
struct SocketTransform {
    glm::vec3 position{0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};
SocketTransform socketTransform(const glm::mat4& jointModel,
                                const ViewmodelSocketDef& socket);

// Every field finite, scale positive, name and joint non-empty.
bool validViewmodelSocket(const ViewmodelSocketDef& socket);

// The rig's sockets as live nodes: one child node per definition, re-posed from
// the animator's model matrices every frame.
//
// Owns its nodes and destroys them on clear(), because a weapon swap rebuilds
// what hangs beneath them and a level transition rebuilds the rig entirely.
class ViewmodelSocketSet {
public:
    // Creates a node per socket under `rigNode` and resolves each joint index
    // against `rig`. A socket naming a joint the skeleton does not have is
    // dropped with a warning rather than failing the whole set: one bad row in
    // viewmodel_hands.toml must not take the hands offline.
    void build(eng::Renderer& renderer, eng::NodeHandle rigNode,
               const eng::animation::AnimationRig& rig,
               const std::vector<ViewmodelSocketDef>& sockets);
    void clear(eng::Renderer& renderer);

    // Re-poses every socket node from this frame's skeleton pose.
    void update(eng::Renderer& renderer,
                std::span<const glm::mat4> jointModelMatrices);

    // The node to parent an attachment to, or an invalid handle when the rig
    // has no socket of that name. Callers treat that as "nothing to attach to",
    // which is also what an author gets for a typo they have not fixed yet.
    eng::NodeHandle node(const std::string& name) const;
    // World transform of a socket, for the muzzle and for gizmo anchoring.
    bool worldTransform(const eng::Renderer& renderer, const std::string& name,
                        eng::NodeTransform& out) const;
    // The names, in authored order -- the editor's socket picker reads this so
    // an author chooses from what the rig actually has.
    std::vector<std::string> names() const;
    bool empty() const { return mSockets.empty(); }

private:
    struct Live {
        ViewmodelSocketDef def;
        eng::NodeHandle node{};
        int joint = -1;
    };
    std::vector<Live> mSockets;
};

} // namespace game
