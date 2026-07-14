// dungeon-crawler scaffold: FPS walk in a PSX-shaded test room,
// now with the demo scene (crystals, boxes, sparkles, light shaft).

#include "FpsController.h"

#include <eng/Engine.h>
#include <eng/Math.h>

#include <glm/gtc/matrix_transform.hpp> // glm::scale
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <string>
#include <vector>

namespace {

// Godot linear-space shading: sRGB editor colours linearised, energy
// multiplied after.
float lin(float srgb) { return std::pow(srgb, 2.2f); }

// R(quat) * S(scale), zero translation -- crystal spire bakes.
glm::mat4 trsBake(glm::quat q, glm::vec3 s)
{
    return glm::mat4_cast(q) *
           glm::scale(glm::mat4(1.0f), s);
}

// Basis given as rows a..i (tscn order), zero translation.
glm::mat4 rowsBake(float a, float b, float c, float d, float e, float f,
                   float g, float h, float i)
{
    return glm::mat4(a, d, g, 0, b, e, h, 0, c, f, i, 0, 0, 0, 0, 1);
}

// world/spatial_sin_pan.gd: offset = T(0,sin(t)*dir,0) * Euler-YXZ(t,t,t)
struct SinPan {
    eng::NodeHandle node;
    bool reverse = false;
    void update(eng::Renderer& r, float t) const
    {
        const float dir = reverse ? -1.0f : 1.0f;
        r.setPosition(node, {0.0f, std::sin(t) * dir, 0.0f});
        r.setOrientation(node, glm::angleAxis(t, glm::vec3(0, 1, 0)) *
                                   glm::angleAxis(t, glm::vec3(1, 0, 0)) *
                                   glm::angleAxis(t, glm::vec3(0, 0, 1)));
    }
};

// world/shadow/shadow.gd: scale = 0.775 + sin(t) * 0.125 * dir
struct ShadowScale {
    eng::NodeHandle node;
    bool reverse = false;
    void update(eng::Renderer& r, float t) const
    {
        const float dir = reverse ? 1.0f : -1.0f;
        const float s = 0.775f + std::sin(t) * 0.125f * dir;
        r.setScale(node, {s, s, s});
    }
};

} // namespace

