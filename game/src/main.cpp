// dungeon-crawler: FPS walk through a procedurally generated PSX dungeon
// (DungeonGen -> DungeonMap), with the shared demo scene (crystals, chest,
// sparkles, light shaft) sitting in the generated level's anchor room.
// A whole level is built by buildLevel() into a Level bundle; level
// transitions (portals) clear the scene and rebuild.
//
// The frame loop itself lives in eng::runApplication; DungeonApp below only
// fills in the ordered callbacks (input -> fixed sim -> present -> gui).

#include "DungeonGen.h"
#include "GameCollision.h"
#include "PrototypeCatalogLoader.h"
#include "DungeonMap.h"
#include "LevelResource.h"
#include "LiveLevel.h"
#include "MapPlay.h"
#include "SceneFactory.h"
#include <eng/particles/ParticleLibrary.h>
#include <eng/controllers/FpsController.h>
#include "CombatSystem.h"
#include "DebugOverlay.h"
#include "Dummy.h"
#include "GameContext.h"
#include "GameHud.h"
#include "GameScene.h"
#include "HudModel.h"
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
#include <eng/Log.h>
#include <eng/Physics.h>
#include <eng/DebugTools.h>
#include <eng/FrameStats.h>
#include <eng/Profiler.h>
#include <eng/app/FpsGameApp.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace {

// Frame phases the game measures. Names and order are game policy; the ring
// buffer, the stopwatch and the plot are eng::FrameStats.
enum Phase { PhasePhysics, PhaseWorld, PhasePlayer, PhaseWeapons, kPhaseCount };

class DungeonApp : public eng::FpsGameApp
{
public:
    eng::AppConfig configure(int argc, char** argv) override
    {
        mAssets = APP_ASSET_DIR;
        // `--scene <name>` picks the framing a clip is shot from, so a recording
        // is reproducible from the command line alone. "portal" is the
        // down-portal showcase the PSX_SHOWCASE_PORTAL env var also selects.
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i] ? argv[i] : "";
            if (arg == "--scene" && i + 1 < argc)
                mScene = argv[i + 1];
        }
        mRecording = eng::GifRecorder::optionsFromArgs(argc, argv);
        eng::AppConfig cfg;
        // `--render-preset <name>`: the look a run starts in ("ps1", "ps2",
        // "gamecube", "n64", "pixel-3d", "modern-ps1", "dungeon"). The debug
        // console still switches profiles live; this only picks the start.
        cfg.renderPreset = eng::renderPresetFromArgs(argc, argv);
        cfg.assetDir = mAssets;
        cfg.configPath = mAssets + "/game.toml";
        cfg.fixedDt = kFixedDt;
        cfg.maxFixedSteps = 5;
        cfg.imgui = true;
        return cfg;
    }

protected:
    eng::FpsGameConfig setup(eng::Engine& engine) override;
    bool onStartGame(eng::Engine& engine) override;
    void onInput(const eng::FrameContext& f) override;
    void onPreSimulate(const eng::FrameContext& f, float fixedDt) override;
    void onPostSimulate(const eng::FrameContext& f, float fixedDt) override;
    void onPresent(const eng::FrameContext& f) override;
    void onGameGui(const eng::FrameContext& f) override;
    void onStopGame(eng::Engine& engine) override;

    eng::FpsController* playerController() override
    {
        return &mPlayerSys.controller();
    }
    // The portal preview freezes the sim on top of the console's own freeze.
    bool playerDriven() const override
    {
        return !mPortalPreviewMode && !uiOpen();
    }

