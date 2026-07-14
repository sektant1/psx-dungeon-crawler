// ogre-psx-demo -- 1:1-as-practical port of MenacingMecha's godot-psx-style-demo
// (world/world.tscn + shaders/) to OGRE 14.5 + SDL2.
//
// SDL2 owns the window and the loop; Ogre owns the GPU (no ApplicationContext,
// no RTSS -- every material is hand-written GLSL).

#include "Animation.h"
#include "ObjLoader.h"
#include "ProceduralMeshes.h"
#include "RenderCore.h"

#include <Ogre.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

// .tscn Transform3D(a..i, ox,oy,oz): a..i are the basis ROWS (row-major),
// exactly Ogre::Matrix3's constructor order. Basis columns = local axes.
Ogre::Quaternion rot(float a, float b, float c, float d, float e, float f,
                     float g, float h, float i)
{
    return Ogre::Quaternion(Ogre::Matrix3(a, b, c, d, e, f, g, h, i));
}

// Bake matrix for a crystal spire: R(quat) * S(scale) plus zero translation.
Ogre::Matrix4 trsBake(const Ogre::Quaternion& q, const Ogre::Vector3& s)
{
    Ogre::Matrix3 r;
    q.ToRotationMatrix(r);
    Ogre::Matrix3 m(r[0][0] * s.x, r[0][1] * s.y, r[0][2] * s.z,
                    r[1][0] * s.x, r[1][1] * s.y, r[1][2] * s.z,
                    r[2][0] * s.x, r[2][1] * s.y, r[2][2] * s.z);
    Ogre::Matrix4 m4(m);
    return m4;
}

Ogre::Matrix4 rowsBake(float a, float b, float c, float d, float e, float f,
                       float g, float h, float i)
{
    return Ogre::Matrix4(a, b, c, 0, d, e, f, 0, g, h, i, 0, 0, 0, 0, 1);
}

} // namespace

