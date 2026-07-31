// ogre-psx-demo -- port of MenacingMecha's godot-psx-style-demo, driven
// through the eng public API (no Ogre/SDL includes here). The scene itself
// is ShowcaseScene: three set pieces on a turntable, built in code because what
// it demonstrates is the renderer rather than the content pipeline.

#include "ShowcaseScene.h"

#include <eng/Math.h>
#include <eng/Primitive.h>
#include <eng/RenderPresetInfo.h>
#include <eng/particles/ParticleLibrary.h>
#include <eng/app/Application.h>
#include <eng/render/Warmup.h>
#include <eng/debug/Console.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>

namespace {

// world/orbit_camera.gd: rotation.y = base + t
struct OrbitCamera {
  eng::NodeHandle node;
  float baseYaw = 0.0f;
  void update(eng::Renderer &r, float t) const {
    r.setOrientation(node, glm::angleAxis(baseYaw + t, glm::vec3(0, 1, 0)));
  }
};

// The engine's flagship feature: live-swappable render profiles. The list and
// its numbering come from the engine (eng::renderPresets()) rather than a local
// table -- a UI carrying its own array of names and trusting its index to match
// the engine's ids is the exact bug that header exists to kill.

// Floating placard (same diegetic label style as the game's showcase
// exhibits) naming the demo and the live preset. Text sprites have no
// update-in-place API, so refreshing the label means destroying and
// re-attaching the node.
class PresetSign {
public:
  explicit PresetSign(eng::NodeHandle parent) : mParent(parent) {}

  void show(eng::Renderer &r, const std::string &presetName) {
    if (mNode.valid())
      r.destroyNode(mNode);
    mNode = r.createNode(mParent, {0.0f, 4.4f, 0.0f});
    eng::TextSpriteStyle style;
    style.worldHeight = 0.30f;
    style.accentColour = {0.90f, 0.70f, 0.30f, 1.0f};
    style.colourRules.push_back({presetName, {0.95f, 0.82f, 0.38f, 1.0f}});
    r.attachTextSprite(mNode,
                       "PSX ENGINE DEMO\n"
                       "Preset: " +
                           presetName + "\n" + "[Tab] change preset" + "\n" +
                           "[Space] pause" + "\n" + "[R] restart" + "\n" +
                           "[Esc] quit",
                       style);
  }

private:
  eng::NodeHandle mParent;
  eng::NodeHandle mNode{};
};

// The demo as an eng::Application: scene build in onStart, the orbit/bob
// animation in onUpdate. No fixed step -- nothing here is simulated.
class DemoApp : public eng::Application {
public:
  eng::AppConfig configure(int, char **) override {
    eng::AppConfig cfg;
    cfg.assetDir = APP_ASSET_DIR;
    cfg.configPath = cfg.assetDir + "/demo.toml";
    cfg.fixedDt = 0.0f;
    cfg.loadingTitle = "PSX SHOWCASE";
    return cfg;
  }

  // The demo builds its scene in onStart, but the shader compiles behind it are
  // the same first-frame hitch every app pays; warming them here moves the cost
  // under the loading screen.
  void onLoad(eng::Engine &engine, eng::LoadPlan &plan) override {
    (void)engine;
    eng::addRenderWarmup(plan);
  }

