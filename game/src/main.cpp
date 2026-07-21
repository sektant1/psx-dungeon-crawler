// dungeon-crawler scaffold: FPS walk through a modular PSX dungeon
// (DungeonMap + dungeon.toml), with the demo scene (crystals, boxes,
// sparkles, light shaft) shared via samples/common/DemoScene +
// demo_scene.toml sitting in the great hall. Game-specific lighting
// overrides are applied after the scene loads.

#include "DungeonGen.h"
#include "DungeonMap.h"
#include "FpsController.h"

#include <DemoScene.h>

#include <imgui.h>

#include <eng/Engine.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

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
    // plane is invisible; regions beyond it frustum-cull entirely. Also
    // tightens the MRT depth encode for the stylize/ink pass.
    r.setCameraClip(0.05f, 30.0f);

    // --------------------------------------------------------- dungeon ---
    // Modular dungeon from the PSX_Modular_Medieval tile set, laid out in
    // dungeon.toml. The 'C' cell (great hall centre) sits at the world
    // origin so the shared DemoScene lands inside the hall.
    DungeonMap dungeon;
    if (!dungeon.load(r, assets + "/dungeon.toml", assets + "/meshes/tiles/",
                      assets + "/meshes/props/"))
        return 1;

    // ------------------------------------------------------ shared scene ---
    // Sun parented to the root: static here, orbiting in the demo.
    DemoScene scene;
    DemoScene::Options sceneOpts;
    sceneOpts.crystals = true;     // glassy spires ring the treasure shrine
    sceneOpts.boxes = false;       // movers replaced by the treasure chest
    if (!scene.load(r, DEMO_SCENE_TOML, assets + "/meshes/", eng::kRootNode,
                    sceneOpts))
        return 1;

    // ------------------------------------------- game lighting overrides ---
    // Dark-fantasy grade: the dungeon is torch-lit. The demo's daylight
    // palette (bright sun, blue fog, high ambient) is replaced wholesale --
    // cold near-black ambience, warm light only where torches burn.
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

    // ------------------------------------------------- set dressing ---
    // Medieval props (PSX_Modular_Medieval + medievalweaponspack
    // conversions in meshes/props) placed per room of dungeon.toml.
    // Cell centres: x = (col-3)*4, z = (row-7)*4 ('C' is col 3, row 7).
    {
        const std::string props = assets + "/meshes/props/";
        const auto mesh = [&](const char* f) { return r.loadObj(props + f); };
        // Props cast stencil shadows (kit meshes are closed enough to
        // extrude); the dungeon tiles themselves only receive.
        // 'cast' opts a prop into stencil shadows. Only closed-ish meshes
        // qualify -- open sheet meshes (crate, haybale, table top) extrude
        // broken shadow volumes that smear dark patches over the walls.
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
        // Two-primitive props share one node (per-primitive OBJ split).
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
        // Table origin sits at the leg tops (legs span y in [-0.88, 0]).
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
        // Pumpkins bulge 0.08 below their origin; lift onto the floor.
        place(pumpkin, "Game/PropMarketMisc", {9.2f, 0.08f, 3.4f}, 0.0f);
        place2(vase0, "Game/PropTerracotta", vase1, "Game/PropPlanks",
               {8.8f, 0.0f, -4.5f}, 0.0f);
        place(sack, "Game/PropJute", {9.1f, 0.0f, -3.4f}, -15.0f);

        // --- vault (z ~ 18..26): the loot room. Sword stabbed into the
        // floor, shield leaning on a barrel. The weapons-pack meshes are
        // authored huge; scale to roughly life size.
        {
            eng::NodeHandle sword =
                r.createNode(eng::kRootNode, {0.0f, 1.15f, 24.0f});
            r.setScale(sword, glm::vec3(0.06f));
            // Blade is modelled pointing +y; roll past 180 so it points
            // down with a slight stuck-in-the-dirt lean.
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

        // --- great hall braziers: the demo's two omni lamps floated in
        // mid-air; ground each in an open barrel with a fire on the rim
        // and lift the light just above the flames. (The animated cubes
        // stay as they are.)
        {
            eng::MeshHandle brz0 = mesh("prop_barrel_open_p0.obj");
            eng::MeshHandle brz1 = mesh("prop_barrel_open_p1.obj");
            const auto& omnis = scene.omniNodes();
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
    // Replaces the demo's crystal field and animated boxes (formerly two
    // spinning "tesseracts"). The great hall is the rest/reward beat
    // between the entry hall and the vault, so the centrepiece is a
    // treasure shrine: a single low-poly chest levitating over the origin
    // like enchanted loot, slowly turning so every facet catches the
    // braziers. The demo's crystal spires (radius ~2, glassy rim sheen)
    // ring it as the arcane apparatus holding it aloft; offering clutter
    // sits one step further out. A warm gold spill under the lid makes it
    // the hall's landmark from every doorway and pulses like banked coals.
    eng::NodeHandle chestBase;
    eng::NodeHandle chestSpin;
    eng::LightHandle chestGlow;
    const glm::vec3 chestGlowColour = glm::vec3(1.0f, 0.62f, 0.22f) * 1.6f;
    {
        const std::string props = assets + "/meshes/props/";
        // Offering ring at radius 3.2 -- outside the crystal spires (which
        // reclaimed their original ~2.1 spots): vases and sacks
        // alternating, tribute piled around the hoard.
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

        // The chest (low-poly-chest pack, converted via gltf_to_obj.py) is
        // authored ~0.2 units wide; scale to a ~1.2 m footprint. Base node
        // bobs (and carries the unscaled sparkles + glow light); the child
        // spin node holds the 6x scale and the slow yaw turn.
        chestBase = r.createNode(eng::kRootNode, {0.0f, 1.35f, 0.0f});
        chestSpin = r.createNode(chestBase);
        r.setScale(chestSpin, glm::vec3(6.0f));
        r.attachMesh(chestSpin, r.loadObj(props + "prop_chest.obj"),
                     "Game/PropChest", true);

        // Treasure shimmer rides the bob: sparkles around the lid; one warm
        // omni (replacing the two violet tesseract lights -- net one slot
        // freed of the shader's 16) spills gold over spires and braziers.
        r.attachParticles(chestBase, "PSX/Sparkles");
        eng::LightDesc glow;
        glow.colour = chestGlowColour;
        glow.range = 6.0f;
        chestGlow = r.attachLight(chestBase, glow);
    }

    // ------------------------------------------------------- FPS player ---
    FpsController player;
    player.init(r, dungeon.spawn(),
                float(engine.config().getNumber("player.speed", 3.0)),
                float(engine.config().getNumber("player.mouse_sensitivity", 0.002)),
                glm::vec3(-1000.0f), glm::vec3(1000.0f));
    player.setResolver([&dungeon](glm::vec3 from, glm::vec3 to) {
        return dungeon.resolveMove(from, to, 0.35f);
    });
    // Distance-based readability: a weak cool light carried at the player's
    // head keeps the near field legible; the fog swallows everything else.
    {
        eng::LightDesc carry;
        carry.colour = glm::vec3(std::pow(0.55f, 2.2f), std::pow(0.65f, 2.2f),
                                 std::pow(0.55f, 2.2f)) * 0.8f;
        carry.range = 5.0f;
        r.attachLight(player.headNode(), carry);
    }
    engine.input().setMouseGrab(true);

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
        scene.update(r, animTime);
        dungeon.update(r, animTime); // torch flicker
        dungeon.updateVisibility(r, player.eyePosition(), 30.0f);

        // Levitating loot: slow bob + steady yaw turn so every facet
        // catches the braziers; glow pulses like banked coals (two-sine
        // breathing, never below ~0.8x).
        r.setPosition(chestBase, {0.0f, 1.35f + 0.25f * std::sin(animTime * 0.9f),
                                  0.0f});
        r.setOrientation(chestSpin,
                         glm::angleAxis(animTime * 0.8f, glm::vec3(0, 1, 0)));
        const float pulse = 0.9f + 0.1f * std::sin(animTime * 1.7f) +
                            0.05f * std::sin(animTime * 4.3f);
        r.setLightColour(chestGlow, chestGlowColour * pulse);

        player.update(in, r, dt);

        // Torch interaction: aim at a torch within reach -> HUD prompt;
        // E toggles the flame + light.
        const int aimed =
            dungeon.findTorch(player.eyePosition(), player.forward(), 2.5f);
        if (aimed >= 0) {
            engine.debugUi().setHudPrompt(dungeon.torchLit(aimed)
                                              ? "Press [E] to snuff the torch"
                                              : "Press [E] to light the torch");
            if (in.wasPressed("interact"))
                dungeon.toggleTorch(r, aimed);
        } else {
            engine.debugUi().setHudPrompt("");
        }

        engine.renderFrame(dt);
    }
    engine.shutdown();
    return 0;
}
