#pragma once

#include "PlayerWeapons.h"

#include <eng/Handles.h>
#include <eng/Physics.h>

#include <glm/glm.hpp>

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eng { class Renderer; }

class ProjectileSystem {
public:
    using ImpactFn = std::function<void(eng::BodyHandle victim,
                                        const std::string& payload,
                                        glm::vec3 travelDirection,
                                        glm::vec3 point)>;

    void setImpactCallback(ImpactFn fn) { mOnImpact = std::move(fn); }
    void fire(eng::Physics&, eng::Renderer&, const game::PlayerWeaponDef&,
              glm::vec3 aimOrigin, glm::vec3 aimDirection,
              std::optional<glm::vec3> muzzleOrigin = std::nullopt);
    void onHit(eng::Physics&, eng::Renderer&, const eng::HitEvent&);
    void fixedUpdate(eng::Physics&, eng::Renderer&, float dt);
    void syncRender(eng::Physics&, eng::Renderer&);
    void clear(eng::Physics&, eng::Renderer&);

private:
    struct Projectile {
        eng::BodyHandle body;
        eng::NodeHandle node;
        eng::ParticlesHandle trail;
        std::string payload;
        std::string impactEffect;
        glm::vec3 direction{0.0f, 0.0f, -1.0f};
        float ttl = 0.0f;
        bool impacted = false;
    };

    eng::MeshHandle meshFor(eng::Renderer&,
                            const game::PlayerWeaponDef& definition);
    void despawn(eng::Physics&, eng::Renderer&, Projectile&);

    std::vector<Projectile> mLive;
    std::unordered_map<std::string, eng::MeshHandle> mMeshes;
    int mMaxLive = 96;
    ImpactFn mOnImpact;
};