private:
    void teardownDummy();
    // Wipe the scene, build the level at `mDepth`, and (re)spawn the player.
    // atExit spawns at the down-portal (arrived by ascending); else at entry.
    void enterLevel(eng::Engine& engine, bool atExit);
    void wireCombatModel();

    static constexpr float kFixedDt = 1.0f / 60.0f;

    std::string mAssets;
    eng::Engine* mEngine = nullptr; // set in onStart; used by enterLevel

    // Persistent level stack: mSeeds[d] is depth d's seed (stored so revisits
    // reuse it -> identical layout). Live scenes are never cached; one level is
    // live at a time and rebuilt on every transition.
    std::vector<uint32_t> mSeeds;
    int mDepth = 0;
    uint32_t mBaseSeed = 1;

    float mSpeed = 3.0f;
    float mSens = 0.002f;
    eng::FpsController::DashTuning mDashTuning;
    float mDodgeIframes = 0.22f;
    float mDodgeStamina = 25.0f;

    game::CombatVocabulary mVocabulary;
    std::optional<game::GameContext> mCtx;
    game::CombatSystem mCombat;
    eng::ParticleLibrary mParticles;
    game::PropSystem mProps;
    Dummy mDummy;
    bool mDummyAlive = false;
    entt::entity mPlayerEntity = entt::null;
    entt::entity mDummyEntity = entt::null;

    LiveLevel mLevel;
    game::PlayerSystem mPlayerSys;
    game::GameHud mHud;
    bool mPortalPreviewMode = false;
    std::string mScene;                                  // --scene
    std::optional<eng::RecordingOptions> mRecording;     // --record
    eng::Content mLevelContent;
    std::string mShowroomPath;

    game::DebugPanels mPanels;
    game::InteractionSystem mInteraction;

    // Per-frame scratch shared between callbacks.
    bool mSwordAttack = false;
    bool mDidCast = false;
};

// Everything the genre base needs before physics exists. Note what is NOT here
// any more: camera clip planes, the collision setup call, the console, the
// frame-stat ring -- the base owns those now.
eng::FpsGameConfig DungeonApp::setup(eng::Engine& engine)
{
    mAssets = APP_ASSET_DIR;
    eng::Config& cfg = engine.config();

    game::layer::PhysicsTuning physicsTuning;
    physicsTuning.gravity = float(cfg.getNumber("physics.gravity", -18.0));
    physicsTuning.characterPushImpulse =
        float(cfg.getNumber("physics.character_push_impulse", 2.0));
    physicsTuning.multithreaded = cfg.getBool("physics.multithreaded", false);

    eng::FpsGameConfig game;
    game.physics = game::layer::physicsSetup(physicsTuning);
    game.staticLayers = eng::layerMask(game::layer::Static);
    game.cameraFov = 70.0f;
    // With the current 0.05 exponential fog, a 90 m far plane retains only
    // ~1.1% scene colour. The cut is hidden without visibly popping long
    // corridors; the 0.08 m near plane also improves depth precision.
    game.nearClip = 0.08f;
    game.farClip = 90.0f;
    game.phases = {"Physics", "World", "Player", "Weapons"};
    return game;
}

