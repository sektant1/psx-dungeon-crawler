// ogre-psx-demo -- port of MenacingMecha's godot-psx-style-demo, driven
// through the eng public API (no Ogre/SDL includes here). The scene itself
// is ShowcaseScene: three set pieces on a turntable, built in code because what
// it demonstrates is the renderer rather than the content pipeline.

#include "DemoHud.h"
#include "ShowcaseScene.h"

#include <eng/DebugTools.h>
#include <eng/FrameStats.h>
#include <eng/Math.h>
#include <eng/Primitive.h>
#include <eng/RenderPresetInfo.h>
#include <eng/assets/AssetRoot.h>
#include <eng/particles/ParticleLibrary.h>
#include <eng/app/Application.h>
#include <eng/render/Warmup.h>
#include <eng/debug/Console.h>
#include <eng/debug/ParticlePanel.h>
#include <eng/debug/SurfacePanels.h>

#include <glm/gtc/quaternion.hpp>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace {

// Everything a viewer can move without rebuilding the scene. One struct, so
// "reset" is an assignment and the tuning panel has exactly one thing to edit.
struct DemoTuning {
    // world/orbit_camera.gd drove a single rotation; the rig here is the same
    // turntable with the camera's stand-off exposed.
    float distance = 12.2f;  // dolly, metres from the orbit point
    float height = 4.3f;     // eye height above the dais
    float pitchDeg = -16.0f; // framed for the dais to read as a floor
    float fovDeg = 46.0f;    // 46 rather than the Godot port's 68: the wide
                             // lens stretched the dais into the corners
    float orbitSpeed = 1.0f; // turntable rate multiplier
    float animSpeed = 1.0f;  // per-station animation rate multiplier
    float zoomSpeed = 0.12f; // fraction of the current distance per notch
    float dragSpeed = 0.35f; // degrees per mouse pixel
    float particleQuality = 1.0f;
};

constexpr float kMinDistance = 3.5f;
constexpr float kMaxDistance = 30.0f;
constexpr float kMinPitch = -70.0f;
constexpr float kMaxPitch = 25.0f;
constexpr float kMinFov = 20.0f;
constexpr float kMaxFov = 110.0f;
constexpr float kFovRate = 30.0f; // degrees per second while the key is held

// The turntable and the camera hanging off it. Splitting the two nodes is what
// lets the orbit keep running while the viewer dollies in and out: the spin
// lives on the parent, the framing on the child.
class OrbitRig {
public:
    void build(eng::Renderer& r, const DemoTuning& tuning)
    {
        mOrbit = r.createNode(eng::kRootNode);
        mBaseYaw = std::atan2(-0.556238f, 0.831023f);
        mCamera = r.createNode(mOrbit);
        r.attachCamera(mCamera);
        apply(r, tuning);
    }

    // Pushes the whole rig at once. Cheap enough to call every frame, which is
    // what makes a slider in the tuning panel take effect with no apply step.
    void apply(eng::Renderer& r, const DemoTuning& tuning)
    {
        r.setCameraFov(tuning.fovDeg);
        r.setPosition(mCamera, {0.0f, tuning.height, tuning.distance});
        r.setOrientation(mCamera, glm::angleAxis(glm::radians(tuning.pitchDeg),
                                                 glm::vec3(1.0f, 0.0f, 0.0f)));
        r.setOrientation(mOrbit, glm::angleAxis(mBaseYaw + mSpin + mManualYaw,
                                                glm::vec3(0.0f, 1.0f, 0.0f)));
    }

    void advance(float dt, float speed) { mSpin += dt * speed; }
    void nudgeYaw(float radians) { mManualYaw += radians; }
    void reset()
    {
        mSpin = 0.0f;
        mManualYaw = 0.0f;
    }

private:
    eng::NodeHandle mOrbit{};
    eng::NodeHandle mCamera{};
    float mBaseYaw = 0.0f;
    float mSpin = 0.0f;      // driven by the turntable clock
    float mManualYaw = 0.0f; // driven by dragging
};

// The engine's flagship feature: live-swappable render profiles. The list and
// its numbering come from the engine (eng::renderPresets()) rather than a local
// table -- a UI carrying its own array of names and trusting its index to match
// the engine's ids is the exact bug that header exists to kill.

