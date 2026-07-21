// dungeon-crawler: FPS walk through a procedurally generated PSX dungeon
// (DungeonGen -> DungeonMap), with the shared demo scene (crystals, chest,
// sparkles, light shaft) sitting in the generated level's anchor room.
// A whole level is built by buildLevel() into a Level bundle; level
// transitions (portals) clear the scene and rebuild.

#include "DungeonGen.h"
#include "DungeonMap.h"
#include "FpsController.h"

#include <DemoScene.h>

#include <imgui.h>

#include <eng/Engine.h>
#include <eng/Log.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// Per-scene palette + shader grade (verdigris). Must re-run after every
// scene rebuild, so it lives in its own function called from buildLevel.
static void applyPalette(eng::Renderer& r, const DemoScene& scene)
{
    const auto lin = [](float srgb) { return std::pow(srgb, 2.2f); };
    // Faint verdigris ambient: green-tinted so unlit stone reads mossy and
    // torch pools read warm by contrast.
    r.setAmbient({lin(0.35f) * 0.06f, lin(0.50f) * 0.06f, lin(0.38f) * 0.06f});
    // Sun becomes a weak sickly-green spill through cracks: steep top-down
    // so it varies wall shading, dim enough to never fight the torches.
    r.setOrientation(scene.sunNode(),
                     glm::angleAxis(glm::radians(30.0f), glm::vec3(0, 1, 0)) *
                         glm::angleAxis(glm::radians(-75.0f), glm::vec3(1, 0, 0)));
    r.setLightColour(scene.sunLight(),
                     {lin(0.40f) * 0.22f, lin(0.55f) * 0.22f, lin(0.42f) * 0.22f});
    // Fog and backdrop: swamp green-black murk, denser so corridors drown
    // roughly two cells out. Background matches the fog chromaticity so
    // silhouettes have no seam against the void.
    r.setFog({lin(0.04f), lin(0.07f), lin(0.05f)}, 0.12f);
    r.setBackground({0.012f, 0.021f, 0.015f});
    // Additive shaft: faint ghost-light in the murk.
    r.setMaterialParam("PSX/LightShaft", "modulateColor",
                       glm::vec4(0.85f, 1.0f, 0.9f, 0.16f));
    // Verdigris shader defaults: banded torch pools, distance desaturation,
    // palette-unifying grade (all live-tunable in the debug panel).
    r.setLightSteps(4.0f);
    r.setFogDesatBoost(0.4f);
    r.setGradeEnabled(true);
    // Stylize outlines tinted to the palette: green-black shadows, warm
    // parchment highlights (raw sRGB, mixed post-encode by the pass).
    r.setMaterialParam("PSX/PixelStylize", "shadowColor",
                       glm::vec3(0.03f, 0.07f, 0.035f));
    r.setMaterialParam("PSX/PixelStylize", "shadowStrength", 0.45f);
    r.setMaterialParam("PSX/PixelStylize", "highlightColor",
                       glm::vec3(0.94f, 0.88f, 0.72f));
    r.setMaterialParam("PSX/PixelStylize", "highlightStrength", 0.12f);
    // Torch-only bloom: only flames/embers/singularity cross the threshold.
    r.setBloomParams(0.85f, 0.6f);
}

// Everything a built level owns that the main loop animates or references.
// Swapped atomically on a transition (clearScene + buildLevel).
struct Level {
    DungeonMap map;
    DemoScene scene;
    eng::NodeHandle chestBase{}, chestSpin{};
    eng::LightHandle chestGlow{};
    glm::vec3 chestGlowColour{0.0f};
    glm::vec3 spawn{0.0f}, exit{0.0f};
    eng::NodeHandle downPortal{};
    eng::NodeHandle upPortal{}; // invalid at depth 0
};