bool DungeonApp::onStartGame(eng::Engine& engine)
{
    mEngine = &engine;
    eng::Renderer& r = engine.renderer();

    // What a failed load is replaced with. Registered before any level builds,
    // so the very first missing asset already reads as what it was meant to be.
    {
        eng::prototype::PrototypeCatalog prototypes;
        game::loadPrototypeCatalog(mAssets + "/prototypes.toml", prototypes);
        r.setPrototypeCatalog(std::move(prototypes));
    }

    if (const char* s = std::getenv("PSX_GEN_SEED"))
        mBaseSeed = uint32_t(std::strtoul(s, nullptr, 10));
    mSeeds.assign(1, mBaseSeed);

    eng::Config& cfg = engine.config();
    mSpeed = float(cfg.getNumber("player.speed", 3.0));
    mDashTuning.speed = float(cfg.getNumber("dodge.speed", 14.0));
    mDashTuning.duration = float(cfg.getNumber("dodge.duration", 0.32));
    mDashTuning.cooldown = float(cfg.getNumber("dodge.cooldown", 0.45));
    mDodgeIframes = float(cfg.getNumber("dodge.iframes", 0.22));
    mDodgeStamina = float(cfg.getNumber("dodge.stamina", 25.0));
    mSens = float(cfg.getNumber("player.mouse_sensitivity", 0.002));

    colliderView().enabled = std::getenv("PSX_SHOW_COLLIDERS") != nullptr;

    // Damage channels and schools of magic. Loaded before anything that names
    // one, and owned here because the level builder, the viewmodels and the
    // combat model all resolve names through the same table.
    if (!mVocabulary.load(mAssets + "/magic.toml"))
        eng::log::error("magic.toml failed to load; combat names resolve to "
                        "nothing and weapons fall back to the first channel");

    mCtx.emplace(r, physics(), engine.input(), mAssets, mVocabulary);

    mParticles.load(r, mAssets + "/particles.toml");

    // Player controller + first-person viewmodels + active weapon selection.
    // Weapon cycles on the swap_weapon bind and drives which viewmodel shows and
    // which attack input CombatSystem accepts (Sword->melee, Staff->spells,
    // Torch->light+bash).
    mPlayerSys.setTuning(mSpeed, mSens);
    mPlayerSys.controller().setDashTuning(mDashTuning);
    mHud.configure(cfg);
    mPortalPreviewMode = std::getenv("PSX_SHOWCASE_PORTAL") != nullptr ||
                         mScene == "portal";

    // Authored showroom shell. Override it for layout iteration without
    // recompiling; all depth-zero exhibits are positioned relative to its C
    // anchor, which is world origin.
    const char* showroomOverride = std::getenv("PSX_SHOWROOM_MAP");
    mShowroomPath =
        showroomOverride ? showroomOverride : mAssets + "/showroom.toml";

    enterLevel(engine, false); // depth 0, spawn at entry

    // Initialise combat (loads [combat.*], builds procedural projectile/spell
    // meshes) and register the contact seam so arrows stick and bolts despawn.
    mCombat.init(*mCtx, mAssets + "/game.toml");
    physics().setContactCallback([this](const eng::HitEvent& e) {
        mCombat.onContact(*mCtx, e);
        if (mDummyAlive && mDummy.alive() &&
            (e.self == mDummy.body() || e.other == mDummy.body())) {
            // Projectile hit the dummy: route arrow damage through the combat
            // model (resistances, crit, death). Knockback pushes it forward/up.
            mCombat.director().hitBody(physics(), mDummy.body(), "arrow",
                                       mPlayerEntity,
                                       glm::vec3(0.0f, 0.4f, 1.0f), e.point);
        }
    });

    mProps.spawnShowroom(*mCtx, mLevel.markerPlacements("physics."));

    // Portal proving-ground target: far enough from spawn to establish a
    // readable combat lane, centred so arrows, spells and enchantments are
    // tested against the strongest architectural sightline.
    mDummy.init(physics(), r, glm::vec3(0.0f, 0.0f, -18.0f));
    mDummyAlive = true;

    wireCombatModel();

    // The engine's console owns every engine-tuning tab; the game adds the two
    // that are its own policy.
    mPanels.install(console());
    if (std::getenv("PSX_DEBUG_UI")) // start with the console open (testing)
        console().setVisible(true);
    // Last, so the clip's first frame is a fully built level: recording pins the
    // frame delta, and any load hitch would otherwise be baked into the timing.
    if (mRecording)
        engine.startRecording(*mRecording);
    return true;
}

void DungeonApp::teardownDummy()
{
    if (!mDummyAlive)
        return;
    mCombat.director().removeCombatant(mDummy.body()); // drop body->entity link
    mDummyEntity = entt::null;
    mDummy.clear(physics(), mCtx->renderer);
    mDummyAlive = false;
}

void DungeonApp::enterLevel(eng::Engine& engine, bool atExit)
{
    eng::Renderer& r = engine.renderer();
    // Destroy dynamic prop + dummy bodies before clearScene wipes their nodes.
    mProps.teardown(physics());
    teardownDummy();
    bool loaded = false;
    if (mDepth == 0) {
        if (LevelResource* showroom =
                mLevelContent.load<LevelResource>("showroom", mShowroomPath)) {
            loaded = mLevel.rebuildLayout(r, physics(), mVocabulary, mAssets,
                                          showroom->layout(), mDepth);
        }
        // load() already logged on failure; leaves `loaded` false.
    } else {
        loaded = mLevel.rebuild(r, physics(), mVocabulary, mAssets,
                                mSeeds[size_t(mDepth)], mDepth);
    }
    if (!loaded) {
        eng::log::error("Level %d failed to load", mDepth);
        return;
    }
    eng::FpsController& player = mPlayerSys.controller();
    const bool portalPreview = mDepth == 0 && mPortalPreviewMode;
    const float portalYaw = glm::radians(mLevel.dungeon().exitYawDegrees());
    const glm::vec3 portalFront(std::sin(portalYaw), 0.0f, std::cos(portalYaw));
    const glm::vec3 p = portalPreview
        ? mLevel.exitPosition() + portalFront * 4.0f
        : (atExit ? mLevel.exitPosition() : mLevel.spawnPosition());
    mPlayerSys.spawnAt(*mCtx, p);
    if (portalPreview) {
        player.setViewAngles(portalYaw);
        player.present(r);
    }
    mPlayerSys.attachLoadout(*mCtx);
    engine.input().setMouseGrab(!portalPreview);
    mHud.notifyRegion(mDepth == 0 ? game::HudRegion::Threshold
                                  : game::HudRegion::Interior);
}

