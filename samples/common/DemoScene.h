#pragma once
#include <eng/Handles.h>

#include <string>
#include <vector>

namespace eng {
class Renderer;
}

// Shared PSX demo scene (environment palette, sun, omni lamps, light shaft,
// animated boxes + sparkles, blob shadows, crystals), described by a TOML
// file (see samples/common/assets/demo_scene.toml). The sun light is created
// under `sunParent` so apps can orbit it (demo) or keep it static (game).
class DemoScene
{
public:
    // Section toggles: apps that re-dress the scene (the game's dungeon
    // hall) can skip the demo's centrepiece geometry while keeping the
    // lights/environment.
    struct Options {
        bool crystals = true;
        bool boxes = true; // animated boxes + sparkles
        bool blobShadows = true;
    };

    // False on parse/load failure (details logged). meshDir must contain the
    // .obj files referenced by the scene file.
    bool load(eng::Renderer& r, const std::string& sceneToml,
              const std::string& meshDir, eng::NodeHandle sunParent,
              const Options& opts);
    bool load(eng::Renderer& r, const std::string& sceneToml,
              const std::string& meshDir, eng::NodeHandle sunParent)
    {
        return load(r, sceneToml, meshDir, sunParent, Options());
    }

    void update(eng::Renderer& r, float t) const; // animated boxes + shadows

    // Sun light node (child of sunParent) -- apps may re-orient it.
    eng::NodeHandle sunNode() const { return mSun; }
    // The sun's light -- apps may retint/dim it (e.g. the game turns it
    // into faint moonlight for the dungeon).
    eng::LightHandle sunLight() const { return mSunLight; }
    // Omni lamp nodes, in x_positions order -- apps may reposition them
    // (the game grounds them into braziers).
    const std::vector<eng::NodeHandle>& omniNodes() const { return mOmnis; }

private:
    eng::NodeHandle mSun{};
    eng::LightHandle mSunLight{};
    std::vector<eng::NodeHandle> mOmnis;
    struct Anim {
        eng::NodeHandle node;
        bool reverse = false;
    };
    std::vector<Anim> mSinPans;
    std::vector<Anim> mShadowScales;
};
