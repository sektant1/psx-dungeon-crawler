// ogre-psx-demo -- port of MenacingMecha's godot-psx-style-demo, driven
// through the eng public API (no Ogre/SDL includes here). The scene itself
// is shared with the game: samples/common/DemoScene + demo_scene.toml.

#include "DemoScene.h"

#include <eng/Engine.h>
#include <eng/Math.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <string>

namespace {

// world/orbit_camera.gd: rotation.y = base + t
struct OrbitCamera {
    eng::NodeHandle node;
    float baseYaw = 0.0f;
    void update(eng::Renderer& r, float t) const
    {
        r.setOrientation(node, glm::angleAxis(baseYaw + t, glm::vec3(0, 1, 0)));
    }
};

} // namespace

int main(int, char**)
{
    eng::Engine engine;
    const std::string assets = APP_ASSET_DIR;
    if (!engine.init(assets + "/demo.toml", assets))
        return 1;
    eng::Renderer& r = engine.renderer();

    // Camera3D: fov 68.1243 vertical, Godot default clips.
    r.setCameraFov(68.1243f);
    r.setCameraClip(0.05f, 4000.0f);

    // -------------------------------------------------- OrbitPoint branch ---
    OrbitCamera orbit;
    orbit.node = r.createNode(eng::kRootNode);
    orbit.baseYaw = std::atan2(-0.556238f, 0.831023f);

    eng::NodeHandle camNode = r.createNode(orbit.node, {0.0f, 2.147f, 4.48151f});
    r.setOrientation(camNode,
                     eng::quatFromBasisRows(1, 0, 0, 0, 0.989078f, 0.147395f, 0,
                                            -0.147395f, 0.989078f));
    r.attachCamera(camNode);

    // --------------------------------------------------------- Background ---
    eng::MeshHandle bgMesh = r.createInteriorBox(40.0f, 25);
    r.attachMesh(r.createNode(eng::kRootNode, {0.0f, 20.0f, 0.0f}), bgMesh,
                 "PSX/Floor");

    // -------------------------------------------------------- shared scene ---
    DemoScene scene;
    DemoScene::Options sceneOpts;
    sceneOpts.boxes = false; // replaced by the game's treasure-chest centre
    if (!scene.load(r, DEMO_SCENE_TOML, assets + "/meshes/", orbit.node,
                    sceneOpts))
        return 1;

    // ---------------------------------------------- dusk grade overrides ---
    // demo_scene.toml ships the original daylight palette (white 1.5 sun,
    // pink ambient, powder-blue fog/sky) -- too bright for the treasure
    // shrine to read. Re-grade to blue-hour dusk: cool dim ambient, the
    // sun knocked down to pale moonlight, deep blue fog, so the warm lamp
    // posts and the chest's gold glow carry the frame instead.
    const auto lin = [](float srgb) { return std::pow(srgb, 2.2f); };
    r.setAmbient({lin(0.55f) * 0.12f, lin(0.60f) * 0.12f, lin(0.80f) * 0.12f});
    r.setLightColour(scene.sunLight(),
                     {lin(0.50f) * 0.35f, lin(0.58f) * 0.35f, lin(0.85f) * 0.35f});
    // Denser, darker fog: the 40 m interior box fades into night murk
    // instead of hanging as a bright blue backdrop behind the orbit.
    r.setFog({lin(0.06f), lin(0.07f), lin(0.12f)}, 0.08f);
    r.setBackground({0.02f, 0.024f, 0.045f});
    // Additive shaft: ghostlier in the dark (daylight strength blows out).
    r.setMaterialParam("PSX/LightShaft", "modulateColor",
                       glm::vec4(1.0f, 1.0f, 1.0f, 0.22f));

    // ------------------------------------------------- hall centrepiece ---
    // Same treasure shrine as the game's great hall: the low-poly chest
    // levitating over the origin, slowly turning inside the crystal ring
    // (glassy rim spires), sparkles and a pulsing gold omni riding the bob.
    eng::NodeHandle chestBase;
    eng::NodeHandle chestSpin;
    eng::LightHandle chestGlow;
    const glm::vec3 chestGlowColour = glm::vec3(1.0f, 0.62f, 0.22f) * 1.6f;
    {
        chestBase = r.createNode(eng::kRootNode, {0.0f, 1.35f, 0.0f});
        chestSpin = r.createNode(chestBase);
        r.setScale(chestSpin, glm::vec3(6.0f));
        r.attachMesh(chestSpin, r.loadObj(assets + "/meshes/props/prop_chest.obj"),
                     "Game/PropChest");
        r.attachParticles(chestBase, "PSX/Sparkles");
        eng::LightDesc glow;
        glow.colour = chestGlowColour;
        glow.range = 6.0f;
        chestGlow = r.attachLight(chestBase, glow);
    }

    // ------------------------------------------------------ set dressing ---
    // Medieval props (meshes/props, materials/props.material — same
    // conversions as the game) in a ring around the crystal field
    // (~2.4 m radius), inside the orbit-camera radius of ~4.5 m. No walls
    // nearby, so everything is free-standing: lamp posts instead of wall
    // lamps, the sword stabbed into the ground, the shield leaning on a
    // barrel.
    {
        const std::string props = assets + "/meshes/props/";
        const auto mesh = [&](const char* f) { return r.loadObj(props + f); };
        // Ring placement: polar(angleDeg, radius) -> floor position.
        const auto polar = [](float angleDeg, float radius) {
            const float a = glm::radians(angleDeg);
            return glm::vec3(radius * std::sin(a), 0.0f, radius * std::cos(a));
        };
        const auto place = [&](eng::MeshHandle m, const char* mat,
                               glm::vec3 pos, float yawDeg,
                               glm::vec3 scale = glm::vec3(1.0f)) {
            eng::NodeHandle n = r.createNode(eng::kRootNode, pos);
            if (yawDeg != 0.0f)
                r.setOrientation(n, glm::angleAxis(glm::radians(yawDeg),
                                                   glm::vec3(0, 1, 0)));
            if (scale != glm::vec3(1.0f))
                r.setScale(n, scale);
            r.attachMesh(n, m, mat);
            return n;
        };
        // Two-primitive props share one node (per-primitive OBJ split).
        const auto place2 = [&](eng::MeshHandle m0, const char* mat0,
                                eng::MeshHandle m1, const char* mat1,
                                glm::vec3 pos, float yawDeg) {
            eng::NodeHandle n = place(m0, mat0, pos, yawDeg);
            r.attachMesh(n, m1, mat1);
            return n;
        };

        eng::MeshHandle barrel0 = mesh("prop_barrel_p0.obj");
        eng::MeshHandle barrel1 = mesh("prop_barrel_p1.obj");
        eng::MeshHandle crate = mesh("prop_crate.obj");
        eng::MeshHandle sack = mesh("prop_jutesack.obj");
        eng::MeshHandle hay = mesh("prop_haybale.obj");
        eng::MeshHandle pumpkin = mesh("prop_pumpkin.obj");
        eng::MeshHandle vase0 = mesh("prop_vase_p0.obj");
        eng::MeshHandle vase1 = mesh("prop_vase_p1.obj");

        // 0 deg: market table with bread + pumpkin (node rides at the leg
        // tops, y=0.88; tabletop is ~0.51 above that).
        eng::NodeHandle table = place2(
            mesh("prop_table_p0.obj"), "Game/PropWood",
            mesh("prop_table_p1.obj"), "Game/PropMarket",
            polar(0.0f, 3.8f) + glm::vec3(0.0f, 0.88f, 0.0f), 180.0f);
        r.attachMesh(r.createNode(table, {0.3f, 0.53f, -0.2f}),
                     mesh("prop_bread.obj"), "Game/PropMarketMisc");
        r.attachMesh(r.createNode(table, {-0.35f, 0.59f, 0.25f}), pumpkin,
                     "Game/PropMarketMisc");

        // 40 deg: terracotta vases
        place2(vase0, "Game/PropTerracotta", vase1, "Game/PropPlanks",
               polar(40.0f, 3.4f), 0.0f);
        place2(vase0, "Game/PropTerracotta", vase1, "Game/PropPlanks",
               polar(48.0f, 4.0f), 30.0f);

        // 80 deg: barrels + crate stack
        place2(barrel0, "Game/PropPlanks", barrel1, "Game/PropBauerhaus",
               polar(80.0f, 3.6f), 15.0f);
        place2(mesh("prop_barrel_open_p0.obj"), "Game/PropPlanks",
               mesh("prop_barrel_open_p1.obj"), "Game/PropBauerhaus",
               polar(90.0f, 4.1f), -30.0f);
        {
            const glm::vec3 c = polar(70.0f, 4.1f);
            place(crate, "Game/PropMarket", c, 10.0f);
            place(crate, "Game/PropMarket", c + glm::vec3(0, 0.24f, 0), -25.0f);
            place(crate, "Game/PropMarket", c + glm::vec3(0, 0.48f, 0), 40.0f);
        }

        // 130 deg: hay + pumpkins
        place(hay, "Game/PropHay", polar(126.0f, 3.7f), 20.0f);
        place(hay, "Game/PropHay", polar(140.0f, 4.2f), -60.0f);
        // Pumpkins bulge 0.08 below their origin; lift onto the floor.
        place(pumpkin, "Game/PropMarketMisc",
              polar(118.0f, 4.3f) + glm::vec3(0.0f, 0.08f, 0.0f), 0.0f);
        place(pumpkin, "Game/PropMarketMisc",
              polar(148.0f, 3.5f) + glm::vec3(0.0f, 0.08f, 0.0f), 90.0f);

        // 170 deg: jute sacks
        place(sack, "Game/PropJute", polar(168.0f, 3.5f), 100.0f);
        place(sack, "Game/PropJute", polar(176.0f, 4.0f), -15.0f);

        // 210 deg: barrel + crates
        place2(barrel0, "Game/PropPlanks", barrel1, "Game/PropBauerhaus",
               polar(206.0f, 3.7f), 45.0f);
        {
            const glm::vec3 c = polar(218.0f, 4.1f);
            place(crate, "Game/PropMarket", c, -20.0f);
            place(crate, "Game/PropMarket", c + glm::vec3(0, 0.24f, 0), 15.0f);
        }

        // 250 deg: sword stabbed into the ground, shield against a barrel.
        // The weapons-pack meshes are authored huge; scale to ~life size.
        {
            eng::NodeHandle sword =
                r.createNode(eng::kRootNode,
                             polar(246.0f, 3.3f) + glm::vec3(0.0f, 1.15f, 0.0f));
            r.setScale(sword, glm::vec3(0.06f));
            // Blade is modelled pointing +y; roll past 180 so it points
            // down with a slight stuck-in-the-dirt lean.
            r.setOrientation(sword,
                             glm::angleAxis(glm::radians(168.0f),
                                            glm::vec3(0, 0, 1)) *
                                 glm::angleAxis(glm::radians(8.0f),
                                                glm::vec3(1, 0, 0)));
            r.attachMesh(sword, mesh("prop_sword.obj"), "Game/PropWeapon");

            const glm::vec3 b = polar(256.0f, 3.8f);
            place2(barrel0, "Game/PropPlanks", barrel1, "Game/PropBauerhaus",
                   b, 0.0f);
            eng::NodeHandle shield =
                r.createNode(eng::kRootNode,
                             b + glm::vec3(0.0f, 0.55f, 0.75f));
            r.setScale(shield, glm::vec3(0.08f));
            // Lean the shield back against the barrel side.
            r.setOrientation(shield, glm::angleAxis(glm::radians(-20.0f),
                                                    glm::vec3(1, 0, 0)));
            r.attachMesh(shield, mesh("prop_shield.obj"), "Game/PropWeapon");
        }

        // 290 deg: hay + vase
        place(hay, "Game/PropHay", polar(290.0f, 3.8f), -35.0f);
        place2(vase0, "Game/PropTerracotta", vase1, "Game/PropPlanks",
               polar(300.0f, 3.4f), 60.0f);

        // 330 deg: crate stack + sack
        {
            const glm::vec3 c = polar(330.0f, 3.6f);
            place(crate, "Game/PropMarket", c, 25.0f);
            place(crate, "Game/PropMarket", c + glm::vec3(0, 0.24f, 0), -10.0f);
        }
        place(sack, "Game/PropJute", polar(322.0f, 4.2f), 45.0f);

        // Lamp posts between the clusters: 2 m beam post, hanging lamp on
        // top, warm point light (torch-lit vibe against the cool ambient).
        eng::MeshHandle beam = mesh("prop_beam.obj");
        eng::MeshHandle lamp = mesh("prop_lamp.obj");
        eng::LightDesc warmLight;
        warmLight.colour = glm::vec3(std::pow(1.0f, 2.2f),
                                     std::pow(0.62f, 2.2f),
                                     std::pow(0.32f, 2.2f)) * 3.0f;
        warmLight.range = 4.0f;
        for (float a : {45.0f, 135.0f, 225.0f, 315.0f}) {
            const glm::vec3 p = polar(a, 4.3f);
            place(beam, "Game/PropBauerhaus", p, a);
            eng::NodeHandle n =
                r.createNode(eng::kRootNode, p + glm::vec3(0.0f, 2.1f, 0.0f));
            r.attachMesh(n, lamp, "Game/PropLamp");
            r.attachLight(n, warmLight);
        }
    }

    // ---------------------------------------------------------------- loop ---
    bool paused = false;
    float animTime = 0.0f;
    orbit.update(r, 0.0f);

    while (!engine.shouldClose()) {
        const float dt = engine.tick();
        eng::Input& in = engine.input();
        if (in.wasPressed("quit"))
            engine.requestClose();
        if (in.wasPressed("pause"))
            paused = !paused;
        if (in.wasPressed("restart"))
            animTime = 0.0f;

        if (!paused)
            animTime += dt;
        orbit.update(r, animTime);
        scene.update(r, animTime);

        // Levitating loot, same motion as the game: slow bob + steady yaw
        // turn; glow breathes on two sines (never below ~0.8x).
        r.setPosition(chestBase,
                      {0.0f, 1.35f + 0.25f * std::sin(animTime * 0.9f), 0.0f});
        r.setOrientation(chestSpin,
                         glm::angleAxis(animTime * 0.8f, glm::vec3(0, 1, 0)));
        const float pulse = 0.9f + 0.1f * std::sin(animTime * 1.7f) +
                            0.05f * std::sin(animTime * 4.3f);
        r.setLightColour(chestGlow, chestGlowColour * pulse);

        engine.renderFrame(dt);
    }
    engine.shutdown();
    return 0;
}
