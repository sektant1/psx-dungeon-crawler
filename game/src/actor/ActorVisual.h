#pragma once

#include "ActorAnimator.h"
#include "ActorRig.h"

#include <eng/Handles.h>

#include <glm/glm.hpp>

#include <string>

namespace eng { class Renderer; }

namespace game::actor {

// Which point of an actor a position refers to.
//
// This exists because the three things that own a body disagree, and each was
// right about itself: the player's character node is at the FEET, a Jolt
// capsule reports its CENTRE, and an NPC's node was authored raised by half its
// height. Three call sites converting between those by hand is three chances to
// bury an actor to the waist or float it -- which is exactly what happened.
//
// Naming the frame instead means a caller states a fact it already knows and
// never does the arithmetic. Adding a fourth kind of actor is one enumerator
// choice, not a sign to get right.
enum class ActorAnchor {
    Feet,   // the ground the actor stands on -- character controllers, markers
    Centre, // the middle of its stated height -- a physics capsule's transform
};

// What a body looks like. Grouped into a struct because create() had six
// positional arguments heading for eight, and `create(r, rig, node, mat, 1.8f,
// true)` cannot be read at the call site.
struct ActorVisualDesc {
    std::string material = "Game/Actor/Default";
    // Metres, total. The rig is scaled to it, so a 1.4 m goblin and a 2.4 m
    // brute are the same mesh and the same clips.
    float height = 1.8f;
    ActorAnchor anchor = ActorAnchor::Feet;
    // Art multiplier on top of the height fit, for a definition that wants a
    // squat or a stretched silhouette. Applied on the rig's own node, never on
    // the parent: scaling a parent also scales the offset its child sits at,
    // which is a second way to sink an actor into the floor.
    glm::vec3 scale{1.0f};
    bool castShadows = true;
};

// One actor's body in the world: a node, a skin instance over the SHARED rig
// geometry, and the animator that poses it.
//
// This is the whole of what the player's avatar, an enemy and an NPC have in
// common visually, and it is deliberately not an ECS component: the player's
// rig is not an entity, enemies live on the combat director's registry, and
// NPCs on the level's. A small owned object each of them can hold is the thing
// all three can actually share.
//
// The visual owns its node completely -- position, orientation and scale. A
// caller that also writes those on the same node is fighting it; a caller that
// wants the body to ride something else passes that as `parent`.
class ActorVisual {
public:
    // `parent` is usually kRootNode. The player's avatar hangs off the body
    // node instead, so what you see turn is what the simulation turned -- and
    // so it inherits that node's interpolation rather than being re-placed a
    // frame late.
    //
    // When parented, the anchor describes the PARENT: `Feet` puts the rig at
    // the parent's origin, `Centre` drops it half a height.
    bool create(eng::Renderer&, const ActorRig&, eng::NodeHandle parent,
                const ActorVisualDesc&);
    void destroy(eng::Renderer&);
    bool valid() const { return mNode.valid() && mAnimator.valid(); }

    // Where the actor is, in whatever frame the desc named, and which way it
    // faces. Separate from update() because the simulation writes the transform
    // and the pose advances on the stepped presentation clock.
    void setTransform(eng::Renderer&, glm::vec3 position, float yawRadians);
    void update(eng::Renderer&, float dt, const ActorAnimationInput&);
    void setVisible(eng::Renderer&, bool);

    ActorAnimator& animator() { return mAnimator; }
    const ActorAnimator& animator() const { return mAnimator; }
    eng::NodeHandle node() const { return mNode; }
    // Uniform scale applied to fit the rig to this actor's stated height. The
    // caller needs it to keep an attachment or an effect on the same scale.
    float scale() const { return mScale; }
    // Feet position for a point given in this body's anchor frame, and the
    // reverse. Public because the animator's input is authored in world space
    // from the feet up (eye height, look targets) and the caller should not
    // re-derive the same half-height a second time.
    glm::vec3 feetOf(glm::vec3 position) const
    {
        return position - glm::vec3(0.0f, anchorRise(), 0.0f);
    }

private:
    // How far the anchor sits above the feet. Zero for Feet, half the actor's
    // stated height for Centre.
    float anchorRise() const;

    ActorAnimator mAnimator;
    eng::NodeHandle mNode{};
    eng::SkinInstanceHandle mSkin{};
    float mScale = 1.0f;
    float mHeight = 1.8f;
    ActorAnchor mAnchor = ActorAnchor::Feet;
};

} // namespace game::actor