// The demo as an eng::Application: scene build in onStart, the orbit/bob
// animation in onUpdate. No fixed step -- nothing here is simulated.
class DemoApp : public eng::Application {
public:
    eng::AppConfig configure(int, char**) override
    {
        eng::AppConfig cfg;
        cfg.mountSet = "demo";
        cfg.configPath = "config/demo.toml";
        cfg.fixedDt = 0.0f;
        // The console, the tuning panel and the placard all live in the imgui
        // frame. Without this the runner never opens one and every debug
        // surface the demo registers is dead code.
        cfg.imgui = true;
        cfg.loadingTitle = "PSX SHOWCASE";
        return cfg;
    }

    // The demo builds its scene in onStart, but the shader compiles behind it
    // are the same first-frame hitch every app pays; warming them here moves
    // the cost under the loading screen.
    void onLoad(eng::Engine& engine, eng::LoadPlan& plan) override
    {
        (void)engine;
        eng::addRenderWarmup(plan);
    }

    bool onStart(eng::Engine& engine) override
    {
        eng::Renderer& r = engine.renderer();

        r.setCameraClip(0.05f, 4000.0f);
        mRig.build(r, mTuning);

        // ---------------------------------------------------------- particles
        // --- Registered BEFORE the scene is built: spawnParticles() resolves
        // an effect by name at the call site and no-ops silently on a miss, so
        // a library loaded afterwards leaves every emitter in the showcase
        // dead. That is exactly what had happened -- assets/particles.toml was
        // authored for this scene and never loaded by anything.
        mParticles.load(r, eng::assets::resolve("config/particles.toml").string());

        // ------------------------------------------------------------- scene
        // ---
        if (!mScene.build(r))
            return false;

        // ------------------------------------------------------------ presets
        // --- Seed from PSX_RENDER_PRESET (already applied once by
        // Engine::init) so the label matches reality, then let Tab/Backspace
        // cycle it live. With no override the engine is on its default profile,
        // so start the cursor there rather than on whatever happens to be first
        // in the table.
        {
            const char* presetName = std::getenv("PSX_RENDER_PRESET");
            const int id = presetName ? eng::renderPresetFromName(presetName)
                                      : eng::kDefaultRenderPreset;
            const auto& presets = eng::renderPresets();
            for (size_t i = 0; i < presets.size(); ++i)
                if (presets[i].id == id)
                    mPresetIndex = i;
        }

        if (!mHud.initialise())
            eng::log::warn("Demo: UI font unavailable, placard disabled");

        // The tuning panel: the engine owns the tabs that tune the engine
        // (render profile, stylize shaders, step clock, materials), and the
        // demo adds the one tab only it can fill -- its camera rig and its own
        // scene.
        mTools.addPanel("Demo", [this, &engine] { drawDemoPanel(engine); });

        // The showcase's own surface shaders, on the engine's Portal/VFX
        // panels. The demo puts a portal and three liquid pools on screen and
        // had no way to touch either -- which is backwards for the app whose
        // job is to show what the renderer does. The values below mirror
        // assets/materials/kit.material and liquids.material, so a slider
        // starts where the material starts.
        {
            eng::PortalTuning portal;
            portal.dark = {0.076f, 0.000f, 0.535f, 1.0f};
            portal.mid = {0.858f, 0.000f, 1.000f, 1.0f};
            portal.bright = {0.540f, 0.000f, 1.000f, 1.0f};
            portal.core = {0.566f, 0.000f, 1.000f, 1.0f};
            portal.stepFps = 11.0f;
            portal.flowSpeed = 0.159f;
            portal.swirlSpeed = 0.521f;
            portal.twist = -0.355f;
            portal.arms = 1.0f;
            portal.texelSize = 0.0240f;
            portal.pixelGrid = 39.0f;
            portal.depthScale = 1.466f;
            portal.coreRadius = 0.227f;
            portal.coreBoost = 0.417f;
            portal.rimRadius = 0.600f;
            portal.rimWidth = 0.106f;
            portal.rimIntensity = 0.0f;
            portal.edgeFade = 0.0f;
            portal.dither = 0.863f;
            portal.edgeGlow = 1.882f;
            portal.edgeFlow = 3.0f;
            portal.brightness = 1.039f;
            mSurfaces.addPortal("Descent  (Demo/Portal/Down)", "Demo/Portal/Down",
                                portal, "slime_stylized.png");

            eng::LiquidTuning slime;
            slime.dark = {0.025f, 0.14f, 0.015f, 1.0f};
            slime.mid = {0.12f, 0.95f, 0.025f, 1.0f};
            slime.bright = {0.78f, 1.38f, 0.08f, 1.0f};
            slime.flowA = {-0.035f, 0.055f};
            slime.flowB = {0.045f, -0.020f};
            slime.emission = 0.20f;
            mSurfaces.addLiquid("Water  (Game/Vfx/Water)", "Game/Vfx/Water");
            mSurfaces.addLiquid("Toxic Slime  (Game/Vfx/ToxicSlime)",
                                "Game/Vfx/ToxicSlime", slime);
            mSurfaces.addLava("Lava  (Game/Vfx/Lava)", "Game/Vfx/Lava");

            // Bloom mirrored from the profile the demo boots in, so the first
            // touch of that slider does not jump the whole frame.
            const eng::RenderPresetBloom boot =
                eng::renderPresetBloom(engine.renderPreset());
            mSurfaces.setBloom({boot.enabled, boot.threshold, boot.intensity});
            mSurfaces.install(mTools);
        }
        // The particle panel is the same deal: the showcase is the fastest
        // place to try an effect against the profile it will ship under, and
        // the panel itself is engine tooling over eng::ParticleLibrary.
        mParticlePanel.install(mTools, eng::PanelGroup::Content);
        mPerf.setVisible(false);

        // Shared engine console: the demo's own switches, reachable by name.
        // Same window the game and the editor open, so nothing here is
        // demo-specific beyond the command list.
        mConsole.captureEngineLog();
        mConsole.registerCommand("quit", "close the demo",
                                 [&engine](const eng::DebugConsole::Args&) {
                                     engine.requestClose();
                                 });
        mConsole.registerCommand(
            "pause", "freeze/unfreeze the turntable",
            [this](const eng::DebugConsole::Args&) { mPaused = !mPaused; });
        mConsole.registerCommand(
            "restart", "rewind the animation clock",
            [this](const eng::DebugConsole::Args&) { resetView(); });
        mConsole.registerCommand(
            "tune", "toggle the tuning panel",
            [this](const eng::DebugConsole::Args&) { mTools.toggle(); });
        mConsole.registerCommand(
            "perf", "toggle the performance HUD",
            [this](const eng::DebugConsole::Args&) { mPerf.toggle(); });
        mConsole.registerCommand(
            "hud", "toggle the on-screen placard",
            [this](const eng::DebugConsole::Args&) { mHud.toggle(); });
        // Camera values as console bindings as well as sliders: the panel is
        // for hunting a value, the console for setting one you already know.
        mConsole.bindFloat("cam.distance", &mTuning.distance, kMinDistance,
                           kMaxDistance, "camera dolly, metres");
        mConsole.bindFloat("cam.height", &mTuning.height, 0.0f, 20.0f,
                           "camera height above the dais");
        mConsole.bindFloat("cam.pitch", &mTuning.pitchDeg, kMinPitch, kMaxPitch,
                           "camera pitch, degrees");
        mConsole.bindFloat("cam.fov", &mTuning.fovDeg, kMinFov, kMaxFov,
                           "vertical field of view");
        mConsole.bindFloat("demo.spin", &mTuning.orbitSpeed, 0.0f, 3.0f,
                           "turntable rate multiplier");
        mConsole.bindFloat("demo.anim", &mTuning.animSpeed, 0.0f, 3.0f,
                           "station animation rate multiplier");
        mConsole.registerCommand(
            "r.preset", "list render profiles, or switch to one by name",
            [this, &r](const eng::DebugConsole::Args& a) {
                const auto& presets = eng::renderPresets();
                if (a.size() > 1) {
                    const int id = eng::renderPresetFromName(a[1].c_str());
                    for (size_t i = 0; i < presets.size(); ++i)
                        if (presets[i].id == id)
                            mPresetIndex = i;
                    eng::applyRenderPreset(r, id);
                    return;
                }
                for (const auto& p : presets)
                    mConsole.print(eng::log::Level::Info, "render", p.name);
            },
            [](const eng::DebugConsole::Args&) {
                std::vector<std::string> out;
                for (const auto& p : eng::renderPresets())
                    out.emplace_back(p.name);
                return out;
            });

        // Set dressing belongs to ShowcaseScene::buildDressing, which places it
        // in the gaps between the stations. A second ring used to be built here
        // as well, at radius 3.3-4.3 -- inside the dais and across the station
        // ring -- so every angle had a barrel in front of whatever the camera
        // was meant to be looking at. One scene, one dressing pass.

        mRig.apply(r, mTuning);
        return true;
    }

