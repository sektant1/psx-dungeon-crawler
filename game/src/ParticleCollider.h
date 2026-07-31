#pragma once
#include <eng/Physics.h>
#include <eng/particles/ParticleCollider.h>

namespace game {

// Adapts the particle simulation's view of the world onto Jolt.
//
// This lives in the game rather than the engine on purpose. eng_systems owns
// both the renderer and physics, so the dependency would link, but the *policy*
// here is a gameplay decision: which layers a spark is allowed to bounce off is
// the same kind of choice as which layers a projectile hits, and it belongs
// next to those. The engine only defines the seam.
class JoltParticleCollider final : public eng::IParticleCollider
{
public:
    JoltParticleCollider(const eng::Physics& physics, eng::CollisionMask mask)
        : mPhysics(physics), mMask(mask) {}

    bool sweep(glm::vec3 from, glm::vec3 to, glm::vec3& hitPos,
               glm::vec3& hitNormal) override;

private:
    const eng::Physics& mPhysics;
    eng::CollisionMask mMask;
};

} // namespace game
