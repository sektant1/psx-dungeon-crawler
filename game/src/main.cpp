// dungeon-crawler: FPS walk through a procedurally generated PSX dungeon
// (DungeonGen -> DungeonMap), with the shared demo scene (crystals, chest,
// sparkles, light shaft) sitting in the generated level's anchor room.
// A whole level is built by buildLevel() into a Level bundle; level
// transitions (portals) clear the scene and rebuild.

#include "DungeonGen.h"
#include "DungeonMap.h"
#include "FpsController.h"
#include "LevelEditor.h"
#include "LevelResource.h"
#include "LiveLevel.h"
#include "MapPlay.h"
#include "Projectiles.h"
#include "Spells.h"
#include "CombatConfig.h"
#include "SceneFactory.h"
#include "Melee.h"
#include "ParticleLibrary.h"
#include "CombatSystem.h"
#include "Dummy.h"
#include "GameContext.h"
#include "GameDiagnostics.h"
#include "GameScene.h"
#include "PropSystem.h"
#include "Targeting.h"
#include "ViewModel.h"

#include <eng/ecs/Components.h>

#include <DemoScene.h>

#include <imgui.h>

#include <eng/Content.h>
#include <eng/Engine.h>
#include <eng/LoadingScreen.h>
#include <eng/Log.h>
#include <eng/Physics.h>
#include <eng/Profiler.h>