    void onFrameBegin(const eng::FrameContext& f) override
    {
        eng::Renderer& r = f.engine.renderer();
        eng::Input& in = f.engine.input();
        mStats.beginFrame();

        // While the console has the keyboard, keys are text: without this gate,
        // typing "restart" in it rewinds the scene four times over.
        const bool typing = f.engine.imguiWantsKeyboard();
        if (in.wasPressed("dev_console"))
            mConsole.toggle();
        if (!typing) {
            if (in.wasPressed("quit"))
                f.engine.requestClose();
            if (in.wasPressed("pause"))
                mPaused = !mPaused;
            if (in.wasPressed("restart"))
                resetView();
            if (in.wasPressed("tuning"))
                mTools.toggle();
            if (in.wasPressed("perf"))
                mPerf.toggle();
            if (in.wasPressed("hud"))
                mHud.toggle();
            if (in.wasPressed("zoom_in"))
                dolly(-1.0f);
            if (in.wasPressed("zoom_out"))
                dolly(1.0f);
            if (in.wasPressed("spin_up"))
                mTuning.orbitSpeed = std::min(3.0f, mTuning.orbitSpeed + 0.25f);
            if (in.wasPressed("spin_down"))
                mTuning.orbitSpeed = std::max(0.0f, mTuning.orbitSpeed - 0.25f);
            // Held, not tapped: the lens is a value you sweep to find, unlike
            // the dolly, where one notch is a step you can count.
            if (in.isDown("fov_up"))
                widenFov(kFovRate * f.dt);
            if (in.isDown("fov_down"))
                widenFov(-kFovRate * f.dt);
            if (in.wasPressed("preset_next") || in.wasPressed("preset_prev")) {
                const auto& presets = eng::renderPresets();
                const size_t n = presets.size();
                mPresetIndex = in.wasPressed("preset_next")
                                   ? (mPresetIndex + 1) % n
                                   : (mPresetIndex + n - 1) % n;
                eng::applyRenderPreset(r, presets[mPresetIndex].id);
            }
        }

        // Mouse: wheel dollies, drag orbits. Both gated on imgui, so scrolling
        // the console's log or dragging a slider does not also fly the camera.
        if (!f.engine.imguiWantsMouse()) {
            if (const float wheel = in.wheelDelta(); wheel != 0.0f)
                dolly(-wheel);
            if (in.isMouseDown(eng::MouseButton::Left) ||
                in.isMouseDown(eng::MouseButton::Right)) {
                const glm::vec2 drag = in.mouseDelta();
                mRig.nudgeYaw(glm::radians(-drag.x * mTuning.dragSpeed));
                mTuning.pitchDeg =
                    std::clamp(mTuning.pitchDeg - drag.y * mTuning.dragSpeed,
                               kMinPitch, kMaxPitch);
            }
        }
    }