// Build a complete level (dungeon + demo scene + props + chest + portals)
// into the (already-clear) scene. depth>0 adds an up-portal at the entry.
static Level buildLevel(eng::Renderer& r, const std::string& assets,
                        uint32_t seed, int depth)
{
    Level lv;

    // --------------------------------------------------------- dungeon ---
    // Procedurally generated level; the anchor 'C' room lands at the world
    // origin so the shared DemoScene sits centred inside it.
    if (!lv.map.loadFromRows(r, gen::generate(seed), assets + "/meshes/tiles/",
                             assets + "/meshes/props/")) {
        eng::log::error("buildLevel: map load failed");
        return lv;
    }
    lv.spawn = lv.map.spawn();
    lv.exit = lv.map.exitPos();

    // ------------------------------------------------------ shared scene ---
    DemoScene::Options sceneOpts;
    sceneOpts.crystals = true;     // glassy spires ring the treasure shrine
    sceneOpts.boxes = false;       // movers replaced by the treasure chest
    lv.scene.load(r, DEMO_SCENE_TOML, assets + "/meshes/", eng::kRootNode,
                  sceneOpts);

    applyPalette(r, lv.scene);

    // ------------------------------------------------- set dressing ---
    // Medieval props placed around the anchor room (positions authored for
    // the shared centrepiece layout at the world origin).
    {
        const std::string props = assets + "/meshes/props/";
        const auto mesh = [&](const char* f) { return r.loadObj(props + f); };
        const auto place = [&](eng::MeshHandle m, const char* mat,
                               glm::vec3 pos, float yawDeg,
                               glm::vec3 scale = glm::vec3(1.0f),
                               bool cast = true) {
            eng::NodeHandle n = r.createNode(eng::kRootNode, pos);
            if (yawDeg != 0.0f)
                r.setOrientation(n, glm::angleAxis(glm::radians(yawDeg),
                                                   glm::vec3(0, 1, 0)));
            if (scale != glm::vec3(1.0f))
                r.setScale(n, scale);
            r.attachMesh(n, m, mat, cast);
            return n;
        };
        const auto place2 = [&](eng::MeshHandle m0, const char* mat0,
                                eng::MeshHandle m1, const char* mat1,
                                glm::vec3 pos, float yawDeg,
                                bool cast = true) {
            eng::NodeHandle n = place(m0, mat0, pos, yawDeg,
                                      glm::vec3(1.0f), cast);
            r.attachMesh(n, m1, mat1, cast);
            return n;
        };
        const glm::vec3 noScale{1.0f};

        eng::MeshHandle barrel0 = mesh("prop_barrel_p0.obj");
        eng::MeshHandle barrel1 = mesh("prop_barrel_p1.obj");
        eng::MeshHandle crate = mesh("prop_crate.obj");
        eng::MeshHandle sack = mesh("prop_jutesack.obj");
        eng::MeshHandle hay = mesh("prop_haybale.obj");
        eng::MeshHandle pumpkin = mesh("prop_pumpkin.obj");
        eng::MeshHandle vase0 = mesh("prop_vase_p0.obj");
        eng::MeshHandle vase1 = mesh("prop_vase_p1.obj");

        // --- entry hall (z ~ -24): a market table greets the player.
        eng::NodeHandle table = place2(
            mesh("prop_table_p0.obj"), "Game/PropWood",
            mesh("prop_table_p1.obj"), "Game/PropMarket",
            {7.0f, 0.88f, -24.5f}, -60.0f, false);
        r.attachMesh(r.createNode(table, {0.3f, 0.53f, -0.2f}),
                     mesh("prop_bread.obj"), "Game/PropMarketMisc");
        r.attachMesh(r.createNode(table, {-0.35f, 0.59f, 0.25f}), pumpkin,
                     "Game/PropMarketMisc");
        place2(vase0, "Game/PropTerracotta", vase1, "Game/PropPlanks",
               {-8.5f, 0.0f, -25.0f}, 30.0f);
        place(sack, "Game/PropJute", {-5.0f, 0.0f, -22.8f}, 100.0f);

        // --- great hall corners (x +-10, z +-6 interior)
        place2(barrel0, "Game/PropPlanks", barrel1, "Game/PropBauerhaus",
               {-8.8f, 0.0f, 4.5f}, 15.0f);
        place2(barrel0, "Game/PropPlanks", barrel1, "Game/PropBauerhaus",
               {-9.0f, 0.0f, 3.4f}, 80.0f);
        {
            const glm::vec3 c{-9.0f, 0.0f, -4.5f};
            place(crate, "Game/PropMarket", c, 10.0f, noScale, false);
            place(crate, "Game/PropMarket", c + glm::vec3(0, 0.24f, 0), -25.0f,
                  noScale, false);
            place(crate, "Game/PropMarket", c + glm::vec3(0, 0.48f, 0), 40.0f,
                  noScale, false);
        }
        place(hay, "Game/PropHay", {8.7f, 0.0f, 4.4f}, 20.0f, noScale, false);
        place(hay, "Game/PropHay", {7.6f, 0.0f, 5.0f}, -60.0f, noScale, false);
        place(pumpkin, "Game/PropMarketMisc", {9.2f, 0.08f, 3.4f}, 0.0f);
        place2(vase0, "Game/PropTerracotta", vase1, "Game/PropPlanks",
               {8.8f, 0.0f, -4.5f}, 0.0f);
        place(sack, "Game/PropJute", {9.1f, 0.0f, -3.4f}, -15.0f);

        // --- vault (z ~ 18..26): sword stabbed into the floor, shield on a
        // barrel. The weapons-pack meshes are authored huge; scale down.
        {
            eng::NodeHandle sword =
                r.createNode(eng::kRootNode, {0.0f, 1.15f, 24.0f});
            r.setScale(sword, glm::vec3(0.06f));
            r.setOrientation(sword,
                             glm::angleAxis(glm::radians(168.0f),
                                            glm::vec3(0, 0, 1)) *
                                 glm::angleAxis(glm::radians(8.0f),
                                                glm::vec3(1, 0, 0)));
            r.attachMesh(sword, mesh("prop_sword.obj"), "Game/PropWeapon");

            const glm::vec3 b{-4.0f, 0.0f, 24.2f};
            place2(barrel0, "Game/PropPlanks", barrel1, "Game/PropBauerhaus",
                   b, 0.0f);
            eng::NodeHandle shield =
                r.createNode(eng::kRootNode, b + glm::vec3(0.0f, 0.55f, -0.75f));
            r.setScale(shield, glm::vec3(0.08f));
            r.setOrientation(shield,
                             glm::angleAxis(glm::radians(180.0f),
                                            glm::vec3(0, 1, 0)) *
                                 glm::angleAxis(glm::radians(-20.0f),
                                                glm::vec3(1, 0, 0)));
            r.attachMesh(shield, mesh("prop_shield.obj"), "Game/PropWeapon");
        }
        place2(mesh("prop_barrel_open_p0.obj"), "Game/PropPlanksTwoSided",
               mesh("prop_barrel_open_p1.obj"), "Game/PropBauerhausTwoSided",
               {4.2f, 0.0f, 24.0f}, -30.0f);

        // --- braziers: ground the demo's two omni lamps in open barrels with
        // a fire on the rim and lift the light just above the flames.
        {
            eng::MeshHandle brz0 = mesh("prop_barrel_open_p0.obj");
            eng::MeshHandle brz1 = mesh("prop_barrel_open_p1.obj");
            const auto& omnis = lv.scene.omniNodes();
            const float xs[2] = {-4.0f, 4.0f};
            for (size_t i = 0; i < omnis.size() && i < 2; ++i) {
                place2(brz0, "Game/PropPlanksTwoSided", brz1,
                       "Game/PropBauerhausTwoSided",
                       {xs[i], 0.0f, 0.0f}, i == 0 ? 25.0f : -40.0f);
                eng::NodeHandle flame =
                    r.createNode(eng::kRootNode, {xs[i], 1.35f, 0.0f});
                r.attachParticles(flame, "Game/TorchFire");
                r.attachParticles(flame, "Game/TorchAsh");
                r.setPosition(omnis[i], {xs[i], 1.6f, 0.0f});
            }
        }
        {
            const glm::vec3 c{-4.5f, 0.0f, 20.0f};
            place(crate, "Game/PropMarket", c, -20.0f, noScale, false);
            place(crate, "Game/PropMarket", c + glm::vec3(0, 0.24f, 0), 15.0f,
                  noScale, false);
        }
    }

    // ------------------------------------------- hall centrepiece ---
    // Treasure shrine: a low-poly chest levitating over the origin (anchor
    // room centre), ringed by the demo's crystal spires + offering clutter,
    // with a warm gold spill that pulses like banked coals.
    lv.chestGlowColour = glm::vec3(1.0f, 0.62f, 0.22f) * 1.6f;
    {
        const std::string props = assets + "/meshes/props/";
        eng::MeshHandle vase0 = r.loadObj(props + "prop_vase_p0.obj");
        eng::MeshHandle vase1 = r.loadObj(props + "prop_vase_p1.obj");
        eng::MeshHandle sack = r.loadObj(props + "prop_jutesack.obj");
        for (int i = 0; i < 5; ++i) {
            const float a = glm::radians(72.0f * float(i) + 56.0f);
            const glm::vec3 pos{3.2f * std::sin(a), 0.0f, 3.2f * std::cos(a)};
            eng::NodeHandle n = r.createNode(eng::kRootNode, pos);
            r.setOrientation(n, glm::angleAxis(a, glm::vec3(0, 1, 0)));
            if (i % 2 == 0) {
                r.attachMesh(n, vase0, "Game/PropTerracotta", true);
                r.attachMesh(n, vase1, "Game/PropPlanks", true);
            } else {
                r.attachMesh(n, sack, "Game/PropJute", true);
            }
        }

        lv.chestBase = r.createNode(eng::kRootNode, {0.0f, 1.35f, 0.0f});
        lv.chestSpin = r.createNode(lv.chestBase);
        r.setScale(lv.chestSpin, glm::vec3(6.0f));
        r.attachMesh(lv.chestSpin, r.loadObj(props + "prop_chest.obj"),
                     "Game/PropChest", true);
        r.attachParticles(lv.chestBase, "PSX/Sparkles");
        eng::LightDesc glow;
        glow.colour = lv.chestGlowColour;
        glow.range = 6.0f;
        lv.chestGlow = r.attachLight(lv.chestBase, glow);
    }

    // Portals: emissive additive cubes as waist-high floor markers. The
    // player spawns ON the portal cell (up on descend, down on ascend), so a
    // small low cube reads as a glow at their feet rather than engulfing the
    // camera; from across a room it's a visible landmark to aim at.
    {
        eng::MeshHandle cube = r.createInteriorBox(0.8f, 0);
        lv.downPortal = r.createNode(eng::kRootNode,
                                     lv.exit + glm::vec3(0.0f, 0.4f, 0.0f));
        r.attachMesh(lv.downPortal, cube, "Game/PortalDown");
        if (depth > 0) {
            lv.upPortal = r.createNode(eng::kRootNode,
                                       lv.spawn + glm::vec3(0.0f, 0.4f, 0.0f));
            r.attachMesh(lv.upPortal, cube, "Game/PortalUp");
        }
    }
    return lv;
}

