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
    // False on parse/load failure (details logged). meshDir must contain the
    // .obj files referenced by the scene file.
    bool load(eng::Renderer& r, const std::string& sceneToml,
              const std::string& meshDir, eng::NodeHandle sunParent);

    void update(eng::Renderer& r, float t) const; // animated boxes + shadows

    // Sun light node (child of sunParent) -- apps may re-orient it.
    eng::NodeHandle sunNode() const { return mSun; }

private:
    eng::NodeHandle mSun{};
    struct Anim {
        eng::NodeHandle node;
        bool reverse = false;
    };
    std::vector<Anim> mSinPans;
    std::vector<Anim> mShadowScales;
};
