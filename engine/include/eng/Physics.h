#pragma once
#include <eng/Handles.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <functional>
#include <memory>
#include <vector>

namespace eng {

enum class BodyLayer { Static, Player, Prop, Projectile, Trigger };
enum class ShapeKind { Box, Sphere, Capsule, Cylinder };

struct BodyDesc {
    ShapeKind kind = ShapeKind::Box;
    glm::vec3 halfExtents{0.5f};
    float radius = 0.5f;
    float halfHeight = 0.5f;
    glm::vec3 position{0.0f};
    glm::quat orientation{1,0,0,0};
    BodyLayer layer = BodyLayer::Prop;
    bool dynamic = true;
    bool sensor = false;
    bool continuousCast = false;
    float mass = 1.0f;
    float friction = 0.5f;
    float restitution = 0.1f;
};

struct CharacterDesc {
    float radius = 0.30f;
    float height = 1.7f;
    glm::vec3 position{0.0f};
    float maxSlopeDeg = 46.0f;
    float stepHeight = 0.4f;
    float mass = 80.0f;
};

enum class GroundState { OnGround, OnSteepSlope, InAir };
struct CharacterState {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 groundNormal{0,1,0};
    GroundState ground = GroundState::InAir;
    bool grounded() const { return ground == GroundState::OnGround; }
};

struct RayHit  { BodyHandle body; glm::vec3 point{0}; glm::vec3 normal{0}; float fraction = 1.0f; };
struct ShapeHit { BodyHandle body; glm::vec3 point{0}; glm::vec3 normal{0}; float penetration = 0.0f; };
struct HitEvent { BodyHandle self; BodyHandle other; glm::vec3 point{0}; glm::vec3 normal{0}; float impulse = 0.0f; };

class Physics {
public:
    Physics();
    ~Physics();
    void init();
    void shutdown();
    void update(float fixedDt, int collisionSteps = 1);
    float interpolationAlpha() const;
    void setInterpolationAlpha(float alpha);

    BodyHandle createBody(const BodyDesc&);
    BodyHandle createMeshBody(const std::vector<glm::vec3>& verts,
                              const std::vector<uint32_t>& indices,
                              glm::vec3 pos, glm::quat rot, BodyLayer);
    void removeBody(BodyHandle);
    void setBodyTransform(BodyHandle, glm::vec3, glm::quat);
    void getRenderTransform(BodyHandle, glm::vec3& pos, glm::quat& rot) const;
    void applyImpulse(BodyHandle, glm::vec3 impulse, glm::vec3 atPoint);
    void setBodyKinematic(BodyHandle, bool);
    int  activeBodyCount() const;

    CharacterHandle createCharacter(const CharacterDesc&);
    void removeCharacter(CharacterHandle);
    void characterSetVelocity(CharacterHandle, glm::vec3 velocity);
    void characterUpdate(CharacterHandle, float dt);
    CharacterState characterState(CharacterHandle) const;
    void characterSetShape(CharacterHandle, float radius, float height);

    bool rayCast(glm::vec3 from, glm::vec3 dir, float dist, RayHit&, BodyLayer mask) const;
    int  shapeCast(const BodyDesc& shape, glm::vec3 from, glm::vec3 to,
                   std::vector<ShapeHit>&, BodyLayer mask) const;
    int  overlap(const BodyDesc& shape, glm::vec3 at,
                 std::vector<ShapeHit>&, BodyLayer mask) const;

    using HitCallback = std::function<void(const HitEvent&)>;
    void setContactCallback(HitCallback);

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace eng