#include <chrono>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    // Dev self-test: PSX_GEN_DUMP=<seed> prints a generated grid and exits,
    // no window/Ogre. Eyeball connectivity + room shapes across seeds.
    if (const char* dump = std::getenv("PSX_GEN_DUMP")) {
        const auto grid = gen::generate(uint32_t(std::strtoul(dump, nullptr, 10)));
        for (const std::string& row : grid.rows())
            std::printf("%s\n", row.c_str());
        return 0;
    }

    eng::Engine engine;
    const std::string assets = APP_ASSET_DIR;
    if (!engine.init(assets + "/game.toml", assets))
        return 1;
    eng::Renderer& r = engine.renderer();
    eng::LoadingScreen loading(engine);
    loading.begin("Loading dungeon");
    loading.step("Preparing renderer", 0.08f);
    loading.present();

    r.setCameraFov(70.0f);
    // With the current 0.05 exponential fog, a 90 m far plane retains only
    // ~1.1% scene colour. The cut is hidden without visibly popping long
    // corridors; the 0.08 m near plane also improves depth precision.
    r.setCameraClip(0.08f, 90.0f);

    uint32_t baseSeed = 1;
    if (const char* s = std::getenv("PSX_GEN_SEED"))
        baseSeed = uint32_t(std::strtoul(s, nullptr, 10));

    // Persistent level stack: seeds[d] is depth d's seed (stored so revisits
    // reuse it -> identical layout). Live scenes are never cached; one level
    // is live at a time and rebuilt on every transition.
    std::vector<uint32_t> seeds{baseSeed};
    int depth = 0;
    const float speed = float(engine.config().getNumber("player.speed", 3.0));
    const float sens =
        float(engine.config().getNumber("player.mouse_sensitivity", 0.002));

    bool showColliders = std::getenv("PSX_SHOW_COLLIDERS") != nullptr;

    eng::Physics physics;
    physics.init();

    // `game <file.map>`: play an authored editor scene instead of the
    // procedural dungeon. Handled here (engine + physics + assets ready, before
    // the procedural build) and exits with the play loop's return code.
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.size() > 4 && arg.substr(arg.size() - 4) == ".map") {
            const int rc = game::playMap(engine, physics, assets, arg);
            engine.shutdown();
            return rc;
        }
    }

    loading.step("Preparing physics", 0.16f);
    loading.present();

    game::GameContext ctx{r, physics, engine.input(), assets};

    // Attack subsystems (arrows/spells/melee) + data-driven tunables from
    // [combat.*] in game.toml, live-editable in the "Attacks" debug window.
    // init() (config load + procedural mesh build) runs after the level is
    // built, matching the original ordering.
    game::CombatSystem combat;

    ParticleLibrary particles;
    particles.load(r, assets + "/particles.toml");
    loading.step("Loading combat data", 0.26f);
    loading.present();

    // Dynamic lobby props (crates + barrels): spawned once, synced each frame,
    // torn down before any rebuild. Known limitation: not re-spawned on level
    // transition. Shares the world through GameContext.
    game::PropSystem props;
    Dummy dummy;
    bool dummyAlive = false;
    combat.melee().setHitCallback([&dummy, &dummyAlive, &physics](
                             eng::BodyHandle body, glm::vec3 point,
                             glm::vec3 normal) {
        if (dummyAlive && dummy.alive() && body == dummy.body())
            dummy.kill(physics, -normal * 8.0f + glm::vec3(0.0f, 3.0f, 0.0f),
                       point);
    });

    LiveLevel level;
    FpsController player;
    ViewModel viewModel;
    ViewModel staffModel;
    ViewModel torchModel;
    // Active weapon, cycled by the swap_weapon bind. Drives which viewmodel is
    // shown and which action input is accepted:
    //   Sword -> melee (LMB), Staff -> spells (cast keys), Torch -> light + bash.
    enum Weapon { WSword = 0, WStaff = 1, WTorch = 2, WeaponCount = 3 };
    int weapon = WSword;
    // Show only the active viewmodel.
    const auto applyWeaponVis = [&](eng::Renderer& rr) {
        viewModel.setVisible(rr, weapon == WSword);
        staffModel.setVisible(rr, weapon == WStaff);
        torchModel.setVisible(rr, weapon == WTorch);
    };
    // Re-attach the carried light and the three viewmodels to the player's
    // fresh head node after a respawn/rebuild (the old head node is destroyed
    // by clearScene), then show only the active weapon. Called from every
    // path that rebuilds the level under the player.
    const auto attachPlayerLoadout = [&] {
        eng::LightDesc carry;
        carry.colour = glm::vec3(std::pow(1.0f, 2.2f), std::pow(0.80f, 2.2f),
                                 std::pow(0.58f, 2.2f)) * 0.95f;
        carry.range = 7.0f;
        r.attachLight(player.headNode(), carry);
        viewModel.init(r, player.headNode(), assets + "/meshes/props");
        staffModel.initStaff(r, player.headNode(),
                             assets + "/meshes/crystal_spire1.obj");
        torchModel.initTorch(r, player.headNode());
        applyWeaponVis(r);
    };
    const bool portalPreviewMode =
        std::getenv("PSX_SHOWCASE_PORTAL") != nullptr;

    const auto teardownDummy = [&] {
        if (!dummyAlive)
            return;
        dummy.clear(physics, r);
        dummyAlive = false;
    };

    // New engine content spine drives the lobby load (dual-run; same rows, same
    // look). Persist so re-entering depth 0 reuses the cached resource.
    eng::Content levelContent;

    // Wipe the scene, build the level at `depth`, and (re)spawn the player.
    // atExit spawns at the down-portal (arrived by ascending); else at entry.
    const auto enterLevel = [&](bool atExit) {
        // Destroy dynamic prop + dummy bodies before clearScene wipes their nodes.
        props.teardown(physics);
        teardownDummy();
        bool loaded = false;
        if (depth == 0) {
            if (LevelResource* lobby =
                    levelContent.load<LevelResource>("lobby", assets + "/lobby.toml")) {
                loaded = level.rebuildLayout(r, physics, assets,
                                             lobby->layout(), depth);
            }
            // load() already logged on failure; leaves `loaded` false.
        } else {
            loaded = level.rebuild(r, physics, assets, seeds[size_t(depth)],
                                   depth);
        }
        if (!loaded) {
            eng::log::error("Level %d failed to load", depth);
            return;
        }
        const bool portalPreview = depth == 0 && portalPreviewMode;
        const float portalYaw = glm::radians(level.dungeon().exitYawDegrees());
        const glm::vec3 portalFront(std::sin(portalYaw), 0.0f,
                                    std::cos(portalYaw));
        const glm::vec3 p = portalPreview
            ? level.exitPosition() + portalFront * 4.0f
            : (atExit ? level.exitPosition() : level.spawnPosition());
        player.init(r, physics, p, speed, sens, glm::vec3(-1000.0f), glm::vec3(1000.0f));
        if (portalPreview) {
            player.setViewAngles(portalYaw);
            player.present(r);
        }
        attachPlayerLoadout();
        engine.input().setMouseGrab(!portalPreview);
    };
    loading.step("Building level", 0.42f);
    loading.present();
    enterLevel(false); // depth 0, spawn at entry

    // Initialise combat (loads [combat.*], builds procedural projectile/spell
    // meshes) and register the contact seam so arrows stick and bolts despawn.
    combat.init(ctx, assets + "/game.toml");
    loading.step("Spawning systems", 0.72f);
    loading.present();
    physics.setContactCallback([&combat, &ctx, &physics, &dummy, &dummyAlive](const eng::HitEvent& e) {
        combat.onContact(ctx, e);
        if (dummyAlive && dummy.alive() &&
            (e.self == dummy.body() || e.other == dummy.body())) {
            // Arrow hit the dummy: knock it forward and upward
            dummy.kill(physics, glm::vec3(0.0f, 3.0f, 6.0f), e.point);
        }
    });

    props.spawnLobby(ctx);

    // Spawn a topple dummy alongside the lobby props (entry hall area).
    // Placed 3 m further toward the anchor room from the crate cluster.
    dummy.init(physics, r, glm::vec3(3.0f, 0.0f, 15.0f));
    dummyAlive = true;
    loading.step("Ready", 1.0f);
    loading.present();
    loading.finish();

    engine.debugUi().addPanel("Player", [&player] {
        ImGui::SliderFloat("move speed", &player.speed(), 0.5f, 15.0f);
        ImGui::SliderFloat("mouse sensitivity", &player.sensitivity(), 0.0005f,
                           0.01f, "%.4f");
        float baseFov = player.baseFov();
        if (ImGui::SliderFloat("locomotion base FOV", &baseFov, 30.0f, 120.0f,
                               "%.0f"))
            player.setBaseFov(baseFov);
        ImGui::Text("stance: %s", player.crouched() ? "crouched" : "standing");
        ImGui::Text("sprint: %s  stamina: %3.0f%%",
                    player.sprinting() ? "active" : "ready",
                    player.sprintStamina() * 100.0f);
        ImGui::Text("movement: %s", player.sliding() ? "sliding"
                                                       : (player.grounded() ? "grounded"
                                                                            : "airborne"));
    });
    engine.debugUi().addPanel("Attacks", [&combat, &engine] {
        combat.config().drawDebugUi(engine.input());
    });
    engine.debugUi().addPanel("Particles", [&particles, &r] {
        static float particleQuality = 1.0f;
        if (ImGui::SliderFloat("global quality", &particleQuality, 0.25f, 1.0f))
            r.setParticleQuality(particleQuality);
        ImGui::TextDisabled("lower = fewer particles in heavy scenes");
        ImGui::Separator();
        auto& descs = particles.descs();
        for (size_t i = 0; i < descs.size(); ++i) {
            eng::ParticleEffectDesc& d = descs[i];
            if (!ImGui::TreeNode(d.name.c_str())) continue;
            ImGui::SliderInt("quota", &d.quota, 1, 128);
            if (!d.emitters.empty())
                ImGui::SliderFloat("emission", &d.emitters[0].emissionRate, 0.0f, 200.0f);
            ImGui::SliderFloat("base w", &d.baseWidth, 0.02f, 0.6f);
            ImGui::SliderFloat("base h", &d.baseHeight, 0.02f, 0.6f);
            for (size_t s = 0; s < d.colourRamp.size(); ++s) {
                ImGui::PushID(int(s));
                ImGui::ColorEdit4("ramp stop", &d.colourRamp[s].rgba.x);
                ImGui::PopID();
            }
            ImGui::SliderFloat("scale jitter", &d.scaleJitter, 0.0f, 0.5f);
            if (ImGui::Button("apply"))
                particles.reregister(r, i);
            ImGui::TreePop();
        }
    });
    engine.debugUi().addPanel("Physics", [&physics, &player, &showColliders] {
        ImGui::Text("active bodies: %d", physics.activeBodyCount());
        float g = physics.gravityY();
        if (ImGui::SliderFloat("gravity Y", &g, -40.0f, 0.0f, "%.1f"))
            physics.setGravity(g);
        ImGui::Checkbox("show colliders", &showColliders);
        ImGui::Separator();
        ImGui::Text("grounded: %s", player.grounded() ? "yes" : "no");
        const glm::vec3 n = player.groundNormal();
        ImGui::Text("ground normal: %.2f %.2f %.2f", n.x, n.y, n.z);
        ImGui::Text("horizontal speed: %.2f m/s", player.horizontalSpeed());
        ImGui::Text("stance: %s", player.crouched() ? "crouched"
                    : (player.sliding() ? "sliding" : "standing"));
    });
    // Self-windowing debug view: projects the generated grid into its own
    // ImGui window, so it registers as a window (not an inline panel).
    engine.debugUi().addWindow([&level, &player] {
        game::drawDungeonMap(level.dungeon(), player.eyePosition());
    });
    LevelEditor editor(level.dungeon().debugLayoutRows(),
                       assets + "/editor_level.toml");
    engine.debugUi().addWindow([&level, &player, &editor, &r, &physics,
                                &assets, &depth, speed, sens, &engine,
                                &attachPlayerLoadout] {
        if (!editor.draw(level.dungeon(), player.eyePosition()))
            return;
        const gen::Layout layout = editor.takeLayout();
        if (!level.rebuildLayout(r, physics, assets, layout, depth))
            return;
        player.init(r, physics, level.spawnPosition(), speed, sens,
                    glm::vec3(-1000.0f), glm::vec3(1000.0f));
        attachPlayerLoadout();
        engine.input().setMouseGrab(false);
    });

    // Standalone Diagnostics window (F1), fed by per-phase timers in the loop.
    using game::ProfHud;
    ProfHud prof;
    engine.debugUi().addWindow([&prof, &physics] { game::drawDiagnostics(prof, physics); });

    // ---------------------------------------------------------------- loop ---
    constexpr float kFixedDt = 1.0f / 60.0f;
    float accumulator = 0.0f;
    float animTime = 0.0f;
    std::vector<GameplayTarget> targets;
    targets.reserve(64);
    while (!engine.shouldClose()) {
        const float dt = engine.tick();
        eng::Input& in = engine.input();
        using clk = std::chrono::steady_clock;
        auto phaseMs = [](clk::time_point t0) {
            return std::chrono::duration<float, std::milli>(clk::now() - t0).count();
        };
        // First Esc releases the mouse, second quits; click re-grabs.
        // Suspended while the debug panel is open (F1 owns grab then).
        if (!engine.debugUi().visible()) {
            if (in.wasPressed("quit")) {
                if (in.mouseGrabbed())
                    in.setMouseGrab(false);
                else
                    engine.requestClose();
            }
            if (!in.mouseGrabbed() && in.wasMouseClicked())
                in.setMouseGrab(true);
        }

        // Fixed-step physics. Cap at 5 steps to prevent spiral of death.
        auto tPhysics = clk::now();
        accumulator += dt;
        int guard = 0;
        while (accumulator >= kFixedDt && guard++ < 5) {
            physics.update(kFixedDt);
            combat.fixedStep(ctx, player.eyePosition(), player.forward(), kFixedDt);
            accumulator -= kFixedDt;
        }
        physics.setInterpolationAlpha(accumulator / kFixedDt);

        // Sync dynamic prop render nodes from the interpolated physics transform.
        props.sync(ctx);
        combat.syncRender(ctx);
        if (dummyAlive)
            dummy.syncRender(physics, r);

        prof.ms[ProfHud::Physics] = phaseMs(tPhysics);

        auto tWorld = clk::now();
        animTime += dt;
        level.update(r, animTime);
        level.updateVisibility(r, player.eyePosition());
        prof.ms[ProfHud::World] = phaseMs(tWorld);

        auto tPlayer = clk::now();
        if (!portalPreviewMode)
            player.update(in, r, dt);
        prof.ms[ProfHud::Player] = phaseMs(tPlayer);

        targets.clear();
        level.appendTargets(targets, depth);
        const GameplayTarget* target = aimedTarget(
            targets, player.eyePosition(), player.forward());
        if (!target) {
            engine.debugUi().setHudPrompt({});
        } else if (target->kind == TargetKind::Torch) {
            engine.debugUi().setHudPrompt(level.torchIsLit(target->id)
                                              ? "Press [E] to snuff the torch"
                                              : "Press [E] to light the torch");
            if (in.wasPressed("interact"))
                level.toggleTorch(r, target->id);
        } else if (target->kind == TargetKind::PortalDown) {
            engine.debugUi().setHudPrompt("Press [E] to descend");
            if (in.wasPressed("interact")) {
                if (depth + 1 == int(seeds.size()))
                    seeds.push_back(baseSeed +
                                    uint32_t(depth + 1) * 0x9E3779B9u);
                ++depth;
                enterLevel(false);
            }
        } else {
            engine.debugUi().setHudPrompt("Press [E] to ascend");
            if (in.wasPressed("interact")) {
                --depth;
                enterLevel(true);
            }
        }

        // Projectile firing — only when mouse is grabbed (not in debug UI).
        bool swordAttack = false;
        bool didCast = false;
        if (in.mouseGrabbed()) {
            if (in.wasPressed("swap_weapon")) {
                weapon = (weapon + 1) % WeaponCount;
                applyWeaponVis(r);
            }
            if (in.wasPressed("fire_arrow"))
                combat.fireArrow(ctx, player.eyePosition(), player.forward());
            // Staff casts only when the staff is equipped.
            if (weapon == WStaff && in.wasPressed("cast_spell")) {
                combat.castFireball(ctx, player.eyePosition(), player.forward());
                didCast = true;
            }
            if (weapon == WStaff && in.wasPressed("cast_beam")) {
                combat.castBeam(ctx, player.eyePosition(), player.forward());
                didCast = true;
            }
            // Melee swing for the sword and the torch (a light club).
            if ((weapon == WSword || weapon == WTorch) && in.wasMouseClicked()) {
                combat.startSwing();
                swordAttack = true;
            }
        }
        auto tWeapons = clk::now();
        viewModel.update(r, dt, weapon == WSword && swordAttack,
                         in.mouseGrabbed() &&
                             in.isMouseDown(eng::MouseButton::Right));
        staffModel.update(r, dt, didCast, false);
        torchModel.update(r, dt, weapon == WTorch && swordAttack, false);
        prof.ms[ProfHud::Weapons] = phaseMs(tWeapons);

        // Physics collider wireframe overlay — neon-pink collision view.
        if (showColliders) {
            // Hot neon pink (~#FF14C8), pushed slightly >1 so it stays vivid
            // through tonemapping/dither and reads as an emissive outline.
            const glm::vec3 kNeonPink(1.30f, 0.08f, 0.78f);
            static std::vector<eng::Physics::DebugLine> pl;
            pl.clear();
            physics.debugDraw(pl);
            static std::vector<eng::Renderer::DebugLine> dl;
            dl.clear();
            for (const auto& l : pl)
                dl.push_back({l.a, l.b, kNeonPink});
            r.setDebugLines(dl);
        } else {
            r.setDebugLines({});
        }

        prof.pushFrame(dt * 1000.0f);
        auto tRender = clk::now();
        engine.renderFrame(dt);
        prof.ms[ProfHud::Render] = phaseMs(tRender);
    }
    // Remove dynamic prop bodies before shutdown (nodes are owned by Ogre/scene).
    props.teardown(physics);
    teardownDummy();
    level.clearPhysics();
    combat.clear(ctx);
    physics.shutdown();
    engine.shutdown();
    return 0;
}
