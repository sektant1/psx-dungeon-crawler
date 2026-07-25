// dungeon-crawler: FPS walk through a procedurally generated PSX dungeon
// (DungeonGen -> DungeonMap), with the shared demo scene (crystals, chest,
// sparkles, light shaft) sitting in the generated level's anchor room.
// A whole level is built by buildLevel() into a Level bundle; level
// transitions (portals) clear the scene and rebuild.

#include "DungeonGen.h"
#include "DungeonMap.h"
#include "LevelResource.h"
#include "LiveLevel.h"
#include "MapPlay.h"
#include "SceneFactory.h"
#include "ParticleLibrary.h"
#include "CombatSystem.h"
#include "DebugOverlay.h"
#include "Dummy.h"
#include "GameContext.h"
#include "GameDiagnostics.h"
#include "GameScene.h"
#include "InteractionSystem.h"
#include "PlayerSystem.h"
#include "PropSystem.h"
#include "combat/CombatComponents.h"
#include "combat/ActionStateSystem.h"
#include "combat/DefenseSystem.h"
#include "combat/FeelComponents.h"
#include "combat/PoiseSystem.h"

#include <eng/ecs/Components.h>

#include <DemoScene.h>


#include <eng/Content.h>
#include <eng/Engine.h>
#include <eng/Log.h>
#include <eng/Physics.h>
#include <eng/Profiler.h>

#include <chrono>

#include <glm/gtc/quaternion.hpp>