  bool onStart(eng::Engine &engine) override {
    eng::Renderer &r = engine.renderer();
    const std::string assets = APP_ASSET_DIR;
    // Aliases so the scene build below reads as straight-line setup code while
    // the state it produces outlives the call.
    OrbitCamera &orbit = mOrbit;
    size_t &presetIndex = mPresetIndex;

    // Camera3D: fov 68.1243 vertical, Godot default clips.
    // 52 deg rather than the Godot port's 68: the wide lens stretched the
  // dais into the corners and made the stations look further apart than they
  // are.
  r.setCameraFov(46.0f);
    r.setCameraClip(0.05f, 4000.0f);

    // -------------------------------------------------- OrbitPoint branch ---
    orbit.node = r.createNode(eng::kRootNode);
    orbit.baseYaw = std::atan2(-0.556238f, 0.831023f);

    // Framed for ShowcaseScene: far enough back that all three stations fit as
  // the turntable brings them round, high enough to read the dais as a floor
  // rather than as a horizon line.
  eng::NodeHandle camNode =
      r.createNode(orbit.node, {0.0f, 4.3f, 12.2f});
    r.setOrientation(camNode, glm::angleAxis(glm::radians(-16.0f),
                                           glm::vec3(1.0f, 0.0f, 0.0f)));
    r.attachCamera(camNode);

    // ---------------------------------------------------------- particles ---
    // Registered BEFORE the scene is built: spawnParticles() resolves an effect
    // by name at the call site and no-ops silently on a miss, so a library
    // loaded afterwards leaves every emitter in the showcase dead. That is
    // exactly what had happened -- assets/particles.toml was authored for this
    // scene and never loaded by anything.
    mParticles.load(r, assets + "/particles.toml");

    // ------------------------------------------------------------- scene ---
  if (!mScene.build(r, assets))
    return false;

  // ------------------------------------------------------ preset sign ---
    // Seed from PSX_RENDER_PRESET (already applied once by Engine::init) so the
    // label matches reality, then let Tab/Backspace cycle it live. With no
    // override the engine is on its default profile, so start the cursor there
    // rather than on whatever happens to be first in the table.
    {
      const char *presetName = std::getenv("PSX_RENDER_PRESET");
      const int id = presetName ? eng::renderPresetFromName(presetName)
                                : eng::kDefaultRenderPreset;
      const auto &presets = eng::renderPresets();
      for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].id == id)
          presetIndex = i;
    }
    // Shared engine console: the demo's own switches, reachable by name. Same
    // window the game and the editor open, so nothing here is demo-specific
    // beyond the command list.
    mConsole.captureEngineLog();
    mConsole.registerCommand("quit", "close the demo",
                             [&engine](const eng::DebugConsole::Args &) {
                               engine.requestClose();
                             });
    mConsole.registerCommand("pause", "freeze/unfreeze the turntable",
                             [this](const eng::DebugConsole::Args &) {
                               mPaused = !mPaused;
                             });
    mConsole.registerCommand("restart", "rewind the animation clock",
                             [this](const eng::DebugConsole::Args &) {
                               mAnimTime = 0.0f;
                             });
    mConsole.registerCommand(
        "r.preset", "list render profiles, or switch to one by name",
        [this, &r](const eng::DebugConsole::Args &a) {
          const auto &presets = eng::renderPresets();
          if (a.size() > 1) {
            const int id = eng::renderPresetFromName(a[1].c_str());
            for (size_t i = 0; i < presets.size(); ++i)
              if (presets[i].id == id)
                mPresetIndex = i;
            eng::applyRenderPreset(r, id);
            mSign->show(r, presets[mPresetIndex].name);
            return;
          }
          for (const auto &p : presets)
            mConsole.print(eng::log::Level::Info, "render", p.name);
        },
        [](const eng::DebugConsole::Args &) {
          std::vector<std::string> out;
          for (const auto &p : eng::renderPresets())
            out.emplace_back(p.name);
          return out;
        });

    mSign.emplace(mScene.root());
    mSign->show(r, eng::renderPresets()[presetIndex].name);

    // Set dressing belongs to ShowcaseScene::buildDressing, which places it in
    // the gaps between the stations. A second ring used to be built here as
    // well, at radius 3.3-4.3 -- inside the dais and across the station ring --
    // so every angle had a barrel in front of whatever the camera was meant to
    // be looking at. One scene, one dressing pass.

    orbit.update(r, 0.0f);
    return true;
  }

  void onFrameBegin(const eng::FrameContext &f) override {
    eng::Renderer &r = f.engine.renderer();
    eng::Input &in = f.engine.input();
    if (in.wasPressed("quit"))
      f.engine.requestClose();
    if (in.wasPressed("dev_console"))
      mConsole.toggle();
    if (in.wasPressed("pause"))
      mPaused = !mPaused;
    if (in.wasPressed("restart"))
      mAnimTime = 0.0f;
    if (in.wasPressed("preset_next") || in.wasPressed("preset_prev")) {
      const auto &presets = eng::renderPresets();
      const size_t n = presets.size();
      mPresetIndex = in.wasPressed("preset_next") ? (mPresetIndex + 1) % n
                                                  : (mPresetIndex + n - 1) % n;
      eng::applyRenderPreset(r, presets[mPresetIndex].id);
      mSign->show(r, presets[mPresetIndex].name);
    }
  }

  void onGui(const eng::FrameContext &) override { mConsole.draw(); }

  void onUpdate(const eng::FrameContext &f) override {
    if (!mPaused)
      mAnimTime += f.dt;
    eng::Renderer &r = f.engine.renderer();
    mOrbit.update(r, mAnimTime);
    mScene.update(r, mAnimTime);
  }

private:
  static inline const glm::vec3 kChestGlowColour =
      glm::vec3(1.0f, 0.62f, 0.22f) * 1.6f;

  OrbitCamera mOrbit;
  ShowcaseScene mScene;
  eng::ParticleLibrary mParticles;
  eng::DebugConsole mConsole;
  size_t mPresetIndex = 0;
  std::optional<PresetSign> mSign;
  bool mPaused = false;
  float mAnimTime = 0.0f;
};

} // namespace

int main(int argc, char **argv) {
  DemoApp app;
  return eng::runApplication(app, argc, argv);
}
