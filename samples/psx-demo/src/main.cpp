// ogre-psx-demo -- port of MenacingMecha's godot-psx-style-demo, driven
// through the eng public API (no Ogre/SDL includes here).

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

// world/orbit_camera.gd: rotation.y = base + t
struct OrbitCamera {
    eng::NodeHandle node;
    float baseYaw = 0.0f;
    void update(eng::Renderer& r, float t) const
    {
        r.setOrientation(node,
                         glm::angleAxis(baseYaw + t, glm::vec3(0, 1, 0)));
    }
};

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
    if (!engine.init(assets + "/demo.toml", assets))
        return 1;
    eng::Renderer& r = engine.renderer();

    // Camera3D: fov 68.1243 vertical, Godot default clips.
    r.setCameraFov(68.1243f);
    r.setCameraClip(0.05f, 4000.0f);

    // world_env.tres
    r.setAmbient({lin(1.0f) * 0.15f, lin(0.67451f) * 0.15f,
                  lin(0.988235f) * 0.15f});
    r.setFog({lin(0.670588f), lin(0.760784f), lin(1.0f)}, 0.05f);
    r.setBackground({0.670588f, 0.760784f, 1.0f});

    // ------------------------------------------------------------ meshes ---
    const std::string mdir = assets + "/meshes/";
    eng::MeshHandle boxMesh = r.loadObj(mdir + "box.obj");
    eng::MeshHandle bevelBoxMesh = r.loadObj(mdir + "bevel-box.obj");
    eng::MeshHandle lightShaftMesh = r.loadObj(mdir + "light_shaft.obj");
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

    eng::MeshHandle bgMesh = r.createInteriorBox(40.0f, 25);
    eng::MeshHandle shadowPlane = r.createPlane(2.0f);

    // -------------------------------------------------- OrbitPoint branch ---
    OrbitCamera orbit;
    orbit.node = r.createNode(eng::kRootNode);
    orbit.baseYaw = std::atan2(-0.556238f, 0.831023f);

    eng::NodeHandle camNode =
        r.createNode(orbit.node, {0.0f, 2.147f, 4.48151f});
    r.setOrientation(camNode,
                     eng::quatFromBasisRows(1, 0, 0, 0, 0.989078f, 0.147395f,
                                            0, -0.147395f, 0.989078f));
    r.attachCamera(camNode);

    eng::NodeHandle dirLightNode =
        r.createNode(orbit.node, {5.08833f, 2.79045f, -0.311581f});
    r.setOrientation(dirLightNode,
                     eng::quatFromBasisRows(0.999229f, -0.0247207f, 0.0305003f,
                                            0.0f, 0.776871f, 0.629659f,
                                            -0.0392604f, -0.629174f, 0.776272f));
    eng::LightDesc dirLight;
    dirLight.type = eng::LightDesc::Type::Directional;
    dirLight.colour = {1.5f, 1.5f, 1.5f}; // light_energy 1.5, white
    r.attachLight(dirLightNode, dirLight);

    // --------------------------------------------------------- OmniLights ---
    for (float x : {-4.0f, 4.0f}) {
        eng::LightDesc omni;
        omni.colour = glm::vec3(lin(0.909804f), lin(0.803922f),
                                lin(0.666667f)) *
                      4.75f;
        omni.range = 3.0f;
        r.attachLight(r.createNode(eng::kRootNode, {x, 0.784f, 0.0f}), omni);
    }

    // --------------------------------------------------------- Background ---
    r.attachMesh(r.createNode(eng::kRootNode, {0.0f, 20.0f, 0.0f}), bgMesh,
                 "PSX/Floor");

    // --------------------------------------------------------- LightShaft ---
    r.attachMesh(
        r.createNode(eng::kRootNode, {0.0109267f, 2.05731f, 0.0147681f}),
        lightShaftMesh, "PSX/LightShaft");

    // ------------------------------------------------- BoxMetal + sparkles ---
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

    // -------------------------------------------------------------- BoxLit ---
    {
        eng::NodeHandle base =
            r.createNode(eng::kRootNode, {1.0f, 2.236f, 0.0f});
        r.setScale(base, glm::vec3(0.613118f));
        eng::NodeHandle anim = r.createNode(base);
        r.attachMesh(anim, boxMesh, "PSX/BoxLit");
        sinPans.push_back({anim, /*reverse=*/true});
    }

    // -------------------------------------------------------- blob shadows ---
    for (bool reverse : {false, true}) {
        eng::NodeHandle base = r.createNode(
            eng::kRootNode, {reverse ? 1.0f : -1.0f, 0.0f, 0.0f});
        eng::NodeHandle mesh = r.createNode(base, {0.0f, 0.05f, 0.0f});
        r.attachMesh(mesh, shadowPlane, "PSX/Shadow");
        shadowScales.push_back({base, reverse});
        shadowScales.back().update(r, 0.0f);
    }

    // ------------------------------------------------------------ crystals ---
    struct CrystalXf {
        float a, b, c, d, e, f, g, h, i, ox, oy, oz;
    };
    const CrystalXf crystals[5] = {
        {0.719146f, 0, 0.694859f, 0, 1, 0, -0.694859f, 0, 0.719146f, 1.93081f, 0, 1.34372f},
        {0.826285f, 0, -0.563252f, 0, 1, 0, 0.563252f, 0, 0.826285f, -1.32247f, 0, 1.77809f},
        {0.864371f, 0, 0.502854f, 0, 1, 0, -0.502854f, 0, 0.864371f, -2.32825f, 0, -0.999177f},
        {-0.632935f, 0, 0.774205f, 0, 1, 0, -0.774205f, 0, -0.632935f, 2.30476f, 0, -1.16371f},
        {-0.90227f, 0, -0.431172f, 0, 1, 0, 0.431172f, 0, -0.90227f, -0.00271803f, 0, -1.98459f},
    };
    for (const CrystalXf& x : crystals) {
        eng::NodeHandle root = r.createNode(eng::kRootNode, {x.ox, x.oy, x.oz});
        r.setOrientation(root, eng::quatFromBasisRows(x.a, x.b, x.c, x.d, x.e,
                                                      x.f, x.g, x.h, x.i));
        eng::NodeHandle ground =
            r.createNode(root, {-0.02274385f, 0.0f, 0.01232092f});
        r.setScale(ground, glm::vec3(0.31058314f));
        r.attachMesh(ground, crystalGround, "PSX/CrystalGround");
        for (const eng::MeshHandle& spire : spires)
            r.attachMesh(ground, spire, "PSX/CrystalSpire");
    }

    r.setDitherEnabled(true);

    // ---------------------------------------------------------------- loop ---
    bool paused = false;
    float animTime = 0.0f;
    orbit.update(r, 0.0f);
    for (SinPan& p : sinPans)
        p.update(r, 0.0f);

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
        for (SinPan& p : sinPans)
            p.update(r, animTime);
        for (ShadowScale& s : shadowScales)
            s.update(r, animTime);

        engine.renderFrame(dt);
    }
    engine.shutdown();
    return 0;
}
