#pragma once
#include <eng/Handles.h>

#include <glm/glm.hpp>

#include <array>
#include <span>
#include <string>
#include <vector>

namespace eng {
class Renderer;
}

// The demo's showcase: three set pieces on a turntable, arranged so the camera
// meets them one at a time as it orbits.
//
// Built in code rather than loaded from a scene file on purpose. This is the
// engine's shop window, and what it is showing off is the *renderer* -- the
// crystal shader, the portal shader, the particle system, stencil shadows,
// banded lighting. Those are engine features, so the scene that demonstrates
// them belongs with the sample, not in the game's content pipeline (which is
// what the editor and tech_demo.scn are for).
//
// Layout is a centre plus two flanks, not a ring. The levitating chest holds
// the middle of the dais -- the one point the turntable never carries out of
// frame -- and the crystal shrine and the portal stand 120 degrees apart around
// it, so each is read on its own as the camera comes past and the space between
// them stays empty. A ring of evenly-spaced props, which is what this was,
// reads as clutter from every angle: there is always something behind whatever
// you are looking at.
struct ShowcaseOptions {
    float radius = 5.2f;     // distance from centre to each station
    float daisRadius = 3.4f; // the raised floor everything stands on
    bool particles = true;
    bool props = true;
};

// The generated surfaces whose material is a *choice* rather than a property of
// the mesh: flat stone the scene stands on. They are named so the tuning panel
// can re-dress them live with the dungeon materials the game is built from --
// which is the whole reason the kit atlases ship with the sample.
//
// Everything else (crystal, portal membrane, props) keeps its authored
// material: those are the shader profiles the demo exists to show.
enum class SurfaceSlot {
    DaisLower,
    DaisUpper,
    Plinth,
    PortalBacking,
    Shell,
    Count,
};

const char* surfaceSlotName(SurfaceSlot slot);

// Materials that tile cleanly over a generated primitive. Atlas profiles
// (Game/Kit/Dungeon, Game/Kit/Doors, Game/Kit/Containers) are deliberately absent: a
// primitive's 0..1 UVs stretch a whole atlas sheet over one face, which reads
// as a smear rather than as stone. The kit meshes that ARE atlas-mapped keep
// Game/Kit/Dungeon in code.
std::span<const char* const> surfaceMaterialChoices();

class ShowcaseScene {
public:
    using Options = ShowcaseOptions;

    // Content is named by logical path and resolved through the mount list
    // (assets/assets.toml): the demo pack holds only its own deltas and the
    // meshes come from the game pack underneath it.
    bool build(eng::Renderer& renderer, const Options& options = {});
    // Turntable + per-station animation. `time` is seconds; the caller owns
    // pausing by simply not advancing it.
    void update(eng::Renderer& renderer, float time);

    // Live re-dressing of one stone surface. Unknown material names are the
    // renderer's problem, not the scene's: it swaps and reports what is set.
    void setSurfaceMaterial(eng::Renderer& renderer, SurfaceSlot slot,
                            const std::string& material);
    const std::string& surfaceMaterial(SurfaceSlot slot) const
    {
        return mSurfaceMaterials[std::size_t(slot)];
    }

    eng::NodeHandle root() const { return mRoot; }
    // The point the camera should orbit and look at: the centre of the dais at
    // roughly the height of the set pieces, not the floor.
    glm::vec3 focus() const { return {0.0f, 1.5f, 0.0f}; }

private:
    void buildDais(eng::Renderer& renderer, const Options& options);
    void buildCrystalShrine(eng::Renderer& renderer, const Options& options,
                            glm::vec3 at);
    void buildPortal(eng::Renderer& renderer, const Options& options,
                     glm::vec3 at, float facingDeg);
    void buildTreasure(eng::Renderer& renderer, const Options& options,
                       glm::vec3 at);
    // Water / slime / lava pools, one per scrolling-shader profile.
    void buildLiquids(eng::Renderer& renderer, const ShowcaseOptions& options);
    void buildDressing(eng::Renderer& renderer, const Options& options);
    void buildLighting(eng::Renderer& renderer, const Options& options);

    // Attaches a generated primitive and remembers the node, so the surface can
    // be re-dressed later without the scene searching for it.
    void placeSurface(eng::Renderer& renderer, SurfaceSlot slot,
                      eng::NodeHandle node, eng::MeshHandle mesh,
                      const std::string& material, bool castShadows);

    eng::NodeHandle mRoot{};
    std::array<eng::NodeHandle, std::size_t(SurfaceSlot::Count)> mSurfaces{};
    std::array<std::string, std::size_t(SurfaceSlot::Count)>
        mSurfaceMaterials{};

    // Animated pieces, held so update() can drive them without searching.
    eng::NodeHandle mCrystalSpin{};
    std::vector<eng::NodeHandle> mCrystalShards;
    eng::NodeHandle mPortalNode{};
    eng::LightHandle mPortalLight{};
    eng::NodeHandle mChestBase{};
    eng::NodeHandle mChestSpin{};
    eng::LightHandle mChestLight{};
    eng::LightHandle mCrystalLight{};
    std::vector<eng::LightHandle> mLampLights;
};
