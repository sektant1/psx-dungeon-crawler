#pragma once
#include <eng/Handles.h>
#include <eng/Physics.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace eng { class Renderer; }
struct CombatConfig;

// Caster kit: a Fireball physics projectile and an instant Beam hitscan, each
// with attached/one-shot particle VFX. Mirrors ProjectileSystem's lifecycle.
// All tunables (speed, radius, impulse, colours, particles) are read live from
// the CombatConfig set via setConfig, so debug-UI edits apply on the next cast.
class SpellSystem {
public:
    void init(eng::Renderer&);
    void setConfig(const CombatConfig* cfg) { mCfg = cfg; }
    void castFireball(eng::Physics&, eng::Renderer&, glm::vec3 eye, glm::vec3 fwd);
    void castBeam    (eng::Physics&, eng::Renderer&, glm::vec3 eye, glm::vec3 fwd);
    void onHit(eng::Physics&, eng::Renderer&, const eng::HitEvent&);
    void fixedUpdate(eng::Physics&, eng::Renderer&, float dt);
    void syncRender(eng::Physics&, eng::Renderer&);
    void clear(eng::Physics&, eng::Renderer&);

private:
    struct Fireball { eng::BodyHandle body; eng::NodeHandle node; float ttl; bool dead = false; eng::ParticlesHandle trailFx{}; };
    struct Transient { eng::NodeHandle node; float ttl; };

    std::vector<Fireball> mFireballs;
    std::vector<Transient> mTransients;
    eng::MeshHandle mFireballMesh{};
    eng::MeshHandle mBeamMesh{};
    int mMaxFireballs = 24;
    const CombatConfig* mCfg = nullptr;

    void despawnFireball(eng::Physics&, eng::Renderer&, Fireball&);
    eng::NodeHandle spawnBurst(eng::Renderer&, glm::vec3 pos,
                               const std::string& particleName, float ttl,
                               glm::vec3 lightColour, float lightRange);
};
