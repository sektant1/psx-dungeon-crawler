#pragma once
#include <eng/Handles.h>

#include <glm/glm.hpp>

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

class ShowcaseScene
{
public:
    using Options = ShowcaseOptions;

    bool build(eng::Renderer& renderer, const std::string& assetDir,
               const Options& options = {});
    // Turntable + per-station animation. `time` is seconds; the caller owns
    // pausing by simply not advancing it.
    void update(eng::Renderer& renderer, float time);

    eng::NodeHandle root() const { return mRoot; }
    // The point the camera should orbit and look at: the centre of the dais at
    // roughly the height of the set pieces, not the floor.
    glm::vec3 focus() const { return {0.0f, 1.5f, 0.0f}; }

private:
    void buildDais(eng::Renderer& renderer, const Options& options);
    void buildCrystalShrine(eng::Renderer& renderer, const std::string& assetDir,
                            const Options& options, glm::vec3 at);
    void buildPortal(eng::Renderer& renderer, const std::string& assetDir,
                     const Options& options, glm::vec3 at, float facingDeg);
    void buildTreasure(eng::Renderer& renderer, const std::string& assetDir,
                       const Options& options, glm::vec3 at);
    void buildDressing(eng::Renderer& renderer, const std::string& assetDir,
                       const Options& options);
    void buildLighting(eng::Renderer& renderer, const Options& options);

    std::string mAssetDir;
    eng::NodeHandle mRoot{};

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
