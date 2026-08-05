// Raven Engine game sample: FPS traversal through a generated PSX-style dungeon.
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
#include "scene/ComponentRegistry.h"

#include <eng/script/ScriptHost.h>
#include "MapPlay.h"
#include "SceneFactory.h"
#include <eng/particles/ParticleLibrary.h>
#include <eng/controllers/FpsController.h>
#include "CombatSystem.h"
#include "ParticleCollider.h"
#include "DebugOverlay.h"
#include "Dummy.h"
#include "GameAssets.h"
#include "GameContext.h"
#include "ui/GameHud.h"
#include "ui/TooltipBuilder.h"
#include "HitFeel.h"

#include <eng/telemetry/Telemetry.h>
#include "HudModel.h"
#include "InteractionSystem.h"
#include "PlayerSystem.h"
#include "PropSystem.h"
#include "combat/CombatComponents.h"
#include "combat/DamageSystem.h"
#include "enemy/EnemyLibrary.h"
#include "enemy/EnemySave.h"
#include "enemy/EnemySpawner.h"
#include "enemy/EnemySystem.h"
#include "combat/ActionStateSystem.h"
#include "rpg/RpgRuntime.h"
#include "combat/DefenseSystem.h"
#include "combat/FeelComponents.h"
#include "combat/PoiseSystem.h"
#include "audio/ActorAudio.h"
#include "audio/GameAudio.h"

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
        // `--scene <name>` picks the framing a clip is shot from, so a recording
        // is reproducible from the command line alone. "portal" is the
        // down-portal showcase the RAVEN_SHOWCASE_PORTAL env var also selects.
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i] ? argv[i] : "";
            if (arg == "--scene" && i + 1 < argc)
                mScene = argv[i + 1];
            // A cooked map plays as the level itself, inside the full game --
            // enemies, combat, HUD, portals. LiveLevel::rebuildAuthored was
            // written for exactly this and had no caller, so the editor's F5
            // went to a stripped walk loop instead and every authored encounter
            // silently did nothing.
            if (arg.size() > 4 && arg.compare(arg.size() - 4, 4, ".map") == 0)
                mAuthoredMap = arg;
        }
        mRecording = eng::GifRecorder::optionsFromArgs(argc, argv);
        eng::AppConfig cfg;
        // `--render-preset <name>`: the look a run starts in ("ps1", "ps2",
        // "gamecube", "n64", "pixel-3d", "modern-ps1", "dungeon"). The debug
        // console still switches profiles live; this only picks the start.
        cfg.renderPreset = eng::renderPresetFromArgs(argc, argv);
        cfg.mountSet = "game";
        cfg.configPath = "config/game.toml";
        cfg.fixedDt = kFixedDt;
        cfg.maxFixedSteps = 5;
        cfg.imgui = true;
        cfg.loadingTitle = "ENTERING THE DUNGEON";
        cfg.loadingHint = "the torches are still being lit";
        return cfg;
    }

protected:
    eng::FpsGameConfig setup(eng::Engine& engine) override;
    void onLoadGame(eng::Engine& engine, eng::LoadPlan& plan) override;
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
    // Enemy table + spawners + the callbacks that let an enemy hurt the player.
    void wireEnemies();
    // The player's feet, which is what enemy perception and spawner activation
    // are measured from (the eye is 1.7 m up and would skew both).
    glm::vec3 playerFeet() const;
    // Resolves whatever the crosshair is on into tooltip content. It lives on
    // the app because only the app can see both the level's prop catalog and
    // the combat registry; the builder itself stays a pure function.
    eng::ui::TooltipContent lookTooltip();
    // Wipe the scene, build the level at `mDepth`, and (re)spawn the player.
    // atExit spawns at the down-portal (arrived by ascending); else at entry.
    void enterLevel(eng::Engine& engine, bool atExit);
    void wireCombatModel();
    // The player landed a projectile hit. One path for every authored weapon
    // payload and every registered victim: damage, poise, and feedback.
    // A no-op on bodies that are not registered combatants.
    void playerHit(eng::BodyHandle body, const char* weaponId, glm::vec3 dir,
                   glm::vec3 point);
    // Which blood.toml profile a combatant bleeds. Data, not a branch: it comes
    // off the enemy's own definition, so an undead shedding ichor and a
    // construct shedding nothing are both a word in enemies.toml.
    std::string bloodProfileFor(entt::entity e);
    // Everything a landed hit throws: spray and gibs at the wound, and the
    // instant mark on whatever is under it. One place, so the player and an
    // enemy bleed by the same rules.
    void bleed(entt::entity victim, glm::vec3 point, glm::vec3 dir,
               const game::DamageResult& result);
    // Once a frame: everything still standing that is hurt enough to drip.
    void updateBleeders();
    // Souls-style lock-on: who the player is holding, once per rendered frame.
    void updateLockOn(const eng::FrameContext& f);

    static constexpr float kFixedDt = 1.0f / 60.0f;
    // Matches eng::CharacterDesc::height as the player is created with it.
    // Enemy perception and spawner activation measure from the feet, and the
    // controller only reports the eye.
    static constexpr float kPlayerEyeHeight = 1.7f;

    eng::Engine* mEngine = nullptr; // set in onStart; used by enterLevel

    // Persistent level stack: mSeeds[d] is depth d's seed (stored so revisits
    // reuse it -> identical layout). Live scenes are never cached; one level is
    // live at a time and rebuilt on every transition.
    std::vector<uint32_t> mSeeds;
    int mDepth = 0;
    uint32_t mBaseSeed = 1;

    float mSpeed = 3.0f;
    float mSens = 0.002f;
    // The game's own player tuning, from game.toml. A level that authors a
    // FirstPersonController or a ViewmodelRig overrides these for as long as
    // it is loaded; leaving the level restores them.
    eng::ecs::FirstPersonController mConfigController{};
    // The over-the-shoulder framing and the lock-on rules, same story: the
    // game's defaults until a level authors a ThirdPersonCamera of its own.
    eng::ecs::ThirdPersonCamera mConfigCamera{};
    game::CameraMode mConfigCameraMode = game::CameraMode::FirstPerson;
    game::ViewmodelRig mConfigRig{};
    std::string mPlayerBlood = "human";
    eng::FpsController::DashTuning mDashTuning;
    float mDodgeIframes = 0.22f;
    float mDodgeStamina = 25.0f;

    // Screen shake and hit-stop. Driven from the UNSCALED wall delta, because
    // the thing a hit-stop slows is the clock it would otherwise be reading.
    game::HitFeel mHitFeel;
    bool mHitStopDrivingClock = false;

    game::CombatVocabulary mVocabulary;
    std::optional<game::GameContext> mCtx;
    game::CombatSystem mCombat;
    game::GameAudioSystem mGameAudio;
    // Plays an actor's actions. Constructed once the combat registry exists,
    // because the override chain it walks lives on that registry.
    std::optional<game::ActorAudio> mActorAudio;
    bool mCombatReady = false;
    eng::ParticleLibrary mParticles;
    // Owned here so it outlives the renderer that holds a pointer to it.
    std::optional<game::JoltParticleCollider> mParticleCollider;
    game::PropSystem mProps;
    Dummy mDummy;
    bool mDummyAlive = false;
    entt::entity mPlayerEntity = entt::null;
    // Last frame's stance, so leaving the ground can be detected as an edge.
    bool mPlayerWasGrounded = true;
    entt::entity mDummyEntity = entt::null;

    // Enemies. The library is the authored table, the system owns the live
    // ones (on the combat registry), and the spawner owns pacing.
    game::EnemyLibrary mEnemyLibrary;
    game::EnemySystem mEnemies;
    game::EnemySpawner mSpawner;

    // The game's one ECS world, and the renderer view driven from it. It
    // outlives every level: the player and anything else persistent live here
    // ungrouped, while a level's own entities carry kLevelGroup and are
    // destroyed by group on a transition.
    std::optional<eng::ecs::RendererSceneBackend> mBackend;
    eng::ecs::World mWorld;
    // Constructed in onStartGame once the world and physics exist; reset in
    // onStopGame before either goes, because the host holds a contact
    // subscription on physics and an on_destroy hook on the registry.
    std::optional<eng::script::ScriptHost> mScripts;
    LiveLevel mLevel;
    game::PlayerSystem mPlayerSys;
    game::GameHud mHud;
    bool mPortalPreviewMode = false;
    std::string mScene;                                  // --scene
    std::string mAuthoredMap; // a cooked .map named on the command line
    std::optional<eng::RecordingOptions> mRecording;     // --record
    eng::Content mLevelContent;
    std::string mShowroomPath;

    game::DebugPanels mPanels;
    game::InteractionSystem mInteraction;

    // The RPG layer: skills, items, inventory, loot, quests, dialogue, trading,
    // the safehouse and the raid state machine. One object, wired in
    // onStartGame and driven from the same four callbacks as everything else.
    game::rpg::RpgRuntime mRpg;
    bool mRpgReady = false;

    glm::vec3 mLastPlayerHitDirection{0.0f, 0.0f, -1.0f};
};