int main(int, char**)
{
    // ------------------------------------------------------------ window ---
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    // Match the pp_stack.tscn output viewport (960x720, 4:3).
    // No SDL_WINDOW_OPENGL: Ogre GL3Plus creates its own context.
    SDL_Window* window =
        SDL_CreateWindow("ogre-psx-demo", SDL_WINDOWPOS_CENTERED,
                         SDL_WINDOWPOS_CENTERED, 960, 720, SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    SDL_GetWindowWMInfo(window, &wmInfo);
    uintptr_t handle = 0;
#if defined(SDL_VIDEO_DRIVER_X11)
    handle = static_cast<uintptr_t>(wmInfo.info.x11.window);
#endif

    eng::RenderCore renderer;
    if (!renderer.init(handle, 960, 720, "ogre-psx-demo", APP_ASSET_DIR))
        return 1;
    renderer.camera()->setFOVy(Ogre::Degree(68.1243f));
    renderer.viewport()->setBackgroundColour(
        Ogre::ColourValue(0.670588f, 0.760784f, 1.0f));
    Ogre::SceneManager* sm = renderer.sceneMgr();
    Ogre::SceneNode* rootNode = sm->getRootSceneNode();

    // Shading happens in linear space (like Godot); colours picked in the
    // editor are sRGB, and Godot multiplies energy AFTER linearising.
    auto lin = [](float srgb) { return std::pow(srgb, 2.2f); };

    // ------------------------------------------------------- environment ---
    // world_env.tres: ambient colour (1, 0.67451, 0.988235) * energy 0.15,
    // exponential fog colour (0.670588, 0.760784, 1) density 0.05.
    sm->setAmbientLight(Ogre::ColourValue(lin(1.0f) * 0.15f, lin(0.67451f) * 0.15f,
                                          lin(0.988235f) * 0.15f));
    sm->setFog(Ogre::FOG_EXP,
               Ogre::ColourValue(lin(0.670588f), lin(0.760784f), lin(1.0f)), 0.05f);

    // ------------------------------------------------------------ meshes ---
    ObjLoader::load(std::string(APP_ASSET_DIR) + "/meshes/box.obj", "box.mesh");
    ObjLoader::load(std::string(APP_ASSET_DIR) + "/meshes/bevel-box.obj", "bevelbox.mesh");
    ObjLoader::load(std::string(APP_ASSET_DIR) + "/meshes/light_shaft.obj", "lightshaft.mesh");

    // Crystal: spire local transforms (crystal.gltf node TRS, with the
    // Spire_2/3 overrides from crystal_mesh.tscn) contain non-uniform
    // scale+rotation that Ogre TRS nodes can't hold -> baked into the meshes.
    const std::string mdir = std::string(APP_ASSET_DIR) + "/meshes/";
    ObjLoader::load(mdir + "crystal_ground.obj", "crystal_ground.mesh");
    ObjLoader::load(mdir + "crystal_spire1.obj", "crystal_spire1.mesh",
                    trsBake(Ogre::Quaternion(0.99098921f, 0.f, 0.f, -0.13394181f),
                            {0.96659988f, 1.97307432f, 0.96659982f}));
    ObjLoader::load(mdir + "crystal_spire2.obj", "crystal_spire2.mesh",
                    rowsBake(-0.223364f, -0.132712f, 0.884556f,
                             -0.242743f, 1.90228f, -1.36255e-08f,
                             -0.852817f, -0.5067f, -0.231677f));
    ObjLoader::load(mdir + "crystal_spire3.obj", "crystal_spire3.mesh",
                    rowsBake(-0.689533f, -0.463782f, -0.375407f,
                             -0.214429f, 1.90228f, 3.00904e-09f,
                             0.361937f, 0.24344f, -0.715194f));
    ObjLoader::load(mdir + "crystal_spire4.obj", "crystal_spire4.mesh",
                    trsBake(Ogre::Quaternion(0.78186893f, 0.08229667f,
                                             -0.60888463f, -0.10567717f),
                            {0.61454207f, 1.97307408f, 0.61454219f}));

    ProceduralMeshes::createInteriorBox("background.mesh", 40.0f, 25);
    ProceduralMeshes::createPlane("shadowplane.mesh", 2.0f);

    // -------------------------------------------------- OrbitPoint branch ---
    // OrbitPoint basis is a pure Y rotation; orbit_camera.gd adds time on top.
    Ogre::SceneNode* orbitNode = rootNode->createChildSceneNode();
    OrbitCamera orbit;
    orbit.node = orbitNode;
    orbit.baseYaw = std::atan2(-0.556238f, 0.831023f); // from Transform3D row 0

    Ogre::SceneNode* camNode = orbitNode->createChildSceneNode(
        Ogre::Vector3(0.f, 2.147f, 4.48151f));
    camNode->setOrientation(rot(1, 0, 0, 0, 0.989078f, 0.147395f,
                                0, -0.147395f, 0.989078f));
    camNode->attachObject(renderer.camera());

    // DirectionalLight (child of OrbitPoint => rotates with the camera),
    // light_energy 1.5, white. Direction = node -Z, same convention as Godot.
    Ogre::SceneNode* dirLightNode = orbitNode->createChildSceneNode(
        Ogre::Vector3(5.08833f, 2.79045f, -0.311581f));
    dirLightNode->setOrientation(rot(0.999229f, -0.0247207f, 0.0305003f,
                                     0.f, 0.776871f, 0.629659f,
                                     -0.0392604f, -0.629174f, 0.776272f));
    Ogre::Light* dirLight = sm->createLight("DirectionalLight");
    dirLight->setType(Ogre::Light::LT_DIRECTIONAL);
    dirLight->setDiffuseColour(1.5f, 1.5f, 1.5f);
    dirLight->setSpecularColour(Ogre::ColourValue::Black); // specular_disabled
    dirLightNode->attachObject(dirLight);

    // --------------------------------------------------------- OmniLights ---
    // colour (0.909804, 0.803922, 0.666667) * energy 4.75, range 3,
    // attenuation exponent 0.0915055 (applied in psx.vert).
    const Ogre::ColourValue omniColour(lin(0.909804f) * 4.75f,
                                       lin(0.803922f) * 4.75f,
                                       lin(0.666667f) * 4.75f);
    for (float x : {-4.0f, 4.0f}) {
        Ogre::Light* omni = sm->createLight();
        omni->setType(Ogre::Light::LT_POINT);
        omni->setDiffuseColour(omniColour);
        omni->setSpecularColour(Ogre::ColourValue::Black);
        omni->setAttenuation(3.0f, 1.0f, 0.0f, 0.0f); // shader only reads range
        rootNode->createChildSceneNode(Ogre::Vector3(x, 0.784f, 0.f))
            ->attachObject(omni);
    }

    // --------------------------------------------------------- Background ---
    {
        Ogre::Entity* bg = sm->createEntity("background.mesh");
        bg->setMaterialName("PSX/Floor");
        rootNode->createChildSceneNode(Ogre::Vector3(0.f, 20.f, 0.f))
            ->attachObject(bg);
    }

    // ---------------------------------------------------------- LightShaft ---
    {
        Ogre::Entity* shaft = sm->createEntity("lightshaft.mesh");
        shaft->setMaterialName("PSX/LightShaft");
        rootNode
            ->createChildSceneNode(Ogre::Vector3(0.0109267f, 2.05731f, 0.0147681f))
            ->attachObject(shaft);
    }

    // ------------------------------------------------- BoxMetal + sparkles ---
    std::vector<SinPan> sinPans;
    std::vector<ShadowScale> shadowScales;
    {
        Ogre::SceneNode* base =
            rootNode->createChildSceneNode(Ogre::Vector3(-1.f, 2.236f, 0.f));
        base->setScale(0.613118f, 0.613118f, 0.613118f);
        Ogre::SceneNode* anim = base->createChildSceneNode();
        Ogre::Entity* ent = sm->createEntity("bevelbox.mesh");
        ent->setMaterialName("PSX/BoxMetal");
        anim->attachObject(ent);
        sinPans.push_back({anim, /*reverse=*/false});

        Ogre::ParticleSystem* sparkles =
            sm->createParticleSystem("Sparkles", "PSX/Sparkles");
        anim->attachObject(sparkles);
    }

    // -------------------------------------------------------------- BoxLit ---
    {
        Ogre::SceneNode* base =
            rootNode->createChildSceneNode(Ogre::Vector3(1.f, 2.236f, 0.f));
        base->setScale(0.613118f, 0.613118f, 0.613118f);
        Ogre::SceneNode* anim = base->createChildSceneNode();
        Ogre::Entity* ent = sm->createEntity("box.mesh");
        ent->setMaterialName("PSX/BoxLit");
        anim->attachObject(ent);
        sinPans.push_back({anim, /*reverse=*/true});
    }

    // -------------------------------------------------------- blob shadows ---
    // shadow.tscn: animated-scale root, plane mesh child at y=0.05.
    for (bool reverse : {false, true}) {
        Ogre::SceneNode* base = rootNode->createChildSceneNode(
            Ogre::Vector3(reverse ? 1.f : -1.f, 0.f, 0.f));
        Ogre::SceneNode* mesh =
            base->createChildSceneNode(Ogre::Vector3(0.f, 0.05f, 0.f));
        Ogre::Entity* ent = sm->createEntity("shadowplane.mesh");
        ent->setMaterialName("PSX/Shadow");
        mesh->attachObject(ent);
        shadowScales.push_back({base, reverse});
        shadowScales.back().update(0.0f);
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
        Ogre::SceneNode* root =
            rootNode->createChildSceneNode(Ogre::Vector3(x.ox, x.oy, x.oz));
        root->setOrientation(rot(x.a, x.b, x.c, x.d, x.e, x.f, x.g, x.h, x.i));
        // "Ground" node from crystal.gltf: translation + uniform scale.
        Ogre::SceneNode* ground = root->createChildSceneNode(
            Ogre::Vector3(-0.02274385f, 0.f, 0.01232092f));
        ground->setScale(0.31058314f, 0.31058314f, 0.31058314f);
        Ogre::Entity* g = sm->createEntity("crystal_ground.mesh");
        g->setMaterialName("PSX/CrystalGround");
        ground->attachObject(g);
        for (const char* spire : {"crystal_spire1.mesh", "crystal_spire2.mesh",
                                  "crystal_spire3.mesh", "crystal_spire4.mesh"}) {
            Ogre::Entity* s = sm->createEntity(spire);
            s->setMaterialName("PSX/CrystalSpire");
            ground->attachObject(s);
        }
    }

    // ------------------------------------------------------- post process ---
    renderer.setDitherEnabled(true);

    // ---------------------------------------------------------------- loop ---
    // scene_controls.gd: SPACE pauses the animated nodes, R restarts them.
    orbit.update(0.0f);
    for (SinPan& p : sinPans)
        p.update(0.0f);

    bool running = true;
    bool paused = false;
    float animTime = 0.0f;
    auto prev = std::chrono::steady_clock::now();
    // Verification hook: PSX_SCREENSHOT=<path> renders 90 frames, saves, exits.
    const char* shotPath = std::getenv("PSX_SCREENSHOT");
    int frameCount = 0;

    while (running) {
        for (SDL_Event e; SDL_PollEvent(&e);) {
            if (e.type == SDL_QUIT)
                running = false;
            else if (e.type == SDL_WINDOWEVENT &&
                     e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                renderer.onResize(e.window.data1, e.window.data2);
            else if (e.type == SDL_KEYDOWN && e.key.repeat == 0) {
                switch (e.key.keysym.sym) {
                case SDLK_ESCAPE: running = false; break;
                case SDLK_SPACE:
                case SDLK_RETURN: paused = !paused; break; // ui_accept
                case SDLK_r: animTime = 0.0f; break;       // restart()
                }
            }
        }

        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - prev).count();
        prev = now;

        if (!paused)
            animTime += dt;

        orbit.update(animTime);
        for (SinPan& p : sinPans)
            p.update(animTime);
        for (ShadowScale& s : shadowScales)
            s.update(animTime);

        renderer.renderFrame(dt);

        if (shotPath && ++frameCount == 90) {
            renderer.writeScreenshot(shotPath);
            running = false;
        }
    }

    // Shutdown order: Ogre first, native window after (Ogre still references
    // the handle until Root is destroyed).
    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