// The player is a bodiless combatant used as a damage source (faction gate).
// The dummy is an armored, flammable Enemy: resists physical, takes extra fire.
// Melee/arrow hits and the "Combat" debug panel drive real damage; the death
// callback triggers the existing topple.
void DungeonApp::wireCombatModel()
{
    game::Health playerHp;
    playerHp.current = playerHp.max = 100.0f;
    mPlayerEntity = mCombat.director().addCombatant(
        eng::BodyHandle{}, playerHp, game::Resistances{}, game::Faction::Player);

    game::Health dummyHp;
    dummyHp.current = dummyHp.max = 60.0f;
    game::Resistances dummyResist{};
    // Channels by name: which ids these are is magic.toml's business.
    dummyResist[mVocabulary.damageType("physical")] = 0.35f; // plated
    dummyResist[mVocabulary.damageType("fire")] = -0.5f;     // flammable
    mDummyEntity = mCombat.director().addCombatant(
        mDummy.body(), dummyHp, dummyResist, game::Faction::Enemy);

    // Feel layer: give the player and the dummy a stamina/poise/action-state
    // profile. The player uses defaults; the dummy is a goblin-like target with
    // low poise so one heavy hit or a kick staggers it.
    {
        entt::registry& creg = mCombat.director().registry();
        creg.emplace<game::Stamina>(mPlayerEntity);
        creg.emplace<game::Mana>(mPlayerEntity);
        creg.emplace<game::Poise>(mPlayerEntity);
        creg.emplace<game::ActionState>(mPlayerEntity);

        creg.emplace<game::Stamina>(mDummyEntity);
        creg.emplace<game::ActionState>(mDummyEntity);
        game::Poise& dp = creg.emplace<game::Poise>(mDummyEntity);
        dp.current = dp.max = 20.0f;
    }

    mCombat.director().setDeathCallback([this](entt::entity e) {
        if (e != mDummyEntity || !mDummyAlive || !mDummy.alive())
            return;
        glm::vec3 pos;
        glm::quat rot;
        physics().getRenderTransform(mDummy.body(), pos, rot);
        mDummy.kill(physics(), glm::vec3(0.0f, 3.0f, 6.0f), pos);
    });

    // Melee lands the equipped weapon's payload on the dummy.
    mCombat.melee().setHitCallback(
        [this](eng::BodyHandle body, glm::vec3 point, glm::vec3 normal) {
            if (!mDummyAlive || !mDummy.alive() || body != mDummy.body())
                return;
            const char* w = mPlayerSys.torchEquipped() ? "torch" : "sword";
            mCombat.director().hitBody(physics(), body, w, mPlayerEntity, -normal,
                                       point);
            // Feel layer: chip the victim's poise by the weapon's payload so
            // heavy/blunt hits can stagger it (opening a crit window).
            entt::entity victim = mCombat.director().entityForBody(body);
            if (victim != entt::null) {
                const float pd =
                    mCombat.director().weapons().get(w).timing.poiseDamage;
                game::feel::poise::apply(mCombat.director().registry(), victim,
                                         pd);
            }
        });
}

void DungeonApp::onInput(const eng::FrameContext& f)
{
    mSwordAttack = false;
    mDidCast = false;


    // Look runs at the render rate; locomotion runs in onPreSimulate. The base
    // class documents that split; this is the game's half of it.
    if (playerDriven())
        mPlayerSys.look(*mCtx);
}

void DungeonApp::onPreSimulate(const eng::FrameContext&, float fixedDt)
{
    if (playerDriven())
        mPlayerSys.fixedStep(*mCtx, fixedDt);
}

