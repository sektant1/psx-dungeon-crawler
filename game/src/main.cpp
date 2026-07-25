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
#include "Dummy.h"
#include "GameContext.h"
#include "GameDiagnostics.h"
#include "GameScene.h"
#include "InteractionSystem.h"
#include "PlayerSystem.h"
#include "PropSystem.h"
#include "combat/CombatComponents.h"

#include <eng/ecs/Components.h>

#include <DemoScene.h>


#include <eng/Content.h>
#include <eng/Engine.h>
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
            });
    }

    // Per-phase CPU timing; logged periodically (no on-screen UI).
    using game::ProfHud;
    ProfHud prof;

    game::InteractionSystem interaction;

    // ---------------------------------------------------------------- loop ---
    constexpr float kFixedDt = 1.0f / 60.0f;
    float accumulator = 0.0f;
    float animTime = 0.0f;
    while (!engine.shouldClose()) {
        const float dt = engine.tick();
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
        if (!in.mouseGrabbed() && in.wasMouseClicked())
            in.setMouseGrab(true);

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
            playerSys.update(ctx, dt);
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
            // Melee swing for the sword and the torch (a light club).
            if ((playerSys.swordEquipped() || playerSys.torchEquipped()) &&
                in.wasMouseClicked()) {
                combat.startSwing();
                swordAttack = true;
            }
        }
        auto tWeapons = clk::now();
        const bool aiming =
            in.mouseGrabbed() && in.isMouseDown(eng::MouseButton::Right);
        playerSys.updateViewmodels(ctx, dt, swordAttack, didCast, aiming);
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