#include <imgui.h> // collider gizmos drawn via the imgui screen-space draw list

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

    game::ColliderDebug colliderDbg;
    colliderDbg.enabled = std::getenv("PSX_SHOW_COLLIDERS") != nullptr;

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


    game::GameContext ctx{r, physics, engine.input(), assets};

    // Attack subsystems (arrows/spells/melee) + data-driven tunables from
    // [combat.*] in game.toml, live-editable in the "Attacks" debug window.
    // init() (config load + procedural mesh build) runs after the level is
    // built, matching the original ordering.
    game::CombatSystem combat;

    ParticleLibrary particles;
    particles.load(r, assets + "/particles.toml");

    // Dynamic lobby props (crates + barrels): spawned once, synced each frame,
    // torn down before any rebuild. Known limitation: not re-spawned on level
    // transition. Shares the world through GameContext.
    game::PropSystem props;
    Dummy dummy;
    bool dummyAlive = false;
    // Combat-model entities (assigned once the dummy + player exist, below). The
    // player is a bodiless combatant used only as a damage source (faction gate).
    entt::entity playerEntity = entt::null;
    entt::entity dummyEntity = entt::null;

    LiveLevel level;
    // Player controller + first-person viewmodels + active weapon selection.
    // Weapon cycles on the swap_weapon bind and drives which viewmodel shows and
    // which attack input CombatSystem accepts (Sword->melee, Staff->spells,
    // Torch->light+bash).
    game::PlayerSystem playerSys;
    playerSys.setTuning(speed, sens);
    FpsController& player = playerSys.controller();
    const bool portalPreviewMode =
        std::getenv("PSX_SHOWCASE_PORTAL") != nullptr;

    const auto teardownDummy = [&] {
        if (!dummyAlive)
            return;
        combat.director().removeCombatant(dummy.body()); // drop body->entity link
        dummyEntity = entt::null;
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
        playerSys.spawnAt(ctx, p);
        if (portalPreview) {
            player.setViewAngles(portalYaw);
            player.present(r);
        }
        playerSys.attachLoadout(ctx);
        engine.input().setMouseGrab(!portalPreview);
    };
    enterLevel(false); // depth 0, spawn at entry

    // Initialise combat (loads [combat.*], builds procedural projectile/spell
    // meshes) and register the contact seam so arrows stick and bolts despawn.
    combat.init(ctx, assets + "/game.toml");
    physics.setContactCallback([&combat, &ctx, &physics, &dummy, &dummyAlive,
                                &playerEntity](const eng::HitEvent& e) {
        combat.onContact(ctx, e);
        if (dummyAlive && dummy.alive() &&
            (e.self == dummy.body() || e.other == dummy.body())) {
            // Projectile hit the dummy: route arrow damage through the combat
            // model (resistances, crit, death). Knockback pushes it forward/up.
            combat.director().hitBody(physics, dummy.body(), "arrow",
                                      playerEntity,
                                      glm::vec3(0.0f, 0.4f, 1.0f), e.point);
        }
    });

    props.spawnLobby(ctx);

    // Spawn a topple dummy alongside the lobby props (entry hall area).
    // Placed 3 m further toward the anchor room from the crate cluster.
    dummy.init(physics, r, glm::vec3(3.0f, 0.0f, 15.0f));
    dummyAlive = true;

    // ---- combat model wiring -------------------------------------------------
    // The player is a bodiless combatant used as a damage source (faction gate).
    // The dummy is an armored, flammable Enemy: resists physical, takes extra
    // fire. Melee/arrow hits and the "Combat" debug panel drive real damage;
    // the death callback triggers the existing topple.
    {
        game::Health playerHp;
        playerHp.current = playerHp.max = 100.0f;
        playerEntity = combat.director().addCombatant(
            eng::BodyHandle{}, playerHp, game::Resistances{},
            game::Faction::Player);

        game::Health dummyHp;
        dummyHp.current = dummyHp.max = 60.0f;
        game::Resistances dummyResist{};
        dummyResist[game::DamageType::Physical] = 0.35f; // plated
        dummyResist[game::DamageType::Fire] = -0.5f;     // flammable
        dummyEntity = combat.director().addCombatant(
            dummy.body(), dummyHp, dummyResist, game::Faction::Enemy);

        // Feel layer: give the player and the dummy a stamina/poise/action-state
        // profile. The player uses defaults; the dummy is a goblin-like target
        // with low poise so one heavy hit or a kick staggers it.
        {
            entt::registry& creg = combat.director().registry();
            creg.emplace<game::Stamina>(playerEntity);
            creg.emplace<game::Mana>(playerEntity);
            creg.emplace<game::Poise>(playerEntity);
            creg.emplace<game::ActionState>(playerEntity);

            creg.emplace<game::Stamina>(dummyEntity);
            creg.emplace<game::ActionState>(dummyEntity);
            game::Poise& dp = creg.emplace<game::Poise>(dummyEntity);
            dp.current = dp.max = 20.0f;
        }

        combat.director().setDeathCallback(
            [&dummy, &dummyAlive, &physics, dummyEntity](entt::entity e) {
                if (e != dummyEntity || !dummyAlive || !dummy.alive())
                    return;
                glm::vec3 pos;
                glm::quat rot;
                physics.getRenderTransform(dummy.body(), pos, rot);
                dummy.kill(physics, glm::vec3(0.0f, 3.0f, 6.0f), pos);
            });

        // Melee lands the equipped weapon's payload on the dummy.
        combat.melee().setHitCallback(
            [&combat, &dummy, &dummyAlive, &physics, &playerSys, &playerEntity](
                eng::BodyHandle body, glm::vec3 point, glm::vec3 normal) {
                if (!dummyAlive || !dummy.alive() || body != dummy.body())
                    return;
                const char* w =
                    playerSys.torchEquipped() ? "torch" : "sword";
                combat.director().hitBody(physics, body, w, playerEntity,
                                          -normal, point);
                // Feel layer: chip the victim's poise by the weapon's payload so
                // heavy/blunt hits can stagger it (opening a crit window).
                entt::entity victim = combat.director().entityForBody(body);
                if (victim != entt::null) {
                    const float pd =
                        combat.director().weapons().get(w).timing.poiseDamage;
                    game::feel::poise::apply(combat.director().registry(),
                                             victim, pd);
                }
            });
    }

    // Per-phase CPU timing; logged periodically (no on-screen UI).
    using game::ProfHud;
    ProfHud prof;

    // On-screen debug/tuning console (F1). Docked side panel, one tab per
    // system; every control writes straight to the live system.
    game::DebugOverlay debugUi;
    if (std::getenv("PSX_DEBUG_UI")) // start with the console open (testing)
        debugUi.setVisible(true);
    game::PerfOverlay perf; // top-left perf HUD, on by default, F4 toggles

    game::InteractionSystem interaction;

    // ---------------------------------------------------------------- loop ---
    constexpr float kFixedDt = 1.0f / 60.0f;
    float accumulator = 0.0f;
    float animTime = 0.0f;
    // Stop-motion animation clock (OSRS look): particles, world anim, viewmodels
    // and enemy/prop render-sync advance in 1/animFps snaps. 0/negative = full
    // rate. Runtime var so a debug slider can retune it live.
    float animFps = float(engine.config().getNumber("render.anim_fps", 15.0));
    float visualAccum = 0.0f;
    while (!engine.shouldClose()) {
        const float dt = engine.tick();
        // Quantize the animation clock. visualDt is 0 on most frames and jumps
        // one (or more, if the frame ran long) step on a tick frame; camera,
        // input and physics stay full-rate so only the *look* is choppy.
        float visualDt = dt;
        bool visualStep = true;
        if (animFps > 0.0f) {
            const float stepDur = 1.0f / animFps;
            visualAccum += dt;
            const int steps = int(visualAccum / stepDur);
            visualDt = float(steps) * stepDur;
            visualAccum -= visualDt;
            visualStep = steps > 0;
        }
        eng::Input& in = engine.input();
        using clk = std::chrono::steady_clock;
        auto phaseMs = [](clk::time_point t0) {
            return std::chrono::duration<float, std::milli>(clk::now() - t0).count();
        };
        // First Esc releases the mouse, second quits; click re-grabs.
        if (in.wasPressed("quit")) {
            if (in.mouseGrabbed())
                in.setMouseGrab(false);
            else
                engine.requestClose();
        }
        // F1 toggles the debug console. Opening it releases the mouse so the
        // cursor can drive the panel; the click-to-regrab above is suppressed
        // while it is open (see the guard on that branch is implicit: opening
        // sets grab off and the panel captures the click).
        if (in.wasPressed("debug_ui")) {
            debugUi.toggle();
            if (debugUi.visible())
                in.setMouseGrab(false);
        }
        if (!in.mouseGrabbed() && !debugUi.visible() && in.wasMouseClicked())
            in.setMouseGrab(true);

        // F3 toggles the collider wireframe overlay (drawn below).
        if (in.wasPressed("show_colliders"))
            colliderDbg.enabled = !colliderDbg.enabled;
        // F4 toggles the performance HUD.
        if (in.wasPressed("show_perf"))
            perf.toggle();

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

        // Props, projectiles and spells stay full-rate (smooth). Only the enemy
        // creature snaps at animFps: on non-tick frames its render node holds its
        // last pose (physics still runs at 60 Hz above), giving the stop-motion
        // creature movement without stepping projectiles/particles.
        props.sync(ctx);
        combat.syncRender(ctx);
        if (dummyAlive && visualStep)
            dummy.syncRender(physics, r);

        prof.ms[ProfHud::Physics] = phaseMs(tPhysics);

        auto tWorld = clk::now();
        animTime += visualDt; // world anim (torch flicker) snaps at animFps
        level.update(r, animTime);
        level.updateVisibility(r, player.eyePosition());
        prof.ms[ProfHud::World] = phaseMs(tWorld);

        auto tPlayer = clk::now();
        if (!portalPreviewMode && !debugUi.visible())
            playerSys.update(ctx, dt); // simulate + present
        else
            player.present(r); // sim frozen, but still apply camera/FOV tweaks
                               // so the debug-UI camera sliders take effect live
        prof.ms[ProfHud::Player] = phaseMs(tPlayer);

        // Look-interaction + portal transitions. Descend appends the next
        // depth's seed on first visit (so revisits reuse the same layout) and
        // rebuilds; ascend rebuilds the level below.
        interaction.update(
            ctx, level, depth, player.eyePosition(), player.forward(),
            /*onDescend=*/[&] {
                if (depth + 1 == int(seeds.size()))
                    seeds.push_back(baseSeed +
                                    uint32_t(depth + 1) * 0x9E3779B9u);
                ++depth;
                enterLevel(false);
            },
            /*onAscend=*/[&] {
                --depth;
                enterLevel(true);
            });

        // Attack input — only when mouse is grabbed (not in debug UI). Weapon
        // selection lives with the player; casts/swings are gated on the
        // equipped weapon and dispatched to combat.
        bool swordAttack = false;
        bool didCast = false;
        if (in.mouseGrabbed()) {
            if (in.wasPressed("swap_weapon"))
                playerSys.swapWeapon(ctx);
            if (in.wasPressed("fire_arrow"))
                combat.fireArrow(ctx, player.eyePosition(), player.forward());
            // Staff casts only when the staff is equipped.
            if (playerSys.staffEquipped() && in.wasPressed("cast_spell")) {
                combat.castFireball(ctx, player.eyePosition(), player.forward());
                didCast = true;
            }
            if (playerSys.staffEquipped() && in.wasPressed("cast_beam")) {
                combat.castBeam(ctx, player.eyePosition(), player.forward());
                didCast = true;
            }
            // Melee swing for the sword and the torch (a light club). Gated on
            // the feel layer: the swing costs stamina and only starts from Idle
            // (so it can't interrupt a dodge/deflect/stagger).
            if ((playerSys.swordEquipped() || playerSys.torchEquipped()) &&
                in.wasMouseClicked()) {
                entt::registry& creg = combat.director().registry();
                const char* w =
                    playerSys.torchEquipped() ? "torch" : "sword";
                game::ActionState& pas =
                    creg.get<game::ActionState>(playerEntity);
                game::Stamina& pst = creg.get<game::Stamina>(playerEntity);
                const game::WeaponDef& wd = combat.director().weapons().get(w);
                if (game::feel::actionstate::beginAttack(pas, pst, wd.timing)) {
                    combat.startSwing();
                    swordAttack = true;
                }
            }

            // Feel-layer defense. Deflect opens a brief negate window; dodge
            // grants i-frames (reuses Health.invulnTimer) and costs stamina;
            // kick shoves a target in front and chips its poise (env-kills /
            // make-room). Bound to V / B / G in game.toml.
            {
                entt::registry& creg = combat.director().registry();
                game::ActionState& pas =
                    creg.get<game::ActionState>(playerEntity);
                if (in.wasPressed("deflect"))
                    game::feel::defense::beginDeflect(pas);
                if (in.wasPressed("dodge"))
                    game::feel::defense::beginDodge(creg, playerEntity, 0.4f,
                                                    0.2f);
                if (in.wasPressed("kick") && dummyAlive && dummy.alive()) {
                    // Only shove the dummy when it is roughly in front and close.
                    glm::vec3 dpos;
                    glm::quat drot;
                    physics.getRenderTransform(dummy.body(), dpos, drot);
                    glm::vec3 toD = dpos - player.eyePosition();
                    if (glm::length(toD) < 3.0f &&
                        glm::dot(glm::normalize(toD), player.forward()) > 0.4f) {
                        glm::vec3 imp = game::feel::defense::kick(
                            creg, dummyEntity, player.forward(), 8.0f, 25.0f);
                        physics.applyImpulse(dummy.body(), imp, dpos);
                    }
                }
            }
        }
        auto tWeapons = clk::now();
        const bool aiming =
            in.mouseGrabbed() && in.isMouseDown(eng::MouseButton::Right);
        // Player viewmodel (hands/weapon) snaps at animFps — the player half of
        // the stop-motion look. Camera/movement stay full-rate above, so only
        // the weapon animation is stepped, not the view itself.
        playerSys.updateViewmodels(ctx, visualDt, swordAttack, didCast, aiming);
        prof.ms[ProfHud::Weapons] = phaseMs(tWeapons);

        prof.pushFrame(dt * 1000.0f);

        // Debug UI + overlays: one imgui frame per rendered frame (NewFrame and
        // Render must pair). The console early-returns when hidden.
        engine.beginImGuiFrame(dt);
        {
            game::DebugOverlay::Deps deps;
            deps.renderer = &r;
            deps.combat = &combat.config();
            deps.fps = &player;
            deps.registry = &combat.director().registry();
            deps.player = playerEntity;
            deps.prof = &prof;
            deps.colliders = &colliderDbg;
            debugUi.draw(deps);
            perf.draw(&prof, &r);

            // Collider gizmos: drawn as a SCREEN-SPACE imgui overlay (project
            // each 3D line to the window at full resolution), so they stay crisp
            // and identical regardless of the PSX pixelation / render profile.
            if (colliderDbg.enabled) {
                using Pal = eng::Physics::ColliderPalette;
                const Pal pal = (colliderDbg.colorMode == 1) ? Pal::ByLayer
                                                             : Pal::ByShape;
                static std::vector<eng::Physics::DebugLine> pl;
                pl.clear();
                physics.debugDraw(pl, pal, colliderDbg.includeStatic);

                const glm::mat4 vp = r.cameraViewProj();
                const ImVec2 ds = ImGui::GetIO().DisplaySize;
                ImDrawList* draw = ImGui::GetBackgroundDrawList();
                const bool uniform = colliderDbg.colorMode == 2;
                auto project = [&](const glm::vec3& w, ImVec2& out) -> bool {
                    const glm::vec4 c = vp * glm::vec4(w, 1.0f);
                    if (c.w <= 1e-4f) return false; // behind the camera
                    const float x = (c.x / c.w * 0.5f + 0.5f) * ds.x;
                    const float y = (1.0f - (c.y / c.w * 0.5f + 0.5f)) * ds.y;
                    out = ImVec2(x, y);
                    return true;
                };
                for (const auto& l : pl) {
                    ImVec2 a, b;
                    if (!project(l.a, a) || !project(l.b, b))
                        continue; // skip segments crossing the near plane
                    glm::vec3 col = uniform ? colliderDbg.uniformColor : l.colour;
                    col = glm::clamp(col * colliderDbg.brightness, 0.0f, 1.0f);
                    // 70% opacity so colliders read as an overlay, not solid
                    // geometry; the rest of the frame stays fully opaque.
                    const ImU32 c = IM_COL32(int(col.r * 255), int(col.g * 255),
                                             int(col.b * 255), 179);
                    draw->AddLine(a, b, c, colliderDbg.thickness);
                }
            }
        }

        auto tRender = clk::now();
        engine.renderFrame(dt); // full-rate: particles + Ogre anim stay smooth
        prof.ms[ProfHud::Render] = phaseMs(tRender);

        // Periodic profile log (replaces the removed Diagnostics window).
        static int profTick = 0;
        if (std::getenv("PSX_PROFILE") && ++profTick % 120 == 0)
            prof.logSummary();
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