void DungeonApp::onPostSimulate(const eng::FrameContext&, float fixedDt)
{
    eng::FpsController& player = mPlayerSys.controller();
    mCombat.fixedStep(*mCtx, player.eyePosition(), player.forward(), fixedDt);
}

void DungeonApp::onPresent(const eng::FrameContext& f)
{
    eng::Engine& engine = f.engine;
    eng::Renderer& r = engine.renderer();
    eng::Input& in = engine.input();
    eng::StepClock& steps = engine.stepClock();
    eng::FpsController& player = mPlayerSys.controller();

    // Render-sync stepping. Each of these copies a physics transform onto a
    // render node, so skipping the copy on a hold frame *is* the stop-motion:
    // the node keeps its last pose while the 60 Hz simulation carries on
    // underneath, leaving collisions and hit registration untouched.
    // Projectiles get their own faster channel so an arrow stays trackable
    // rather than teleporting across the room in metre-long jumps.
    if (steps.stepped(eng::StepChannel::World))
        mProps.sync(*mCtx);
    if (steps.stepped(eng::StepChannel::Projectiles))
        mCombat.syncRender(*mCtx);
    // Per-creature phase seed: with several enemies on screen and phase_jitter
    // above 0 they stop snapping in unison like one puppet.
    if (mDummyAlive && steps.stepped(eng::StepChannel::Characters,
                                     mDummy.body().id))
        mDummy.syncRender(physics(), r);

    // World anim (torch flicker, animated dressing) is pose-from-time, so it
    // takes the quantised clock directly rather than accumulating a delta -- no
    // drift, and it re-bases correctly if the rate is changed live.
    {
        const auto timed = stats().time(PhaseWorld);
        mLevel.update(r, steps.time(eng::StepChannel::World));
        mLevel.updateVisibility(r, player.eyePosition());
    }

    // Present only: the simulation for this frame already ran in onFixedStep.
    // When the sim is frozen (portal preview, debug UI) this still applies the
    // camera/FOV tweaks so the debug sliders take effect live.
    {
        const auto timed = stats().time(PhasePlayer);
        mPlayerSys.present(*mCtx, playerDriven() ? f.alpha : 1.0f);
    }

    // Look-interaction + portal transitions. Descend appends the next depth's
    // seed on first visit (so revisits reuse the same layout) and rebuilds;
    // ascend rebuilds the level below.
    mInteraction.update(
        *mCtx, mLevel, mDepth, player.eyePosition(), player.forward(),
        /*onDescend=*/[&] {
            if (mDepth + 1 == int(mSeeds.size()))
                mSeeds.push_back(mBaseSeed + uint32_t(mDepth + 1) * 0x9E3779B9u);
            ++mDepth;
            enterLevel(engine, false);
        },
        /*onAscend=*/[&] {
            --mDepth;
            enterLevel(engine, true);
        });

    // Attack input — only when mouse is grabbed (not in debug UI). Weapon
    // selection lives with the player; casts/swings are gated on the equipped
    // weapon and dispatched to combat.
    if (in.mouseGrabbed()) {
        if (in.wasPressed("swap_weapon"))
            mPlayerSys.swapWeapon(*mCtx);
        if (in.wasPressed("fire_arrow"))
            mCombat.fireArrow(*mCtx, player.eyePosition(), player.forward());
        // Staff casts only when the staff is equipped.
        if (mPlayerSys.staffEquipped() && in.wasPressed("cast_spell")) {
            mCombat.castFireball(*mCtx, player.eyePosition(), player.forward());
            mDidCast = true;
        }
        if (mPlayerSys.staffEquipped() && in.wasPressed("cast_beam")) {
            mCombat.castBeam(*mCtx, player.eyePosition(), player.forward());
            mDidCast = true;
        }
        // Melee swing for the sword and the torch (a light club). Gated on the
        // feel layer: the swing costs stamina and only starts from Idle (so it
        // can't interrupt a dodge/deflect/stagger).
        if ((mPlayerSys.swordEquipped() || mPlayerSys.torchEquipped()) &&
            in.wasMouseClicked()) {
            entt::registry& creg = mCombat.director().registry();
            const char* w = mPlayerSys.torchEquipped() ? "torch" : "sword";
            game::ActionState& pas = creg.get<game::ActionState>(mPlayerEntity);
            game::Stamina& pst = creg.get<game::Stamina>(mPlayerEntity);
            const game::WeaponDef& wd = mCombat.director().weapons().get(w);
            if (game::feel::actionstate::beginAttack(pas, pst, wd.timing)) {
                mCombat.startSwing();
                mSwordAttack = true;
            }
        }

        // Feel-layer defense. Deflect opens a brief negate window; dodge grants
        // i-frames (reuses Health.invulnTimer) and costs stamina; kick shoves a
        // target in front and chips its poise (env-kills / make-room). Bound to
        // V / B / G in game.toml.
        {
            entt::registry& creg = mCombat.director().registry();
            game::ActionState& pas = creg.get<game::ActionState>(mPlayerEntity);
            if (in.wasPressed("deflect"))
                game::feel::defense::beginDeflect(pas);
            // Order matters: the dash is asked first because it is the one that
            // can refuse (cooldown). Paying stamina and granting i-frames for a
            // dodge that never moves is the bug that made this read as "there is
            // no dash".
            if (in.wasPressed("dodge") &&
                player.beginDash(player.inputDirection(
                    eng::FpsController::readCommand(in))))
                game::feel::defense::beginDodge(creg, mPlayerEntity,
                                                mDashTuning.duration,
                                                mDodgeIframes, mDodgeStamina);
            if (in.wasPressed("kick") && mDummyAlive && mDummy.alive()) {
                // Only shove the dummy when it is roughly in front and close.
                glm::vec3 dpos;
                glm::quat drot;
                physics().getRenderTransform(mDummy.body(), dpos, drot);
                glm::vec3 toD = dpos - player.eyePosition();
                if (glm::length(toD) < 3.0f &&
                    glm::dot(glm::normalize(toD), player.forward()) > 0.4f) {
                    glm::vec3 imp = game::feel::defense::kick(
                        creg, mDummyEntity, player.forward(), 8.0f, 25.0f);
                    physics().applyImpulse(mDummy.body(), imp, dpos);
                }
            }
        }
    }

    const bool aiming =
        in.mouseGrabbed() && in.isMouseDown(eng::MouseButton::Right);
    // Player viewmodel (hands/weapon) — the player's half of the look. Its own
    // channel because it sits centimetres from the eye, where the rate that
    // suits a distant creature reads about twice as harsh. Camera and movement
    // stay full-rate: the weapon animation is stepped, the view is not.
    {
        const auto timed = stats().time(PhaseWeapons);
        mPlayerSys.updateViewmodels(
            *mCtx, steps.delta(eng::StepChannel::Viewmodel), mSwordAttack,
            mDidCast, aiming);
    }

    if (std::getenv("PSX_PROFILE"))
        stats().logSummaryEvery(120);
}

