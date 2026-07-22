#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <mutex>
#include <vector>

namespace eng::phys {

namespace Layers {
    static constexpr JPH::ObjectLayer STATIC     = 0;
    static constexpr JPH::ObjectLayer PLAYER     = 1;
    static constexpr JPH::ObjectLayer PROP       = 2;
    static constexpr JPH::ObjectLayer PROJECTILE = 3;
    static constexpr JPH::ObjectLayer TRIGGER    = 4;
    static constexpr JPH::ObjectLayer COUNT      = 5;
}

namespace BroadPhaseLayers {
    static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1);
    static constexpr unsigned int COUNT = 2;
}

class BPLayerInterface final : public JPH::BroadPhaseLayerInterface {
public:
    unsigned int GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::COUNT; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer l) const override {
        return (l == Layers::STATIC || l == Layers::TRIGGER)
             ? BroadPhaseLayers::NON_MOVING : BroadPhaseLayers::MOVING;
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
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        using namespace Layers;
        auto pair = [&](JPH::ObjectLayer x, JPH::ObjectLayer y){ return (a==x&&b==y)||(a==y&&b==x); };
        if (a == STATIC && b == STATIC) return false;
        if (pair(PROJECTILE, PROJECTILE)) return false;
        if (pair(PROJECTILE, TRIGGER)) return false;
        if (a == TRIGGER && b == TRIGGER) return false;
        if (pair(TRIGGER, PROP)) return false;
        if (pair(TRIGGER, PROJECTILE)) return false;
        return true;
    }
};

class CharacterPushListener final : public JPH::CharacterContactListener {
public:
    JPH::PhysicsSystem* system = nullptr;

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
        JPH::Vec3 push = -inContact.mContactNormal * (into * 2.0f);
        bi.AddImpulse(inContact.mBodyB, push);
    }
};

} // namespace eng::phys