    void onGui(const eng::FrameContext& f) override
    {
        const eng::FrameStatsView view = mStats.view();
        eng::DebugTools::Deps deps;
        deps.renderer = &f.engine.renderer();
        deps.frame = &view;
        deps.steps = &f.engine.stepClock();
        deps.renderPresetId = f.engine.renderPreset();
        // Per-frame, like every other panel dependency: the surface panels edit
        // materials through the renderer and must not outlive one.
        mSurfaces.setRenderer(&f.engine.renderer());
        mParticlePanel.setSources(&f.engine.renderer(), &mParticles);
        mParticlePanel.update(f.dt);
        mTools.draw(deps);
        mConsole.draw();
        mPerf.draw(&view, &f.engine.renderer());

        DemoHud::Status status;
        status.preset = eng::renderPresets()[mPresetIndex].name;
        status.presetIndex = int(mPresetIndex);
        status.presetCount = int(eng::renderPresets().size());
        status.distance = mTuning.distance;
        // Reported the way the wheel moves it: fully zoomed in reads as a full
        // bar.
        status.zoom =
            (kMaxDistance - mTuning.distance) / (kMaxDistance - kMinDistance);
        status.orbitSpeed = mTuning.orbitSpeed;
        status.fovDeg = mTuning.fovDeg;
        status.fov = (mTuning.fovDeg - kMinFov) / (kMaxFov - kMinFov);
        status.paused = mPaused;
        mHud.draw(status, f.dt);
    }

    void onUpdate(const eng::FrameContext& f) override
    {
        eng::Renderer& r = f.engine.renderer();
        {
            const auto scope = mStats.time(kScenePhase);
            if (!mPaused) {
                mAnimTime += f.dt * mTuning.animSpeed;
                mRig.advance(f.dt, mTuning.orbitSpeed);
            }
            mRig.apply(r, mTuning);
            mScene.update(r, mAnimTime);
        }
        mStats.endFrame(f.dt * 1000.0f);
    }

    void onFrameRendered(float renderMs) override
    {
        mStats.setPhase(kRenderPhase, renderMs);
    }

private:
    static constexpr int kScenePhase = 0;
    static constexpr int kRenderPhase = 1;