void DungeonApp::onGameGui(const eng::FrameContext& f)
{
    // The combat registry and the player entity are recreated on every level
    // transition, so the panels get fresh pointers each frame rather than
    // capturing them at install time.
    game::DebugPanels::Deps deps;
    deps.combat = &mCombat.config();
    deps.registry = &mCombat.director().registry();
    deps.player = mPlayerEntity;
    deps.playerSystem = &mPlayerSys;
    deps.renderer = &f.engine.renderer();
    mPanels.update(deps);
    const game::HudSnapshot hudFrame =
        game::buildHudSnapshot(mCombat.director().registry(), mPlayerEntity,
                               mPlayerSys.weapon(), mInteraction.focus());
    mHud.draw(hudFrame, f.dt, !uiOpen() && !mPortalPreviewMode);


    // renderFrame() (owned by the runner) paints all of the above; it is
    // full-rate so particles + Ogre anim stay smooth.
}

void DungeonApp::onStopGame(eng::Engine&)
{
    // Remove dynamic prop bodies before shutdown (nodes are owned by Ogre/scene).
    mProps.teardown(physics());
    teardownDummy();
    mLevel.clearPhysics();
    if (mCtx)
        mCombat.clear(*mCtx);
}

} // namespace

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

    // `game <file.map>`: play an authored editor scene instead of the procedural
    // dungeon. Its own Application, so it brings up its own engine + physics.
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.size() > 4 && arg.substr(arg.size() - 4) == ".map")
            return game::runMap(APP_ASSET_DIR, arg);
    }

    DungeonApp app;
    return eng::runApplication(app, argc, argv);
}
