#pragma once
#include <eng/particles/ParticleTypes.h>

#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <string>

namespace Ogre { class SceneManager; }

namespace eng {

// Emitted by the particle simulation when a colliding particle asks for a mark.
// Only forward-declared here: the struct lives in the simulation's private
// header (engine/src/particles/ParticleSim.h) and every caller that has a
// request to hand over has necessarily already included it, so pulling a
// private header into the public API would buy nothing.
struct DecalRequest;

// Everything a decal profile varies. Profiles are named by string id and are
// registered by whoever owns the TOML: this class deliberately never parses a
// file and never learns what a "blood" decal is. Bullet holes, scorch marks and
// gore all differ only in these numbers.
struct DecalProfileDesc {
    // Texture *stem*, the same vocabulary particle effects use: the PNG in
    // assets/particles/textures/. The material is generated from it, so adding
    // a decal texture never means editing a .material file.
    std::string texture;

    // Spawn size range in world units, picked uniformly per decal. A range
    // rather than a value because identically sized marks read as tiling.
    float sizeMin = 0.30f;
    float sizeMax = 0.45f;

    // Tint applied to the texture, plus the per-spawn jitter magnitude. The
    // jitter is symmetric (+/- the value) and is what keeps a wall of hits from
    // looking like one repeated stamp.
    glm::vec4 colour{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec3 colourJitter{0.0f};
    float     alphaJitter = 0.0f;

    // Age in seconds at which the decal is removed. Zero means permanent, in
    // which case it can still be evicted by the budget but never by time.
    float lifetime = 0.0f;
    // Seconds of alpha ramp-down immediately before removal. Clamped to
    // lifetime; ignored when the decal is permanent.
    float fadeTime = 1.0f;

    // Pooling. A pooled decal spreads on the surface over time and absorbs
    // later hits nearby instead of adding to the decal count, which is the only
    // reason a floor under sustained fire does not blow the budget in a second.
    bool  pool        = false;
    float growthRate  = 0.0f;   // world units of size per second
    float maxSize     = 1.20f;  // hard ceiling for growth and merges
    float mergeRadius = 0.0f;   // 0 disables merging entirely

    // Distance the quad is lifted along the surface normal. The material also
    // carries a depth bias; the offset exists because a bias alone still
    // z-fights at grazing angles on a low-precision depth buffer.
    float normalOffset = 0.01f;

    // Random yaw about the normal. On by default, off for marks whose texture
    // has a meaningful up direction (a drip, an arrow).
    bool randomRotation = true;

    ParticleBlend blend = ParticleBlend::Alpha;
};

// World-space decal marks with a hard budget.
//
// Projection is deliberately naive: one oriented quad lying on the hit plane,
// not a clipped mesh projection. That is the accepted trade in the particle
// redesign spec -- it costs one instance and no geometry work, and its known
// failure is leakage across convex corners and off the edges of small props.
//
// Rendering reuses the ParticleBatch pattern verbatim (static base quad on
// vertex source 0, HBU_CPU_TO_GPU instance stream on source 1, HBL_DISCARD
// upload). The instance record differs -- a decal carries a surface frame
// instead of a screen rotation -- so it is a sibling renderable rather than a
// reuse of IParticleBatch, but the mechanism is the same one.
class DecalSystem {
public:
    DecalSystem();
    ~DecalSystem();

    DecalSystem(const DecalSystem&) = delete;
    DecalSystem& operator=(const DecalSystem&) = delete;

    // Batches attach to the scene manager's root node, so decal positions are
    // world-space. Safe to call with null, which leaves the system simulating
    // but drawing nothing (headless tests, tooling).
    void attach(Ogre::SceneManager* sceneManager);
    // Drops every decal and every batch. Must be called before the scene
    // manager is destroyed; the destructor does it too.
    void shutdown();

    // Profiles may be re-registered at any time; a hot-reload therefore only
    // needs to call this again. Live decals keep the values they spawned with
    // for size and colour but pick up the new lifetime and growth, which is
    // what makes tuning in the debug panel feel immediate.
    void registerProfile(const std::string& id, const DecalProfileDesc& desc);
    const DecalProfileDesc* profile(const std::string& id) const;

    // Places a mark, merges it into a neighbouring pool, or evicts to make
    // room. Returns false only when the profile is unknown or the normal is
    // degenerate -- a merge still counts as success.
    bool spawn(const DecalRequest& request);

    // Ages, fades, grows pools and retires. Cheap enough to call every frame at
    // the budget sizes this system is meant for.
    void update(float dt);
    // Same, plus back-to-front sorting against the camera. Coplanar decals
    // blend correctly in any order; overlapping ones on different surfaces do
    // not, so pass the camera when the caller has it.
    void update(float dt, const glm::vec3& cameraWorldPos);

    void clear();

    // Hard cap. Lowering it below the live count evicts immediately, oldest
    // first. Unbounded decals are the classic way this feature eats a frame
    // budget, so there is no "unlimited" setting.
    void setBudget(std::size_t maxDecals);
    std::size_t budget() const;

    // Live decal count, for the debug panel.
    std::size_t count() const;

    void setVisible(bool visible);

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace eng
