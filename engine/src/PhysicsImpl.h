#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <eng/Physics.h>
#include <mutex>
#include <vector>

namespace eng::phys {

// Jolt object layers map one-to-one onto eng::CollisionLayer indices, so the
// filters below are pure lookups into the application's PhysicsSetup. Nothing
// here knows what any layer means.

namespace BroadPhaseLayers {
    static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1);
    static constexpr unsigned int COUNT = 2;
}

class BPLayerInterface final : public JPH::BroadPhaseLayerInterface {
public:
    const PhysicsSetup* setup = nullptr;

    unsigned int GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::COUNT; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer l) const override {
        const bool moving = !setup || size_t(l) >= setup->layers.size()
                                   || setup->layers[size_t(l)].moving;
        return moving ? BroadPhaseLayers::MOVING : BroadPhaseLayers::NON_MOVING;
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer) const override { return "layer"; }
#endif
};

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer, JPH::BroadPhaseLayer) const override { return true; }
};

class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    const PhysicsSetup* setup = nullptr;

    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        if (!setup || a >= kMaxCollisionLayers || b >= kMaxCollisionLayers)
            return true;
        return any(setup->collides[a] & layerMask(CollisionLayer(b)));
    }
};

class CharacterPushListener final : public JPH::CharacterContactListener {
public:
    JPH::PhysicsSystem* system = nullptr;
    // Multiplier on the character's speed into the prop. Pure feel, so it
    // comes from the application's PhysicsSetup rather than living here.
    float pushImpulse = 2.0f;

    void OnContactAdded(const JPH::CharacterVirtual* inCharacter,
                        const JPH::CharacterContact& inContact,
                        JPH::CharacterContactSettings& ioSettings) override {
        ioSettings.mCanPushCharacter  = true;
        ioSettings.mCanReceiveImpulses = true;
        if (!system) return;
        if (inContact.mMotionTypeB != JPH::EMotionType::Dynamic) return;
        JPH::BodyInterface& bi = system->GetBodyInterface();
        JPH::Vec3 v = inCharacter->GetLinearVelocity();
        v.SetY(0.0f);
        float into = -inContact.mContactNormal.Dot(v);   // speed into the prop
        if (into <= 0.0f) return;
        if (pushImpulse <= 0.0f) return;
        JPH::Vec3 push = -inContact.mContactNormal * (into * pushImpulse);
        bi.AddImpulse(inContact.mBodyB, push);
    }
};

} // namespace eng::phys