int main(int, char**)
{
    eng::Engine engine;
    const std::string assets = APP_ASSET_DIR;
    if (!engine.init(assets + "/game.toml", assets))
        return 1;
    eng::Renderer& r = engine.renderer();

    r.setCameraFov(70.0f);
    r.setCameraClip(0.05f, 100.0f);

    // Demo environment palette (from world_env.tres).
    r.setAmbient({lin(1.0f) * 0.15f, lin(0.67451f) * 0.15f,
                  lin(0.988235f) * 0.15f});
    r.setFog({lin(0.670588f), lin(0.760784f), lin(1.0f)}, 0.05f);
    r.setBackground({0.670588f, 0.760784f, 1.0f});

    // ------------------------------------------------------------ Room ---
    // 10m interior box stays, "Game/Room" material, floor at y=0.
    const float roomSize = 10.0f;
    eng::MeshHandle room = r.createInteriorBox(roomSize, 7); // dense subdiv: affine (noperspective) UVs warp badly on large tris
    r.attachMesh(r.createNode(eng::kRootNode, {0.0f, roomSize / 2.0f, 0.0f}),
                 room, "Game/Room");

    // ------------------------------------------------------------ Lights ---
    // Directional sun: demo orientation + colour (static, no orbit).
    {
        eng::NodeHandle sunNode =
            r.createNode(eng::kRootNode, {5.08833f, 2.79045f, -0.311581f});
        r.setOrientation(sunNode,
                         eng::quatFromBasisRows(
                             0.999229f, -0.0247207f, 0.0305003f,
                             0.0f, 0.776871f, 0.629659f,
                             -0.0392604f, -0.629174f, 0.776272f));
        eng::LightDesc dirLight;
        dirLight.type = eng::LightDesc::Type::Directional;
        dirLight.colour = {1.5f, 1.5f, 1.5f};
        r.attachLight(sunNode, dirLight);
    }

    // Two omni lamps at ±4 x (demo values).
    for (float x : {-4.0f, 4.0f}) {
        eng::LightDesc omni;
        omni.colour = glm::vec3(lin(0.909804f), lin(0.803922f),
                                lin(0.666667f)) *
                      4.75f;
        omni.range = 3.0f;
        r.attachLight(r.createNode(eng::kRootNode, {x, 0.784f, 0.0f}), omni);
    }

    // ------------------------------------------------------------ Meshes ---
    const std::string mdir = assets + "/meshes/";
    eng::MeshHandle boxMesh       = r.loadObj(mdir + "box.obj");
    eng::MeshHandle bevelBoxMesh  = r.loadObj(mdir + "bevel-box.obj");
    eng::MeshHandle lightShaftMesh= r.loadObj(mdir + "light_shaft.obj");
    eng::MeshHandle crystalGround = r.loadObj(mdir + "crystal_ground.obj");

    // Spire transforms (rotation * non-uniform scale) baked into vertices.
    const glm::mat4 bake1 =
        trsBake(glm::quat(0.99098921f, 0.0f, 0.0f, -0.13394181f),
                {0.96659988f, 1.97307432f, 0.96659982f});
    const glm::mat4 bake2 =
        rowsBake(-0.223364f, -0.132712f, 0.884556f, -0.242743f, 1.90228f,
                 -1.36255e-08f, -0.852817f, -0.5067f, -0.231677f);
    const glm::mat4 bake3 =
        rowsBake(-0.689533f, -0.463782f, -0.375407f, -0.214429f, 1.90228f,
                 3.00904e-09f, 0.361937f, 0.24344f, -0.715194f);
    const glm::mat4 bake4 =
        trsBake(glm::quat(0.78186893f, 0.08229667f, -0.60888463f, -0.10567717f),
                {0.61454207f, 1.97307408f, 0.61454219f});
    const eng::MeshHandle spires[4] = {
        r.loadObj(mdir + "crystal_spire1.obj", &bake1),
        r.loadObj(mdir + "crystal_spire2.obj", &bake2),
        r.loadObj(mdir + "crystal_spire3.obj", &bake3),
        r.loadObj(mdir + "crystal_spire4.obj", &bake4),
    };

    eng::MeshHandle shadowPlane = r.createPlane(2.0f);

    // --------------------------------------------------------- LightShaft ---
    r.attachMesh(
        r.createNode(eng::kRootNode, {0.0109267f, 2.05731f, 0.0147681f}),
        lightShaftMesh, "PSX/LightShaft");

    // --------------------------------------------- BoxMetal + sparkles ---
    std::vector<SinPan> sinPans;
    std::vector<ShadowScale> shadowScales;
    {
        eng::NodeHandle base =
            r.createNode(eng::kRootNode, {-1.0f, 2.236f, 0.0f});
        r.setScale(base, glm::vec3(0.613118f));
        eng::NodeHandle anim = r.createNode(base);
        r.attachMesh(anim, bevelBoxMesh, "PSX/BoxMetal");
        r.attachParticles(anim, "PSX/Sparkles");
        sinPans.push_back({anim, /*reverse=*/false});
    }

    // ---------------------------------------------------------- BoxLit ---
    {
        eng::NodeHandle base =
            r.createNode(eng::kRootNode, {1.0f, 2.236f, 0.0f});
        r.setScale(base, glm::vec3(0.613118f));
        eng::NodeHandle anim = r.createNode(base);
        r.attachMesh(anim, boxMesh, "PSX/BoxLit");
        sinPans.push_back({anim, /*reverse=*/true});
    }

    // ------------------------------------------------------- Blob shadows ---
    for (bool reverse : {false, true}) {
        eng::NodeHandle base = r.createNode(
            eng::kRootNode, {reverse ? 1.0f : -1.0f, 0.0f, 0.0f});
        eng::NodeHandle mesh = r.createNode(base, {0.0f, 0.05f, 0.0f});
        r.attachMesh(mesh, shadowPlane, "PSX/Shadow");
        shadowScales.push_back({base, reverse});
        shadowScales.back().update(r, 0.0f);
    }

    // ------------------------------------------------------- Crystals ---
    struct CrystalXf {
        float a, b, c, d, e, f, g, h, i, ox, oy, oz;
    };
    const CrystalXf crystals[5] = {
        {0.719146f, 0, 0.694859f, 0, 1, 0, -0.694859f, 0,  0.719146f,  1.93081f,  0,  1.34372f},
        {0.826285f, 0,-0.563252f, 0, 1, 0,  0.563252f, 0,  0.826285f, -1.32247f,  0,  1.77809f},
        {0.864371f, 0, 0.502854f, 0, 1, 0, -0.502854f, 0,  0.864371f, -2.32825f,  0, -0.999177f},
        {-0.632935f,0, 0.774205f, 0, 1, 0, -0.774205f, 0, -0.632935f,  2.30476f,  0, -1.16371f},
        {-0.90227f, 0,-0.431172f, 0, 1, 0,  0.431172f, 0, -0.90227f,  -0.00271803f,0,-1.98459f},
    };
    for (const CrystalXf& x : crystals) {
        eng::NodeHandle root =
            r.createNode(eng::kRootNode, {x.ox, x.oy, x.oz});
        r.setOrientation(root, eng::quatFromBasisRows(x.a, x.b, x.c,
                                                      x.d, x.e, x.f,
                                                      x.g, x.h, x.i));
        eng::NodeHandle ground =
            r.createNode(root, {-0.02274385f, 0.0f, 0.01232092f});
        r.setScale(ground, glm::vec3(0.31058314f));
        r.attachMesh(ground, crystalGround, "PSX/CrystalGround");
        for (const eng::MeshHandle& spire : spires)
            r.attachMesh(ground, spire, "PSX/CrystalSpire");
    }

    r.setDitherEnabled(true);

    // ---------------------------------------------------------- FPS player ---
    FpsController player;
    const float margin = roomSize / 2.0f - 0.5f;
    player.init(r, {0.0f, 0.0f, 3.0f},
                float(engine.config().getNumber("player.speed", 3.0)),
                float(engine.config().getNumber("player.mouse_sensitivity", 0.002)),
                {-margin, 0.0f, -margin}, {margin, 0.0f, margin});
    engine.input().setMouseGrab(true);

    // ---------------------------------------------------------------- loop ---
    float animTime = 0.0f;
    for (SinPan& p : sinPans)
        p.update(r, 0.0f);

    while (!engine.shouldClose()) {
        const float dt = engine.tick();
        eng::Input& in = engine.input();
        // First Esc releases the mouse, second quits; click re-grabs.
        if (in.wasPressed("quit")) {
            if (in.mouseGrabbed())
                in.setMouseGrab(false);
            else
                engine.requestClose();
        }
        if (!in.mouseGrabbed() && in.wasMouseClicked())
            in.setMouseGrab(true);

        animTime += dt;
        for (SinPan& p : sinPans)
            p.update(r, animTime);
        for (ShadowScale& s : shadowScales)
            s.update(r, animTime);

        player.update(in, r, dt);
        engine.renderFrame(dt);
    }
    engine.shutdown();
    return 0;
}