// Everything the genre base needs before physics exists. Note what is NOT here
// any more: camera clip planes, the collision setup call, the console, the
// frame-stat ring -- the base owns those now.
eng::FpsGameConfig DungeonApp::setup(eng::Engine& engine)
{
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

// The asset libraries the level build reads from, as named load steps: each
// one is a file parse the player now watches tick past instead of waiting out
// behind a frozen window. Everything here runs before onStartGame.
void DungeonApp::onLoadGame(eng::Engine& engine, eng::LoadPlan& plan)
{
    eng::Renderer& r = engine.renderer();

    // What a failed load is replaced with. Registered before any level builds,
    // so the very first missing asset already reads as what it was meant to be.
    plan.add("Reading prototypes", [this, &r] {
        eng::prototype::PrototypeCatalog prototypes;
        game::loadPrototypeCatalog(game::assetPath("config/prototypes.toml"), prototypes);
        r.setPrototypeCatalog(std::move(prototypes));
    });

    // Damage channels and schools of magic. Loaded before anything that names
    // one, and owned here because the level builder, the viewmodels and the
    // combat model all resolve names through the same table.
    plan.add("Binding the schools of magic", [this] {
        if (!mVocabulary.load(game::assetPath("config/magic.toml")))
            eng::log::error("magic.toml failed to load; combat names resolve "
                            "to nothing and weapons fall back to the first "
                            "channel");
    });

    plan.add("Kindling particle effects",
             [this, &r] {
                 mParticles.load(r, game::assetPath("config/particles.toml"));
             },
             2.0f);
    plan.add("Tuning the soundscape", [this] {
        if (!mGameAudio.load(audio(), game::assetPath("config/audio.toml")))
            eng::log::error("audio.toml failed to load; game will run silent");
    });
}

bool DungeonApp::onStartGame(eng::Engine& engine)
{
    mEngine = &engine;
    eng::Renderer& r = engine.renderer();

    if (const char* s = std::getenv("RAVEN_GEN_SEED"))
        mBaseSeed = uint32_t(std::strtoul(s, nullptr, 10));
    mSeeds.assign(1, mBaseSeed);

    eng::Config& cfg = engine.config();
    mSpeed = float(cfg.getNumber("player.speed", 3.0));
    mDashTuning.speed = float(cfg.getNumber("dodge.speed", 14.0));
    mDashTuning.duration = float(cfg.getNumber("dodge.duration", 0.32));
    mDashTuning.cooldown = float(cfg.getNumber("dodge.cooldown", 0.45));
    mDashTuning.cameraDrop = float(cfg.getNumber("dodge.camera_drop", 0.34));
    mDashTuning.cameraRollDegrees =
        float(cfg.getNumber("dodge.camera_roll_degrees", 360.0));
    mDodgeIframes = float(cfg.getNumber("dodge.iframes", 0.22));
    mDodgeStamina = float(cfg.getNumber("dodge.stamina", 25.0));
    mSens = float(cfg.getNumber("player.mouse_sensitivity", 0.002));
    mPlayerBlood = cfg.getString("player.blood", "human");
    mGameAudio.applyUserMix(cfg);

    colliderView().enabled = std::getenv("RAVEN_SHOW_COLLIDERS") != nullptr;

    mCtx.emplace(r, physics(), engine.input(), mVocabulary);

    // Player controller + three data-authored magical ranged weapons.
    //
    // The config values become the same struct a scene authors on its camera,
    // so "the game's defaults" and "this level's override" are one type and
    // one code path (enterLevel picks between them).
    mConfigController.moveSpeed = mSpeed;
    mConfigController.mouseSensitivity = mSens;
    mConfigController.baseFovDegrees =
        float(cfg.getNumber("player.fov", 70.0));
    mConfigController.sprintFovKick =
        float(cfg.getNumber("player.sprint_fov_kick", 4.0));
    mConfigController.bobAmount = float(cfg.getNumber("player.bob_amount", 0.025));
    mConfigController.bobSpeed = float(cfg.getNumber("player.bob_speed", 8.5));

    // The third-person camera and its lock-on. Defaults live in the component
    // and in game::LockOnTuning, so [camera] only has to name what it changes.
    const eng::ecs::ThirdPersonCamera cameraDefaults;
    mConfigCamera.distance =
        float(cfg.getNumber("camera.distance", cameraDefaults.distance));
    mConfigCamera.pivotHeight =
        float(cfg.getNumber("camera.pivot_height", cameraDefaults.pivotHeight));
    mConfigCamera.shoulderOffset = float(
        cfg.getNumber("camera.shoulder_offset", cameraDefaults.shoulderOffset));
    mConfigCamera.followRate =
        float(cfg.getNumber("camera.follow_rate", cameraDefaults.followRate));
    mConfigCamera.followRateVertical = float(cfg.getNumber(
        "camera.follow_rate_vertical", cameraDefaults.followRateVertical));
    mConfigCamera.pitchMinDegrees = float(
        cfg.getNumber("camera.pitch_min_degrees", cameraDefaults.pitchMinDegrees));
    mConfigCamera.pitchMaxDegrees = float(
        cfg.getNumber("camera.pitch_max_degrees", cameraDefaults.pitchMaxDegrees));
    mConfigCamera.mouseSensitivity = float(cfg.getNumber(
        "camera.mouse_sensitivity", cameraDefaults.mouseSensitivity));
    mConfigCamera.collisionRadius = float(
        cfg.getNumber("camera.collision_radius", cameraDefaults.collisionRadius));
    mConfigCamera.pushOutSpeed =
        float(cfg.getNumber("camera.push_out_speed", cameraDefaults.pushOutSpeed));
    mConfigCamera.minDistance =
        float(cfg.getNumber("camera.min_distance", cameraDefaults.minDistance));
    mConfigCamera.turnRateDegrees = float(
        cfg.getNumber("camera.turn_rate_degrees", cameraDefaults.turnRateDegrees));
    mConfigCamera.fovDegrees =
        float(cfg.getNumber("camera.fov", cameraDefaults.fovDegrees));
    mConfigCamera.lockFramingBias = float(
        cfg.getNumber("camera.lock_framing_bias", cameraDefaults.lockFramingBias));
    mConfigCamera.lockBlendRate =
        float(cfg.getNumber("camera.lock_blend_rate", cameraDefaults.lockBlendRate));
    mConfigCamera.lockPitchDegrees = float(
        cfg.getNumber("camera.lock_pitch_degrees", cameraDefaults.lockPitchDegrees));
    mConfigCamera.lockDistanceBoost = float(cfg.getNumber(
        "camera.lock_distance_boost", cameraDefaults.lockDistanceBoost));
    mConfigCameraMode = cfg.getString("camera.mode", "first_person") ==
                                "third_person"
                            ? game::CameraMode::ThirdPerson
                            : game::CameraMode::FirstPerson;
    game::LockOnTuning lock;
    lock.acquireRange =
        float(cfg.getNumber("camera.lock_on.acquire_range", lock.acquireRange));
    lock.breakRange =
        float(cfg.getNumber("camera.lock_on.break_range", lock.breakRange));
    lock.acquireConeDegrees = float(cfg.getNumber(
        "camera.lock_on.acquire_cone_degrees", lock.acquireConeDegrees));
    lock.switchThresholdPixels = float(cfg.getNumber(
        "camera.lock_on.switch_threshold_pixels", lock.switchThresholdPixels));
    lock.occlusionGrace = float(
        cfg.getNumber("camera.lock_on.occlusion_grace", lock.occlusionGrace));
    mPlayerSys.lockOn().setTuning(lock);
    mPlayerSys.setCameraTuning(mConfigCamera);
    mPlayerSys.setControllerTuning(mConfigController);
    mPlayerSys.controller().setDashTuning(mDashTuning);
    mPlayerSys.loadWeapons(game::assetPath("config/weapons.toml"));
    // The hands rig and the socket names weapons hang off. Before the weapons,
    // in load order but not in dependency: a weapon naming a socket this file
    // does not define is warned about when it is equipped, not here.
    mPlayerSys.loadHands(game::assetPath("config/viewmodel_hands.toml"));
    // The RPG layer. After the vocabulary, because item resistance channels
    // resolve against it; before the level, because a level's authored pickups
    // are spawned from its item table.
    mRpgReady = mRpg.load({}, mVocabulary);
    if (mRpgReady)
        mRpg.newGame();
    // Shared first-person rig framing. Read from the file rather than through
    // eng::Config because the socket and rotation are arrays, which the
    // flattened config has no getter for (see eng/Config.h).
    // Feedback tuning, read from the same file for the same reason the rig is:
    // the tier tables are arrays, which the flattened eng::Config has no getter
    // for. A missing [feel] keeps the shipped numbers.
    {
        game::HitFeelTuning tuning = mHitFeel.tuning();
        if (game::loadHitFeelTuningFile(game::assetPath("config/game.toml"),
                                        tuning))
            mHitFeel.setTuning(tuning);
        else
            eng::log::warn("game.toml [feel] was rejected; keeping defaults");
    }
    game::loadViewmodelRig(game::assetPath("config/game.toml"), mConfigRig);
    mPlayerSys.setViewmodelRig(mConfigRig);
    mHud.initialise();
    mHud.configure(cfg);
    mPortalPreviewMode = std::getenv("RAVEN_SHOWCASE_PORTAL") != nullptr ||
                         mScene == "portal";

    // Authored showroom shell. Override it for layout iteration without
    // recompiling; all depth-zero exhibits are positioned relative to its C
    // anchor, which is world origin.
    const char* showroomOverride = std::getenv("RAVEN_SHOWROOM_MAP");
    mShowroomPath =
        showroomOverride ? showroomOverride
                         : game::assetPath("config/showroom.toml");

    // The world comes up before the first level goes into it. Explicit
    // attach-then-sync ordering rather than a constructor: both views need
    // their subsystem alive first, and the order they reconcile in is engine
    // policy (eng::ecs::World).
    mBackend.emplace(engine.renderer());
    mWorld.attachRenderer(*mBackend);
    mWorld.attachPhysics(physics());
    // GameAudio owns the player's listener; authored emitters still reconcile
    // through the World without competing for it.
    mWorld.attachAudio(audio(), /*drivesListener=*/false);

    // Scripting over the same World. mapio::coreRegistry() is the table the
    // serialiser and the editor inspector already share, so every component
    // this game registers is reachable from Lua without a line of binding code.
    eng::script::ScriptConfig scriptConfig;
    scriptConfig.hotReload = true; // dev builds; see docs/scripting.md
    mScripts.emplace(mWorld, scriptConfig, mapio::coreRegistry());
    mScripts->bindInput(engine.input());
    mScripts->bindPhysics(physics());
    eng::script::registerScriptCommands(devConsole(), *mScripts);

    enterLevel(engine, false); // depth 0, spawn at entry

    mCombat.init(*mCtx);
    mCombatReady = true;
    for (const game::PlayerWeaponDef& weapon : mPlayerSys.weaponDefinitions()) {
        if (!mCombat.director().weapons().contains(weapon.payloadId)) {
            eng::log::error("Player weapon '%s' names missing payload '%s'",
                            weapon.id.c_str(), weapon.payloadId.c_str());
            return false;
        }
        const std::string effects[] = {
            weapon.muzzleEffect, weapon.projectile.trailEffect,
            weapon.projectile.impactEffect};
        for (const std::string& effect : effects) {
            if (!effect.empty() && !mParticles.id(effect).valid()) {
                eng::log::error("Player weapon '%s' names missing effect '%s'",
                                weapon.id.c_str(), effect.c_str());
                return false;
            }
        }
    }
    // Let colliding particles see the world. Until this is installed, effects
    // that ask to bounce or to leave a decal fall straight through the level.
    mParticleCollider.emplace(physics(), eng::kAllLayers);
    mCtx->renderer.setParticleCollider(&*mParticleCollider);
    // Token discarded deliberately: this subscription lives as long as the
    // physics world does.
    physics().addContactCallback(
        [this](const eng::HitEvent& e) { mCombat.onContact(*mCtx, e); });
    mCombat.projectiles().setImpactCallback(
        [this](eng::BodyHandle victim, const std::string& payload,
               glm::vec3 direction, glm::vec3 point) {
            // The weapon's own impact cue over the player's Impact row: what a
            // bolt sounds like hitting a wall is a property of the bolt, and
            // the actor's row is what an author reaches for to change every
            // weapon at once.
            std::string_view impact;
            for (const game::PlayerWeaponDef& weapon :
                 mPlayerSys.weaponDefinitions()) {
                if (weapon.payloadId == payload) {
                    impact = weapon.projectile.impactSound;
                    break;
                }
            }
            if (mActorAudio)
                mActorAudio->play(mPlayerEntity, game::ActorAction::Impact,
                                  point, impact);
            playerHit(victim, payload.c_str(), direction, point);
        });

    mProps.spawnShowroom(*mCtx, mLevel.markerPlacements("physics."));

    // Portal proving-ground target: far enough from spawn to establish a
    // readable combat lane, centred so arrows, spells and enchantments are
    // tested against the strongest architectural sightline.
    mDummy.init(physics(), r, glm::vec3(0.0f, 0.0f, -18.0f));
    mDummyAlive = true;

    wireCombatModel();
    // After wireCombatModel: it registers the player entity, which the enemy
    // hit callback needs, and wireEnemies installs the death callback both use.
    wireEnemies();

    // The engine's console owns every engine-tuning tab; the game adds the two
    // that are its own policy.
    mPanels.install(console());
    if (std::getenv("RAVEN_DEBUG_UI")) // start with the console open (testing)
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
    eng::telemetry::event("level", "enter depth " + std::to_string(mDepth));
    // A shake that survived a transition would be the camera shaking for
    // something in a level the player has left, and a hit-stop that survived
    // one would slow the arrival.
    mHitFeel.reset();
    if (mHitStopDrivingClock) {
        engine.gameClock().setScale(1.0f);
        mHitStopDrivingClock = false;
    }
    eng::Renderer& r = engine.renderer();
    // Destroy dynamic prop + enemy + dummy bodies before clearScene wipes their
    // nodes. Enemies go first: their bodies and nodes are both ours, and a node
    // destroyed by clearScene underneath a live enemy is a dangling handle.
    mProps.teardown(physics());
    // The drip nodes belong to the scene that is about to be cleared; drop the
    // handles rather than destroying them one by one through a renderer that is
    // seconds away from wiping the lot.
    mCombat.blood().forgetDrips();
    // Same reason: whatever the particle panel spawned into this level is about
    // to have its nodes wiped, and the panel should not carry the handles into
    // the next one.
    mPanels.releaseParticleSpawns();
    if (mCtx)
        mRpg.pickups().clear(*mCtx);
    if (mCtx)
        mEnemies.clear(*mCtx);
    if (mCtx)
        mCombat.clear(*mCtx);
    mSpawner.clear();
    teardownDummy();
    bool loaded = false;
    if (!mAuthoredMap.empty()) {
        // An authored level replaces the whole progression: there is one map,
        // and descending is not a thing it does. Every depth loads it, so an
        // exit taken during a playtest returns to the level being worked on
        // rather than into a generated dungeon.
        loaded = mLevel.rebuildAuthored(mWorld, r, physics(), mVocabulary, mAuthoredMap,
                                        mDepth);
    } else if (mDepth == 0) {
        if (LevelResource* showroom =
                mLevelContent.load<LevelResource>("showroom", mShowroomPath)) {
            loaded = mLevel.rebuildLayout(mWorld, r, physics(), mVocabulary,
                                          showroom->layout(), mDepth);
        }
        // load() already logged on failure; leaves `loaded` false.
    } else {
        loaded = mLevel.rebuild(mWorld, r, physics(), mVocabulary,
                                mSeeds[size_t(mDepth)], mDepth);
    }
    if (!loaded) {
        eng::log::error("Level %d failed to load", mDepth);
        return;
    }
    mParticles.reregisterAll(r);
    if (mCombatReady)
        mCombat.reloadPresentation(*mCtx);
    // What this level says about the player, falling back to the config for
    // whichever half it did not author. Applied before the spawn, because
    // spawning builds the controller out of the movement numbers.
    {
        const game::MapRuntime::AuthoredPlayerRig authored = mLevel.playerRig();
        // The camera shape first: it decides what the movement numbers below
        // are pushed into, and it has to be chosen before the spawn builds the
        // rig. A level that authors a ThirdPersonCamera plays over the
        // shoulder; one that does not runs on the game's configured default.
        mPlayerSys.setCameraTuning(authored.thirdPerson ? *authored.thirdPerson
                                                        : mConfigCamera);
        mPlayerSys.selectCameraMode(authored.thirdPerson
                                        ? game::CameraMode::ThirdPerson
                                        : mConfigCameraMode);
        mPlayerSys.setControllerTuning(authored.controller ? *authored.controller
                                                           : mConfigController);
        mPlayerSys.setViewmodelRig(authored.viewmodel ? *authored.viewmodel
                                                      : mConfigRig);
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

    // Encounters. Markers named "enemy.<id>[.<preset>]" in the level are the
    // authored placements; spawners.toml carries the presets and the standalone
    // spawn points. Both feed one spawner, so a level author can use either.
    mSpawner.loadFromToml(game::assetPath("config/spawners.toml"));
    {
        std::vector<game::EnemySpawner::Marker> markers;
        for (const game::ScenePlacement& p : mLevel.enemyPlacements()) {
            game::EnemySpawner::Marker m;
            m.type = p.type;
            m.position = p.position;
            m.yaw = glm::yaw(p.rotation);
            m.sounds = p.sounds;
            markers.push_back(std::move(m));
        }
        mSpawner.addFromMarkers(markers);
    }

    // Authored `pickup.<id>` placements become world pickups. Before this,
    // the editor could place an item, SceneCook wrote it into the .map and
    // MapRuntime reported it -- and nothing read that call, so the item simply
    // did not exist. This is the consumer that closes that path.
    if (mRpgReady && mCtx)
        mRpg.setPickupsForLevel(*mCtx, mLevel.pickupPlacements());

    // Depth 0 is the threshold, which is the safehouse side of the loop;
    // anything deeper is a live raid, where the profile may not be written.
    if (mRpgReady) {
        if (mDepth == 0) {
            mRpg.returnToSafehouse();
        } else {
            if (mRpg.raid().phase() == game::rpg::RaidPhase::Safehouse)
                mRpg.beginRaid(mDepth);
            mRpg.enterRaidLevel(mDepth);
        }
    }

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
    mActorAudio.emplace(mGameAudio, mCombat.director().registry());

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
        // The player is an actor like any other, so their foley resolves
        // through the same table an enemy's does -- which is what lets the
        // sound panel tune a footstep without a player-only code path.
        creg.emplace<game::Actor>(mPlayerEntity,
                                  game::Actor{game::ActorKind::Player});
        // The sheet decides how big the pools are; combat stays the authority
        // on what a hit does to them. This writes the derived maxima onto the
        // components just created, preserving each pool's ratio -- so a
        // character with 22 Vigour starts the level with the health that
        // implies rather than the component default.
        if (mRpgReady)
            mRpg.applyToPlayer(creg, mPlayerEntity);

        creg.emplace<game::Stamina>(mDummyEntity);
        creg.emplace<game::ActionState>(mDummyEntity);
        game::Poise& dp = creg.emplace<game::Poise>(mDummyEntity);
        dp.current = dp.max = 20.0f;
    }

    // The death callback is installed by wireEnemies(), which routes the dummy
    // and enemies from one place: the combat model fires one callback for every
    // death, so two callers would mean the second silently replacing the first.

}

std::string DungeonApp::bloodProfileFor(entt::entity e)
{
    if (e == mPlayerEntity)
        return mPlayerBlood;
    const entt::registry& reg = mCombat.director().registry();
    if (const game::EnemyTag* tag = reg.try_get<game::EnemyTag>(e))
        if (tag->def)
            return tag->def->visual.blood;
    return mPlayerBlood; // the training dummy and anything else fleshy
}

void DungeonApp::bleed(entt::entity victim, glm::vec3 point, glm::vec3 dir,
                       const game::DamageResult& result)
{
    const std::string profile = bloodProfileFor(victim);
    const game::Health* health =
        mCombat.director().registry().try_get<game::Health>(victim);
    // A hit carries no surface normal -- neither an impact callback nor a melee
    // arc has one -- so the wound opens back along the blow, which is the
    // direction a wound sprays anyway.
    mCombat.blood().spawnHit(
        mCtx->renderer, profile, point, -dir, dir,
        game::bloodSeverityFor(result.dealt, health ? health->max : 0.0f,
                               result.killed));
    // No floor mark: blood is particles and voxel chunks only now. The spray
    // and the gibs collide and settle where they land, which is what leaves the
    // trace -- so the ground raycast that used to place a decal is gone.
}

void DungeonApp::updateBleeders()
{
    entt::registry& reg = mCombat.director().registry();
    game::BloodSystem& blood = mCombat.blood();
    const auto fraction = [](const game::Health& h) {
        return h.max > 0.0f ? h.current / h.max : 0.0f;
    };
    // Enemies drip from the middle of the body, not from their feet, so the
    // drops have somewhere to fall from and land around them rather than
    // inside the floor. A corpse does not drip: the pool it left is the mark.
    for (auto [e, tag, motion, health] :
         reg.view<game::EnemyTag, game::EnemyMotion, game::Health>().each()) {
        const game::EnemyRender* render = reg.try_get<game::EnemyRender>(e);
        const float height = tag.def ? tag.def->body.height : 1.8f;
        const bool standing = !health.dead() && !(render && render->dying);
        blood.updateDrip(mCtx->renderer, entt::to_integral(e),
                         tag.def ? tag.def->visual.blood : mPlayerBlood,
                         motion.feet + glm::vec3(0.0f, height * 0.5f, 0.0f),
                         standing ? fraction(health) : 0.0f);
    }
    // The player drips too, below the eye so the drops fall through the lower
    // half of the view rather than out of the camera's own position. Wounded
    // and walking leaves a trail, which is the point.
    if (mPlayerEntity != entt::null && reg.valid(mPlayerEntity))
        if (const game::Health* health = reg.try_get<game::Health>(mPlayerEntity))
            blood.updateDrip(mCtx->renderer, entt::to_integral(mPlayerEntity),
                             mPlayerBlood,
                             playerFeet() + glm::vec3(0.0f, 1.1f, 0.0f),
                             health->dead() ? 0.0f : fraction(*health));
}

void DungeonApp::playerHit(eng::BodyHandle body, const char* weaponId,
                           glm::vec3 dir, glm::vec3 point)
{
    if (!body.valid())
        return;
    const entt::entity victim = mCombat.director().entityForBody(body);
    if (victim == entt::null)
        return; // level geometry, a prop, another arrow

    mLastPlayerHitDirection = dir;
    const game::DamageResult result = mCombat.director().hitBody(
        physics(), body, weaponId, mPlayerEntity, dir, point);
    if (!result.hitLanded)
        return;
    mCtx->renderer.spawnParticles("weapon_hit_confirm", point);
    ENG_TELEMETRY("combat",
                  result.killed ? eng::telemetry::Level::Warn
                                : eng::telemetry::Level::Info,
                  "%s hit for %.1f%s%s", weaponId, result.dealt,
                  result.crit ? " CRIT" : "",
                  result.killed ? " (killed)" : "");
    // Heavy when the blow is one that staggers -- the tier is the weapon's own
    // poise damage, so a wand tap and a crossbow bolt differ without this
    // knowing either weapon by name.
    mHitFeel.impact(result.killed ? game::ImpactTier::Kill
                    : mCombat.director().weapons().get(weaponId).timing
                                  .poiseDamage >= 10.0f
                        ? game::ImpactTier::Heavy
                        : game::ImpactTier::Solid);
    // Blood. The profile is the victim's own -- an undead sheds ichor, a
    // construct sheds nothing -- and the severity is what the blow took off it,
    // so a graze spatters and a killing blow gibs without this knowing either
    // effect by name. A projectile impact carries no surface normal, so the
    // wound opens back along the shot, which is the direction a wound sprays.
    bleed(victim, point, dir, result);
    // Feel layer: chip the victim's poise by the weapon's payload so heavy or
    // blunt hits can stagger it, opening the punish window.
    const float poiseDamage =
        mCombat.director().weapons().get(weaponId).timing.poiseDamage;
    if (poiseDamage > 0.0f)
        game::feel::poise::apply(mCombat.director().registry(), victim,
                                 poiseDamage);
    // Hit reaction. A no-op on anything that is not an enemy, which is how the
    // training dummy keeps working without a branch here.
    mEnemies.notifyHit(victim);
    // The victim grunts, whatever the victim is: the cue resolves from its own
    // table, then its type's, then the convention. A hit on a prop or a wall
    // carries no Actor and therefore emits nothing, which is the branch this
    // used to spell as an EnemyTag lookup.
    if (!result.killed && mActorAudio)
        mActorAudio->play(victim, game::ActorAction::Hurt, point);
}

glm::vec3 DungeonApp::playerFeet() const
{
    // FpsController reports the eye; the feet are the standing height below it.
    return mPlayerSys.controller().eyePosition() -
           glm::vec3(0.0f, kPlayerEyeHeight, 0.0f);
}

// Enemies are combatants with a brain, so the wiring is only three seams: the
// combat model tells us what died, we tell the combat model what an enemy hit,
// and hit reactions go back to the enemy that took the hit.
void DungeonApp::wireEnemies()
{
    if (!mEnemyLibrary.load(game::assetPath("config/enemies.toml"))) {
        eng::log::error("enemies.toml failed to load; no enemy can spawn");
        return;
    }
    for (const std::string& id : mEnemyLibrary.ids()) {
        const auto enemy = mEnemyLibrary.find(id);
        if (!enemy)
            continue;
        for (const game::EnemyAttack& attack : enemy->attacks) {
            if (!mCombat.director().weapons().contains(attack.weapon)) {
                eng::log::error("Enemy '%s' attack '%s' names missing payload '%s'",
                                id.c_str(), attack.id.c_str(),
                                attack.weapon.c_str());
                return;
            }
            if (attack.telegraphEffect.empty() ||
                !mParticles.id(attack.telegraphEffect).valid()) {
                eng::log::error(
                    "Enemy '%s' attack '%s' names missing telegraph '%s'",
                    id.c_str(), attack.id.c_str(),
                    attack.telegraphEffect.c_str());
                return;
            }
            if (!attack.pattern)
                continue;
            for (const game::BulletPatternTrack& track :
                 attack.pattern->tracks) {
                if (!mCombat.director().weapons().contains(
                        track.volley.projectileId)) {
                    eng::log::error(
                        "Enemy '%s' pattern '%s' names missing payload '%s'",
                        id.c_str(), attack.pattern->id.c_str(),
                        track.volley.projectileId.c_str());
                    return;
                }
            }
        }
    }
    mEnemyLibrary.resolve(mVocabulary);
    mEnemies.init(mEnemyLibrary, mCombat.director());

    // Every noise an enemy makes by acting -- spawning, noticing you, stepping,
    // landing, swinging -- arrives here as one callback and resolves through
    // one table. The enemy system names the action; what it sounds like is the
    // author's, three overrides deep (this placement, this enemy type, the
    // convention).
    mEnemies.setActionCallback(
        [this](entt::entity enemy, game::ActorAction action, glm::vec3 at) {
            if (mActorAudio)
                mActorAudio->play(enemy, action, at);
        });

    // An enemy landed a hit on the player. It resolves through the same damage
    // pipeline a player hit does -- one weapon table, one resistance model, one
    // set of i-frames -- which is why a dodge's invulnerability works against
    // enemies without anything here knowing what a dodge is.
    mEnemies.setHitPlayerCallback([this](entt::entity enemy,
                                         const std::string& weapon,
                                         float poiseDamage, glm::vec3 dir,
                                         glm::vec3 point) {
        entt::registry& reg = mCombat.director().registry();
        if (mPlayerEntity == entt::null || !reg.valid(mPlayerEntity))
            return;
        // Deflect first: a clean parry negates the hit and punishes the
        // attacker's poise, and that has to happen before damage is applied.
        if (game::feel::defense::resolveIncoming(reg, mPlayerEntity, enemy,
                                                 poiseDamage)) {
            if (mActorAudio)
                mActorAudio->play(mPlayerEntity, game::ActorAction::Block,
                                  point);
            return;
        }
        const game::WeaponDef& wd = mCombat.director().weapons().get(weapon);
        const game::DamagePacket packet =
            wd.makePacket(enemy, dir, 0.5f); // no crits against the player
        const game::DamageResult result =
            game::damage::apply(reg, mPlayerEntity, packet);
            // Being hit outranks landing one: the player has to notice.
            mHitFeel.impact(game::ImpactTier::Heavy);
        if (poiseDamage > 0.0f)
            game::feel::poise::apply(reg, mPlayerEntity, poiseDamage);
        mCtx->renderer.spawnParticles("engine.hit_sparks", point);
        // The player bleeds through the same path an enemy does: the packet
        // reports what actually landed after resistances and i-frames, so a
        // blocked or dodged hit leaves no mark.
        if (result.hitLanded)
            bleed(mPlayerEntity, point, dir, result);
        if (result.hitLanded && mActorAudio) {
            // Two sounds, two actors: the attacker's blow connecting, and the
            // player being hurt by it. They are authored separately because a
            // hit that a heavy armour shrugs off should be able to sound like
            // the armour rather than like the axe.
            mActorAudio->play(enemy, game::ActorAction::Impact, point);
            mActorAudio->play(mPlayerEntity, game::ActorAction::Hurt, point);
        }
    });
    mEnemies.setTelegraphCallback(
        [this](entt::entity enemy, const game::EnemyAttack& attack,
               glm::vec3 at) {
            eng::ParticleSpawnOptions options;
            options.lifetimeScale =
                std::max(0.5f, attack.timing.windup / 0.33f);
            const glm::vec3 chest = at + glm::vec3(0.0f, 1.2f, 0.0f);
            mCtx->renderer.spawnParticles(attack.telegraphEffect, chest,
                                          options);
            // The move's own cue beats the actor's, because a windup is a
            // property of the swing rather than of the swinger -- and an empty
            // one falls through to the actor's Telegraph row rather than
            // needing a branch here.
            if (mActorAudio)
                mActorAudio->play(enemy, game::ActorAction::Telegraph, chest,
                                  attack.telegraphSound);
        });

    // Death: the combat model decides when something is dead; the enemy system
    // turns that into a corpse. Non-enemy entities fall through untouched, so
    // the training dummy's own callback still runs.
    mCombat.director().setDeathCallback([this](entt::entity e) {
        if (e == mDummyEntity) {
            if (!mDummyAlive || !mDummy.alive())
                return;
            glm::vec3 pos;
            glm::quat rot;
            physics().getRenderTransform(mDummy.body(), pos, rot);
            mCtx->renderer.spawnParticles("enemy_death_burst", pos);
            mDummy.kill(physics(), mLastPlayerHitDirection * 8.0f +
                                      glm::vec3(0.0f, 4.0f, 0.0f),
                        pos);
            return;
        }
        // Where the body fell, read before onKilled hands the corpse to the
        // physics world: from that moment the feet the AI was tracking are a
        // ragdoll's and no longer where it died. The death throws one last
        // spray there -- blood is particles and voxels only, so there is no
        // pool decal to grow.
        const entt::registry& reg = mCombat.director().registry();
        const game::EnemyMotion* motion = reg.try_get<game::EnemyMotion>(e);
        const std::string blood = bloodProfileFor(e);
        if (motion && mActorAudio)
            mActorAudio->play(e, game::ActorAction::Death,
                              motion->feet + glm::vec3(0.0f, 0.8f, 0.0f));
        // What the kill was worth: character experience, a skill credit, and
        // whatever its table drops -- all read off the enemy's own definition,
        // so a new enemy type is still just a row in enemies.toml.
        if (mRpgReady && motion) {
            if (const game::EnemyTag* tag =
                    reg.try_get<game::EnemyTag>(e); tag && tag->def) {
                mRpg.onEnemyKilled(*mCtx, tag->def->id, tag->def->xp,
                                   tag->def->loot, motion->feet);
                if (!tag->def->trains.empty())
                    mRpg.train(tag->def->trains, tag->def->xp);
            }
        }
        mCombat.blood().stopDrip(mCtx->renderer, entt::to_integral(e));
        mEnemies.onKilled(*mCtx, e, mLastPlayerHitDirection);
        if (motion)
            mCombat.blood().spawnHit(mCtx->renderer, blood, motion->feet,
                                     glm::vec3(0.0f, 1.0f, 0.0f),
                                     glm::vec3(0.0f, -1.0f, 0.0f),
                                     game::BloodSeverity::Heavy);
    });
}

void DungeonApp::onInput(const eng::FrameContext& f)
{
    // Look runs at the render rate; locomotion runs in onPreSimulate. The base
    // class documents that split; this is the game's half of it.
    if (playerDriven())
        mPlayerSys.look(*mCtx);
    // Live camera swap. One key rather than a menu because the two shapes are
    // meant to be compared: the feel of a camera is not a thing anybody can
    // judge from a screenshot or from a value in a file.
    if (playerDriven() && mCtx->input.wasPressed("camera_toggle"))
        mPlayerSys.setCameraMode(*mCtx,
                                 mPlayerSys.cameraMode() ==
                                         game::CameraMode::FirstPerson
                                     ? game::CameraMode::ThirdPerson
                                     : game::CameraMode::FirstPerson);
    updateLockOn(f);
    mPlayerSys.sampleWeaponInput(
        *mCtx, playerDriven() && mCtx->input.mouseGrabbed());
}

// Who the player is locked onto, once per rendered frame. The candidate list is
// rebuilt every frame on purpose: enemies die, spawn and walk out of range, and
// a lock that outlives its enemy is a camera pointing at a corpse.
void DungeonApp::updateLockOn(const eng::FrameContext& f)
{
    const glm::vec3 eye = mPlayerSys.controller().eyePosition();
    std::vector<game::LockCandidate> candidates;
    for (const game::EnemySystem::Snapshot& s : mEnemies.snapshot(eye)) {
        if (s.health <= 0.0f)
            continue;
        // The torso, not the feet: a camera framing feet points at the floor.
        // A boss is framed a little higher and treated as a bigger thing.
        candidates.push_back({int(entt::to_integral(s.entity)),
                              s.position + glm::vec3(0.0f, s.boss ? 1.4f : 1.0f,
                                                     0.0f),
                              s.boss ? 1.1f : 0.6f});
    }
    game::LockOnSystem::Input input;
    input.enabled = playerDriven() && mCtx->input.mouseGrabbed();
    input.togglePressed = input.enabled &&
                          (mCtx->input.wasPressed("lock_on") ||
                           mCtx->input.wasMousePressed(eng::MouseButton::Middle));
    // A flick of the mouse switches target, which is the souls gesture with the
    // stick. The system decays this, so only a flick counts.
    input.switchAxis = input.enabled ? mCtx->input.mouseDelta().x : 0.0f;
    mPlayerSys.lockOn().update(
        candidates, eye, mPlayerSys.controller().forward(), input, f.realDt,
        [this](glm::vec3 from, glm::vec3 to) {
            const glm::vec3 delta = to - from;
            const float distance = glm::length(delta);
            if (distance < 0.01f)
                return true;
            eng::RayHit hit;
            // Only the level itself blocks a lock. Bodies, props and the
            // target's own collider would each drop it for a frame at a time.
            return !physics().rayCast(from, delta / distance, distance, hit,
                                      config().staticLayers);
        });
    mPlayerSys.applyLockOn(*mCtx);
}

void DungeonApp::onPreSimulate(const eng::FrameContext&, float fixedDt)
{
    // Immediately before the base's Physics::update, which is what makes
    // fixed_update the right place for a script to push a body.
    if (mScripts) mScripts->fixedTick(fixedDt);

    if (playerDriven()) {
        mPlayerSys.fixedStep(*mCtx, fixedDt);
        entt::registry& registry = mCombat.director().registry();
        if (game::Mana* arc = registry.try_get<game::Mana>(mPlayerEntity)) {
            const game::ActionState* action =
                registry.try_get<game::ActionState>(mPlayerEntity);
            const std::optional<std::size_t> fired =
                mPlayerSys.fixedStepWeapons(
                    *mCtx, *arc,
                    action && action->phase == game::ActionPhase::Idle,
                    fixedDt);
            if (fired) {
                if (const game::PlayerWeaponDef* weapon =
                        mPlayerSys.weaponDefinition(*fired)) {
                    const eng::FpsController& player = mPlayerSys.controller();
                    // Aim comes from the player system, not from the view:
                    // under a lock-on the shot goes at the target, and in
                    // third person the boom is metres behind a wall as often
                    // as not.
                    mCombat.fireWeapon(
                        *mCtx, *weapon, mPlayerSys.aimOrigin(),
                        mPlayerSys.aimDirection(),
                        mPlayerSys.projectileMuzzle(mCtx->renderer));
                    // Deliberately NO camera feedback on firing. The
                    // viewmodel already kicks, and it is the loud half by
                    // design (docs/fps-viewmodel.md). Shaking the camera per
                    // shot does not survive an automatic weapon: the talon
                    // fires every 0.09s, so even a small trauma is added
                    // faster than it decays and the view never settles --
                    // which reads as a broken camera, not as a powerful gun.
                    // The weapon's own fire cue over the player's Attack row,
                    // for the same reason the impact one wins: a wand and a
                    // crossbow do not sound alike, and the actor's row is the
                    // blanket the author reaches for when they do.
                    if (mActorAudio)
                        mActorAudio->play(mPlayerEntity,
                                          game::ActorAction::Attack,
                                          player.eyePosition(),
                                          weapon->fireSound);
                }
            }
        }
    }
}

void DungeonApp::onPostSimulate(const eng::FrameContext&, float fixedDt)
{
    // Right after the step that produced them, so a script reacts to a hit in
    // the same frame it happened rather than the next one.
    if (mScripts) mScripts->drainContacts();

    eng::FpsController& player = mPlayerSys.controller();
    mCombat.fixedStep(*mCtx, player.eyePosition(), player.forward(), fixedDt);
    // Strictly after combat: actionstate::advance sets activeFiredThisStep, and
    // the enemy step is what reads it to deliver the swing. Reversing these two
    // lines costs every enemy attack one frame of latency and, worse, lets an
    // attack that started this step deliver on the same step.
    const glm::vec3 feet = playerFeet();
    mSpawner.update(*mCtx, mEnemies, feet, fixedDt);
    mEnemies.fixedStep(*mCtx, feet, playerDriven(), fixedDt);
}

void DungeonApp::onPresent(const eng::FrameContext& f)
{
    eng::Engine& engine = f.engine;
    eng::Renderer& r = engine.renderer();
    eng::Input& in = engine.input();
    eng::StepClock& steps = engine.stepClock();
    eng::FpsController& player = mPlayerSys.controller();

    mGameAudio.setListener(player.eyePosition(), player.forward(), f.realDt);
    game::PlayerFoleyState foley;
    foley.feet = playerFeet();
    foley.horizontalSpeed = player.horizontalSpeed();
    foley.grounded = player.grounded();
    foley.sprinting = player.sprinting();
    // Leaving the ground under your own power. Read from the controller rather
    // than from the jump key so a jump the controller refused (mid-air, no
    // headroom) makes no noise -- the rule the rest of the foley follows.
    if (mActorAudio && mPlayerWasGrounded && !player.grounded() &&
        player.verticalSpeed() > 0.5f)
        mActorAudio->play(mPlayerEntity, game::ActorAction::Jump, foley.feet);
    mPlayerWasGrounded = player.grounded();
    // The player's own sound table decides what a step and a landing are; the
    // foley layer above decides when they happen.
    if (mActorAudio) {
        foley.footstepCue =
            mActorAudio->cueFor(mPlayerEntity, game::ActorAction::Footstep);
        foley.landCue =
            mActorAudio->cueFor(mPlayerEntity, game::ActorAction::Land);
    }
    mGameAudio.updatePlayerFoley(foley);

    game::MusicMixState music;
    entt::registry& combatRegistry = mCombat.director().registry();
    float pressure = 0.0f;
    for (auto [enemy, tag, motion, health] :
         combatRegistry
             .view<const game::EnemyTag, const game::EnemyMotion,
                   const game::Health>()
             .each()) {
        (void)enemy;
        if (health.dead())
            continue;
        const float distance = glm::distance(motion.feet, foley.feet);
        if (distance <= 22.0f)
            pressure += std::clamp(1.0f - distance / 24.0f, 0.15f, 1.0f) *
                        std::clamp(float(tag.def->tier) * 0.22f, 0.2f, 0.8f);
        music.boss = music.boss || tag.def->boss;
    }
    music.threat = std::clamp(pressure, 0.0f, 1.0f);
    if (const game::Health* health =
            combatRegistry.try_get<game::Health>(mPlayerEntity))
        music.playerHealth = health->max > 0.0f
                                 ? std::clamp(health->current / health->max,
                                              0.0f, 1.0f)
                                 : 1.0f;
    mGameAudio.update(f.realDt, music);

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
    if (steps.stepped(eng::StepChannel::Projectiles))
        mEnemies.syncProjectileRender(*mCtx);
    // Per-creature phase seed: with several enemies on screen and phase_jitter
    // above 0 they stop snapping in unison like one puppet.
    if (mDummyAlive && steps.stepped(eng::StepChannel::Characters,
                                     mDummy.body().id))
        mDummy.syncRender(physics(), r);
    // Enemies ride the Characters channel too, so the stop-motion rate that
    // applies to creatures applies to them. Unlike the dummy they are synced as
    // a group: per-enemy phase jitter lives in their own animation, not in
    // whether their transform is copied this frame.
    if (steps.stepped(eng::StepChannel::Characters))
        mEnemies.syncRender(*mCtx);
    // Bleeding is a state, not an event, so it is refreshed every frame from
    // the health the sim just wrote -- not on the stop-motion channel: the
    // emitter has to follow the body smoothly or it strings drops behind it.
    updateBleeders();

    // World anim (torch flicker, animated dressing) is pose-from-time, so it
    // takes the quantised clock directly rather than accumulating a delta -- no
    // drift, and it re-bases correctly if the rate is changed live.
    {
        const auto timed = stats().time(PhaseWorld);
        // Before LiveLevel::update, which ends in World::sync(): everything a
        // script writes this frame is then pushed at the renderer in the same
        // frame it was written.
        if (mScripts) {
            mScripts->pollReload();
            mScripts->tick(f.dt);
        }
        mLevel.update(r, steps.time(eng::StepChannel::World), f.dt);
        mLevel.updateVisibility(r, player.eyePosition());
    }

    // Feedback, before the present that reads it. On the wall clock so a
    // hit-stop can end itself and the shake keeps moving while time is slowed
    // -- a frozen frame with a still camera reads as a hitch, not a punch.
    {
        mHitFeel.update(f.realDt);
        // The clock is only touched while a stop is live, and released exactly
        // once. Driving it every frame would stomp the console's own
        // slow-motion the moment anything was hit.
        const float wanted = mHitFeel.clockScale();
        if (wanted != 1.0f) {
            f.engine.gameClock().setScale(wanted);
            mHitStopDrivingClock = true;
        } else if (mHitStopDrivingClock) {
            f.engine.gameClock().setScale(1.0f);
            mHitStopDrivingClock = false;
        }
        mPlayerSys.controller().setViewShake(mHitFeel.shakeOffset(),
                                             mHitFeel.shakeRotationDegrees());
    }

    // Present only: the simulation for this frame already ran in onFixedStep.
    // When the sim is frozen (portal preview, debug UI) this still applies the
    // camera/FOV tweaks so the debug sliders take effect live.
    {
        const auto timed = stats().time(PhasePlayer);
        mPlayerSys.present(*mCtx, playerDriven() ? f.alpha : 1.0f,
                           f.realDt);
        if (eng::telemetry::enabled("player", eng::telemetry::Level::Trace)) {
            const glm::vec3 eye = mPlayerSys.controller().eyePosition();
            eng::telemetry::watchf("player", "pos", "%.2f %.2f %.2f", eye.x,
                                   eye.y, eye.z);
            eng::telemetry::watchValue("player", "speed",
                                       mPlayerSys.controller().horizontalSpeed());
            eng::telemetry::watchValue("player", "grounded",
                                       mPlayerSys.controller().grounded() ? 1.0
                                                                          : 0.0);
            if (const game::PlayerWeaponDef* weapon =
                    mPlayerSys.selectedWeapon())
                eng::telemetry::watch("player", "weapon", weapon->displayName);
        }
    }

    // Look-interaction + portal transitions. Descend appends the next depth's
    // seed on first visit (so revisits reuse the same layout) and rebuilds;
    // ascend rebuilds the level below.
    // Publish the training dummy as a look target so the crosshair can name
    // it. It is not owned by the level, so it goes in through the external
    // seam rather than through LiveLevel::appendTargets.
    if (mDummyAlive && mDummy.alive()) {
        glm::vec3 feet{0.0f};
        glm::quat unused{1.0f, 0.0f, 0.0f, 0.0f};
        physics().getRenderTransform(mDummy.body(), feet, unused);
        mInteraction.pushTarget({TargetKind::Actor, 0,
                                 feet + glm::vec3(0.0f, 0.9f, 0.0f), 6.0f,
                                 0.55f});
    }
    // Enemies are look targets too, so the crosshair names them and the banner
    // shows their health. The entity id rides in `catalogIndex` -- the field
    // that exists for exactly this, "which record describes me" -- so
    // lookTooltip() gets back to the enemy without a second lookup table.
    for (const game::EnemySystem::Snapshot& s :
         mEnemies.snapshot(player.eyePosition())) {
        if (s.health <= 0.0f)
            continue;
        mInteraction.pushTarget(
            {TargetKind::Actor, int(entt::to_integral(s.entity)),
             s.position + glm::vec3(0.0f, 1.0f, 0.0f), 12.0f, 0.6f,
             int(entt::to_integral(s.entity))});
    }
    // World pickups: their idle bob, and their place in the one aim test.
    // They go in through the external seam for the same reason enemies do --
    // they are not owned by the level.
    if (mRpgReady) {
        mRpg.pickups().update(*mCtx, f.dt);
        std::vector<GameplayTarget> items;
        mRpg.pickups().appendTargets(items);
        for (const GameplayTarget& t : items)
            mInteraction.pushTarget(t);
        // The extraction countdown runs on the real delta: a hit-stop must not
        // buy the player extra time on the threshold.
        mRpg.tickRaid(f.realDt);
    }

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

    // Taking what is under the crosshair. The interaction system resolves the
    // target and stops there for an item, because it does not own an
    // inventory; the app does, so the verb lives here.
    if (mRpgReady && in.mouseGrabbed() &&
        mInteraction.focus().available &&
        mInteraction.focus().kind == TargetKind::Item &&
        in.wasPressed("interact")) {
        mRpg.takePickup(*mCtx, mInteraction.focus().id);
    }

    // Defensive actions remain independent from selected ranged weapon.
    if (in.mouseGrabbed()) {
        // Feel-layer defense. Deflect opens a brief negate window; dodge grants
        // i-frames (reuses Health.invulnTimer) and costs stamina; kick shoves a
        // target in front and chips its poise (env-kills / make-room). Bound to
        // V / B / G in game.toml.
        {
            entt::registry& creg = mCombat.director().registry();
            game::ActionState& pas = creg.get<game::ActionState>(mPlayerEntity);
            if (in.wasPressed("deflect") &&
                game::feel::defense::beginDeflect(pas) && mActorAudio) {
                // The parry going up, not a parry connecting: the negate itself
                // plays Block from the hit path, and hearing the guard raise is
                // what tells the player the window opened.
                mActorAudio->play(mPlayerEntity, game::ActorAction::Block,
                                  player.eyePosition());
            }
            // Preflight combat state before movement. Once both gates pass,
            // beginDodge cannot reject in this single-threaded presentation
            // phase, so movement, stamina cost, action lock and i-frames start
            // as one player-visible transaction.
            if (in.wasPressed("dodge") &&
                game::feel::defense::canDodge(creg, mPlayerEntity,
                                              mDashTuning.duration,
                                              mDodgeIframes,
                                              mDodgeStamina) &&
                player.beginDash(player.inputDirection(
                    eng::FpsController::readCommand(in)))) {
                game::feel::defense::beginDodge(creg, mPlayerEntity,
                                                mDashTuning.duration,
                                                mDodgeIframes, mDodgeStamina);
                if (mActorAudio)
                    mActorAudio->play(mPlayerEntity, game::ActorAction::Dodge,
                                      player.eyePosition());
            }
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

    // Player viewmodel (hands/weapon) — the player's half of the look. Its own
    // channel because it sits centimetres from the eye, where the rate that
    // suits a distant creature reads about twice as harsh. Camera and movement
    // stay full-rate: the weapon animation is stepped, the view is not.
    {
        const auto timed = stats().time(PhaseWeapons);
        // Two deltas on purpose: the authored clips snap on the stepped
        // channel above, while the rig's own bob/sway/recoil runs at frame
        // rate. Stepping the placement as well reads as input lag this close
        // to the eye -- the same reason the camera is never stepped.
        mPlayerSys.updateViewmodels(
            *mCtx, steps.delta(eng::StepChannel::Viewmodel), f.dt);
    }

    if (std::getenv("RAVEN_PROFILE"))
        stats().logSummaryEvery(120);
}

eng::ui::TooltipContent DungeonApp::lookTooltip()
{
    const game::InteractionFocus& focus = mInteraction.focus();
    if (!focus.available)
        return {};

    const game::PropInfo* prop =
        focus.kind == TargetKind::Prop
            ? mLevel.dungeon().propInfo(focus.catalogIndex)
            : nullptr;

    // An item under the crosshair, flattened for the tooltip. The builder never
    // sees an ItemLibrary; it is handed values.
    game::PickupLook pickup;
    if (mRpgReady && focus.kind == TargetKind::Item) {
        if (const game::rpg::PickupSystem::Entry* entry =
                mRpg.pickups().find(focus.id)) {
            if (const game::rpg::ItemLibrary::Ref def =
                    mRpg.items().find(entry->item)) {
                pickup.valid = true;
                pickup.name = def->name;
                pickup.description = def->description;
                pickup.rarity = game::rpg::nameOf(def->rarity);
                pickup.count = entry->count;
                char category[96];
                std::snprintf(category, sizeof(category), "%s - %.1f kg",
                              game::rpg::nameOf(def->category), def->weight);
                pickup.category = category;
                pickup.takeable = mRpg.canTake(focus.id);
            }
        }
    }

    game::ActorLook actor;
    // Enemy targets carry their entity id in catalogIndex (see onPresent); a
    // zero index is the training dummy, which predates enemies and has none.
    // An enemy carries its entity in catalogIndex; the training dummy predates
    // that and carries -1, so the EnemyTag check below is what tells them
    // apart rather than a sentinel id.
    if (focus.kind == TargetKind::Actor && focus.catalogIndex >= 0) {
        const entt::entity e = entt::entity(uint32_t(focus.catalogIndex));
        const entt::registry& reg = mCombat.director().registry();
        if (reg.valid(e) && reg.all_of<game::EnemyTag>(e)) {
            const game::EnemyTag& tag = reg.get<game::EnemyTag>(e);
            actor.valid = true;
            actor.name = tag.def->name;
            actor.category = tag.def->category;
            if (const auto* hp = reg.try_get<game::Health>(e)) {
                actor.health = hp->current;
                actor.healthMax = hp->max;
            }
        }
        return game::buildTooltip(focus, prop, actor, mHud.interactKey(),
                                  pickup);
    }
    if (focus.kind == TargetKind::Actor && mDummyAlive && mDummy.alive()) {
        actor.valid = true;
        actor.name = "Training Effigy";
        actor.category = "Construct - armoured, flammable";
        const entt::registry& registry = mCombat.director().registry();
        if (mDummyEntity != entt::null &&
            registry.all_of<game::Health>(mDummyEntity)) {
            const game::Health& hp = registry.get<game::Health>(mDummyEntity);
            actor.health = hp.current;
            actor.healthMax = hp.max;
        }
    }

    return game::buildTooltip(focus, prop, actor, mHud.interactKey(), pickup);
}

void DungeonApp::onGameGui(const eng::FrameContext& f)
{
    // The combat registry and the player entity are recreated on every level
    // transition, so the panels get fresh pointers each frame rather than
    // capturing them at install time.
    game::DebugPanels::Deps deps;
    deps.registry = &mCombat.director().registry();
    deps.player = mPlayerEntity;
    deps.playerSystem = &mPlayerSys;
    deps.renderer = &f.engine.renderer();
    deps.enemies = &mEnemies;
    deps.enemyLibrary = &mEnemyLibrary;
    deps.spawner = &mSpawner;
    deps.context = mCtx ? &*mCtx : nullptr;
    deps.level = &mLevel;
    deps.particles = &mParticles;
    deps.audio = &mGameAudio;
    deps.playerFeet = playerFeet();
    deps.playerForward = mPlayerSys.controller().forward();
    deps.dt = f.dt;
    mPanels.update(deps);
    const game::HudSnapshot hudFrame =
        game::buildHudSnapshot(mCombat.director().registry(), mPlayerEntity,
                               mPlayerSys.selectedWeapon(),
                               mInteraction.focus());
    mHud.draw(hudFrame, lookTooltip(), f.realDt,
              !uiOpen() && !mPortalPreviewMode);


    // renderFrame() (owned by the runner) paints all of the above; it is
    // full-rate so particles + Ogre anim stay smooth.
}

void DungeonApp::onStopGame(eng::Engine&)
{
    mGameAudio.stopAll(eng::StopMode::Immediate);
    // Remove dynamic prop bodies before shutdown (nodes are owned by Ogre/scene).
    mProps.teardown(physics());
    if (mCtx)
        mRpg.pickups().clear(*mCtx);
    if (mCtx)
        mEnemies.clear(*mCtx);
    teardownDummy();
    mLevel.clearPhysics();
    if (mCtx)
        mCombat.clear(*mCtx);
    // Nodes and bodies go before the renderer and the physics world they came
    // from; the registry itself outlives both.
    // Before the world and physics go: the host holds a contact subscription on
    // one and an on_destroy hook on the other.
    mScripts.reset();
    mWorld.detachAll();
    mBackend.reset();
}

} // namespace

int main(int argc, char** argv)
{
    // Dev self-test: RAVEN_GEN_DUMP=<seed> prints a generated grid and exits,
    // no window/Ogre. Eyeball connectivity + room shapes across seeds.
    if (const char* dump = std::getenv("RAVEN_GEN_DUMP")) {
        const auto grid = gen::generate(uint32_t(std::strtoul(dump, nullptr, 10)));
        for (const std::string& row : grid.rows())
            std::printf("%s\n", row.c_str());
        return 0;
    }

    // Which loop plays a `.map`, and why the scene itself decides.
    //
    //   the full game     the map is a level: enemies, weapons, HUD, the FPS
    //                     player. "Does this room work" is a question about
    //                     those, which is why it is the default.
    //   the scene loop    the map is a *shot* or a blockout: no combat, and an
    //                     authored Camera drives the view.
    //
    // The scene loop used to require `--walk`, which meant an authored camera
    // was silently ignored unless you knew to pass a flag whose name says the
    // opposite of what it does -- so the last step of "model -> material ->
    // scene -> camera -> run it" failed with no message at all.
    //
    // Now a map that authored a camera picks the scene loop by itself. That is
    // the honest signal: placing a camera in a scene *is* saying "look through
    // this", and nothing else in the game does. `--walk` still forces the scene
    // loop for a map with no camera (a blockout you only want to walk), and
    // `--play` forces the full game for one that has one.
    bool forceScene = false;
    bool forceGame = false;
    std::string mapArg;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        forceScene = forceScene || arg == "--walk" || arg == "--scene";
        forceGame = forceGame || arg == "--play";
        if (arg.size() > 4 && arg.substr(arg.size() - 4) == ".map")
            mapArg = arg;
    }
    if (!mapArg.empty() && !forceGame &&
        (forceScene || game::mapHasCamera(mapArg))) {
        return game::runMap(mapArg, argc, argv);
    }

    DungeonApp app;
    return eng::runApplication(app, argc, argv);
}