// Returns +1 if the player is aiming at the down portal, -1 at the up portal,
// 0 at neither (mirrors DungeonMap::findTorch: within maxDist, ~25 deg of view).
static int aimedPortal(const Level& lv, glm::vec3 eye, glm::vec3 forward,
                       float maxDist, bool hasUp)
{
    const auto looking = [&](glm::vec3 target) {
        const glm::vec3 to = target - eye;
        const float d = glm::length(to);
        return d > 1e-3f && d <= maxDist && glm::dot(to / d, forward) >= 0.9f;
    };
    if (looking(lv.exit + glm::vec3(0.0f, 0.4f, 0.0f)))
        return 1;
    if (hasUp && looking(lv.spawn + glm::vec3(0.0f, 0.4f, 0.0f)))
        return -1;
    return 0;
}

int main(int, char**)
{
    // Dev self-test: PSX_GEN_DUMP=<seed> prints a generated grid and exits,
    // no window/Ogre. Eyeball connectivity + room shapes across seeds.
    if (const char* dump = std::getenv("PSX_GEN_DUMP")) {
        const auto grid = gen::generate(uint32_t(std::strtoul(dump, nullptr, 10)));
        for (const std::string& row : grid)
            std::printf("%s\n", row.c_str());
        return 0;
    }

    eng::Engine engine;
    const std::string assets = APP_ASSET_DIR;
    if (!engine.init(assets + "/game.toml", assets))
        return 1;
    eng::Renderer& r = engine.renderer();

    r.setCameraFov(70.0f);
    // Far clip rides the fog: at 30 m the exponential murk (density 0.12)
    // passes ~2.7 % and the background matches the fog colour, so the far
    // plane is invisible; regions beyond it frustum-cull entirely.
    r.setCameraClip(0.05f, 30.0f);

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

    Level level;
    FpsController player;

    // Wipe the scene, build the level at `depth`, and (re)spawn the player.
    // atExit spawns at the down-portal (arrived by ascending); else at entry.
    const auto enterLevel = [&](bool atExit) {
        r.clearScene();
        level = buildLevel(r, assets, seeds[size_t(depth)], depth);
        const glm::vec3 p = atExit ? level.exit : level.spawn;
        player.init(r, p, speed, sens, glm::vec3(-1000.0f), glm::vec3(1000.0f));
        player.setResolver([&level](glm::vec3 from, glm::vec3 to) {
            return level.map.resolveMove(from, to, 0.35f);
        });
        // Carried light rides the fresh head node (the old one was destroyed).
        eng::LightDesc carry;
        carry.colour = glm::vec3(std::pow(0.55f, 2.2f), std::pow(0.65f, 2.2f),
                                 std::pow(0.55f, 2.2f)) * 0.8f;
        carry.range = 5.0f;
        r.attachLight(player.headNode(), carry);
        engine.input().setMouseGrab(true);
    };
    enterLevel(false); // depth 0, spawn at entry

    engine.debugUi().addPanel("Player", [&player] {
        ImGui::SliderFloat("move speed", &player.speed(), 0.5f, 15.0f);
        ImGui::SliderFloat("mouse sensitivity", &player.sensitivity(), 0.0005f,
                           0.01f, "%.4f");
    });

    // ---------------------------------------------------------------- loop ---
    float animTime = 0.0f;
    while (!engine.shouldClose()) {
        const float dt = engine.tick();
        eng::Input& in = engine.input();
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

        animTime += dt;
        level.scene.update(r, animTime);
        level.map.update(r, animTime); // torch flicker
        level.map.updateVisibility(r, player.eyePosition(), 30.0f);

        // Levitating loot: slow bob + steady yaw turn; glow pulses like
        // banked coals (two-sine breathing, never below ~0.8x).
        r.setPosition(level.chestBase,
                      {0.0f, 1.35f + 0.25f * std::sin(animTime * 0.9f), 0.0f});
        r.setOrientation(level.chestSpin,
                         glm::angleAxis(animTime * 0.8f, glm::vec3(0, 1, 0)));
        const float pulse = 0.9f + 0.1f * std::sin(animTime * 1.7f) +
                            0.05f * std::sin(animTime * 4.3f);
        r.setLightColour(level.chestGlow, level.chestGlowColour * pulse);

        player.update(in, r, dt);

        // Torch interaction: aim at a torch within reach -> HUD prompt;
        // E toggles the flame + light.
        const int aimed =
            level.map.findTorch(player.eyePosition(), player.forward(), 2.5f);
        if (aimed >= 0) {
            engine.debugUi().setHudPrompt(level.map.torchLit(aimed)
                                              ? "Press [E] to snuff the torch"
                                              : "Press [E] to light the torch");
            if (in.wasPressed("interact"))
                level.map.toggleTorch(r, aimed);
        } else {
            // Portal interaction (when not already prompting a torch).
            const int portal = aimedPortal(level, player.eyePosition(),
                                           player.forward(), 3.0f, depth > 0);
            if (portal > 0) {
                engine.debugUi().setHudPrompt("Press [E] to descend");
                if (in.wasPressed("interact")) {
                    if (depth + 1 == int(seeds.size()))
                        seeds.push_back(baseSeed +
                                        uint32_t(depth + 1) * 0x9E3779B9u);
                    ++depth;
                    enterLevel(false); // arrive at the new level's entry
                }
            } else if (portal < 0) {
                engine.debugUi().setHudPrompt("Press [E] to ascend");
                if (in.wasPressed("interact")) {
                    --depth;
                    enterLevel(true); // arrive at the down-portal you left
                }
            } else {
                engine.debugUi().setHudPrompt("");
            }
        }

        engine.renderFrame(dt);
    }
    engine.shutdown();
    return 0;
}
