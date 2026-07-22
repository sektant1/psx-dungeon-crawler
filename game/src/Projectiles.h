#pragma once
#include <eng/Handles.h>
#include <eng/Physics.h>
#include <glm/glm.hpp>
#include <vector>

namespace eng { class Renderer; }
struct CombatConfig;

class ProjectileSystem {
public:
    void init(eng::Renderer& r);
    void setConfig(const CombatConfig* cfg) { mCfg = cfg; }
    void fireArrow(eng::Physics&, eng::Renderer&, glm::vec3 eye, glm::vec3 forward);
    void onHit(eng::Physics&, const eng::HitEvent&);
    void fixedUpdate(eng::Physics&, eng::Renderer&, float dt);
    void syncRender(eng::Physics&, eng::Renderer&);
    void clear(eng::Physics&, eng::Renderer&);

private:
    enum class Kind { Arrow };
    struct Projectile {
        eng::BodyHandle body;
        eng::NodeHandle node;
        Kind kind;
        float ttl;
        bool stuck = false;
    };

    std::vector<Projectile> mLive;
    eng::MeshHandle mArrowMesh{};
    int mMaxLive = 40;
    const CombatConfig* mCfg = nullptr;

    void despawn(eng::Physics&, eng::Renderer&, Projectile&);
};