    // Proportional dolly: a notch moves a fraction of the current distance, so
    // the zoom feels the same close in as far out. A fixed step either crawls
    // at 30 m or slams into the dais at 4.
    void dolly(float notches)
    {
        const float step = 1.0f + mTuning.zoomSpeed;
        const float scale = notches >= 0.0f ? std::pow(step, notches)
                                            : 1.0f / std::pow(step, -notches);
        mTuning.distance =
            std::clamp(mTuning.distance * scale, kMinDistance, kMaxDistance);
    }

    void widenFov(float degrees)
    {
        mTuning.fovDeg = std::clamp(mTuning.fovDeg + degrees, kMinFov, kMaxFov);
    }

    void resetView()
    {
        mAnimTime = 0.0f;
        mTuning = DemoTuning{};
        mRig.reset();
    }

    // The demo's own tab in the engine tuning panel. Everything here writes
    // straight into mTuning or the scene; the rig is pushed to the renderer
    // every frame, so there is nothing to apply.
    void drawDemoPanel(eng::Engine& engine)
    {
        eng::Renderer& r = engine.renderer();
        ImGui::TextUnformatted("Camera rig");
        ImGui::SliderFloat("Distance", &mTuning.distance, kMinDistance,
                           kMaxDistance, "%.2f m");
        ImGui::SliderFloat("Height", &mTuning.height, 0.0f, 20.0f, "%.2f m");
        ImGui::SliderFloat("Pitch", &mTuning.pitchDeg, kMinPitch, kMaxPitch,
                           "%.1f deg");
        ImGui::SliderFloat("FOV", &mTuning.fovDeg, kMinFov, kMaxFov,
                           "%.1f deg");
        ImGui::SliderFloat("Zoom step", &mTuning.zoomSpeed, 0.02f, 0.40f,
                           "%.2f / notch");
        ImGui::SliderFloat("Drag", &mTuning.dragSpeed, 0.05f, 1.0f,
                           "%.2f deg/px");

        ImGui::SeparatorText("Motion");
        ImGui::Checkbox("Paused", &mPaused);
        ImGui::SliderFloat("Turntable", &mTuning.orbitSpeed, 0.0f, 3.0f,
                           "%.2fx");
        ImGui::SliderFloat("Animation", &mTuning.animSpeed, 0.0f, 3.0f,
                           "%.2fx");
        if (ImGui::Button("Reset view"))
            resetView();

        ImGui::SeparatorText("Surfaces");
        // The dungeon's materials, live on the demo's stone. Same profiles the
        // game's kit uses -- this is what they look like under each render
        // preset, which is a question the demo exists to answer.
        const std::span<const char* const> choices = surfaceMaterialChoices();
        for (int i = 0; i < int(SurfaceSlot::Count); ++i) {
            const SurfaceSlot slot = SurfaceSlot(i);
            const std::string& current = mScene.surfaceMaterial(slot);
            if (!ImGui::BeginCombo(surfaceSlotName(slot), current.c_str()))
                continue;
            for (const char* name : choices) {
                if (ImGui::Selectable(name, current == name))
                    mScene.setSurfaceMaterial(r, slot, name);
            }
            ImGui::EndCombo();
        }

        ImGui::SeparatorText("Particles");
        if (ImGui::SliderFloat("Quality", &mTuning.particleQuality, 0.0f, 1.0f,
                               "%.2f"))
            r.setParticleQuality(mTuning.particleQuality);

        ImGui::SeparatorText("Presets");
        const auto& presets = eng::renderPresets();
        if (ImGui::BeginCombo("Profile", presets[mPresetIndex].name)) {
            for (size_t i = 0; i < presets.size(); ++i) {
                if (ImGui::Selectable(presets[i].name, i == mPresetIndex)) {
                    mPresetIndex = i;
                    eng::applyRenderPreset(r, presets[i].id);
                }
            }
            ImGui::EndCombo();
        }
    }

    OrbitRig mRig;
    ShowcaseScene mScene;
    DemoTuning mTuning;
    eng::ParticleLibrary mParticles;
    eng::DebugConsole mConsole;
    eng::DebugTools mTools;
    eng::SurfacePanels mSurfaces;
    eng::ParticlePanel mParticlePanel;
    eng::PerfOverlay mPerf;
    eng::FrameStats mStats{{"Scene", "Render"}};
    DemoHud mHud;
    size_t mPresetIndex = 0;
    bool mPaused = false;
    float mAnimTime = 0.0f;
};

} // namespace

int main(int argc, char** argv)
{
    DemoApp app;
    return eng::runApplication(app, argc, argv);
}
