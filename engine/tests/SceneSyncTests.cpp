#include <eng/ecs/World.h>
#include <eng/ecs/SceneBackend.h>
#include <eng/ecs/SceneSync.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace eng;
using namespace eng::ecs;

static void require(bool c, const char* m) {
    if (!c) { std::cerr << "SceneSyncTests: " << m << '\n'; std::exit(1); }
}

struct RecordingBackend : SceneBackend {
    uint32_t nextNode = 1;
    uint32_t nextLight = 1;
    int creates = 0, destroys = 0, meshes = 0, lights = 0;
    uint32_t nextParticles = 1;
    int particleAttaches = 0, particleDetaches = 0;
    std::string particleEffect;
    glm::vec3 particleOffset{0.0f};
    ParticleSpawnOptions particleOptions;
    std::vector<glm::vec3> positions;

    NodeHandle createNode(NodeHandle, glm::vec3, const std::string&) override {
        ++creates; return NodeHandle{nextNode++};
    }
    std::vector<glm::quat> orientations;
    std::vector<glm::vec3> scales;
    void setPosition(NodeHandle, glm::vec3 p) override { positions.push_back(p); }
    void setOrientation(NodeHandle, glm::quat q) override { orientations.push_back(q); }
    void setScale(NodeHandle, glm::vec3 s) override { scales.push_back(s); }
    void destroyNode(NodeHandle) override { ++destroys; }
    void attachMesh(NodeHandle, MeshHandle, const std::string&, bool) override {
        ++meshes;
    }
    LightHandle attachLight(NodeHandle, const LightDesc&) override {
        ++lights; return LightHandle{nextLight++};
    }
    int colourPushes = 0;
    void setLightColour(LightHandle, glm::vec3) override { ++colourPushes; }
    std::vector<bool> visibility;
    void setNodeVisible(NodeHandle, bool show) override {
        visibility.push_back(show);
    }
    int cameraNodes = 0;
    int lensPushes = 0;
    float lastFov = 0.0f;
    void setCameraNode(NodeHandle) override { ++cameraNodes; }
    void setCameraLens(float fov, float, float) override {
        ++lensPushes;
        lastFov = fov;
    }
    ParticlesHandle attachParticles(NodeHandle, const std::string& effect,
                                    glm::vec3 offset,
                                    const ParticleSpawnOptions& options) override {
        ++particleAttaches;
        particleEffect = effect;
        particleOffset = offset;
        particleOptions = options;
        return ParticlesHandle{nextParticles++};
    }
    void detachParticles(ParticlesHandle) override { ++particleDetaches; }
};

int main() {
    World scene;
    RecordingBackend backend;
    SceneSync sync(scene, backend);

    // A node is allocated for entities that ask for one. RenderNode is the
    // "I draw nothing myself but I need a node" form; MeshRenderer and LightRef
    // imply it. A bare entity (below) stays gameplay-side.
    const entt::entity e = scene.create("box");
    scene.registry().emplace<RenderNode>(e);
    Transform t; t.position = {1.0f, 2.0f, 3.0f};
    scene.setLocalTransform(e, t);

    sync.sync();
    require(backend.creates == 1, "one node created");
    require(scene.registry().all_of<NodeRef>(e), "NodeRef attached");
    require(!backend.positions.empty(), "position pushed");
    require(backend.positions.back() == glm::vec3(1.0f, 2.0f, 3.0f),
            "world position pushed");

    const size_t pushesAfterFirst = backend.positions.size();
    sync.sync();
    require(backend.positions.size() == pushesAfterFirst,
            "clean entity does not re-push");

    scene.destroy(e);
    sync.sync();
    require(backend.destroys == 1, "node destroyed on entity destroy");

    // --- mesh + light attachment (attach exactly once) ---
    World s2;
    RecordingBackend b2;
    SceneSync sync2(s2, b2);
    const entt::entity lit = s2.create("torch");
    s2.registry().emplace<MeshRenderer>(lit, MeshHandle{7}, "Flame", false);
    LightRef lr; lr.desc.range = 5.0f;
    s2.registry().emplace<LightRef>(lit, lr);
    sync2.sync();
    require(b2.meshes == 1, "mesh attached once");
    require(b2.lights == 1, "light attached once");
    require(s2.registry().get<LightRef>(lit).handle.valid(),
            "light handle written back");
    sync2.sync();
    require(b2.meshes == 1 && b2.lights == 1, "attachment not repeated");
    sync2.clear();
    require(b2.destroys == 1 && !s2.registry().all_of<NodeRef>(lit),
            "clear tears down the materialised backend view");
    sync2.clear();
    require(b2.destroys == 1, "clear is idempotent");

    // --- reparent updates pushed world position on next sync ---
    World s3;
    RecordingBackend b3;
    SceneSync sync3(s3, b3);
    const entt::entity par = s3.create("par");
    s3.registry().emplace<RenderNode>(par);
    Transform prt; prt.position = {5.0f, 0.0f, 0.0f};
    s3.setLocalTransform(par, prt);
    const entt::entity kid = s3.create("kid");
    s3.registry().emplace<RenderNode>(kid);
    Transform kdt; kdt.position = {2.0f, 0.0f, 0.0f};
    s3.setLocalTransform(kid, kdt);
    sync3.sync(); // kid world = 2 (no parent yet)
    s3.setParent(kid, par);
    sync3.sync(); // kid world = 7 now
    require(b3.positions.back() == glm::vec3(7.0f, 0.0f, 0.0f),
            "reparented child pushes composed world position");

    // --- double destroy is safe (destroyNode fires once) ---
    World s4;
    RecordingBackend b4;
    SceneSync sync4(s4, b4);
    const entt::entity d = s4.create("d");
    s4.registry().emplace<RenderNode>(d);
    sync4.sync();
    s4.destroy(d);
    sync4.sync();
    sync4.sync(); // second sync after destroy must not re-destroy
    require(b4.destroys == 1, "destroyNode fires exactly once");

    // --- an entity with nothing to draw costs the renderer nothing ----------
    // The rule that makes one shared world affordable: most entities in it (a
    // combatant's stats, a spawner, a trigger volume) have a position and no
    // visual, and each one used to get an empty node plus a transform push per
    // frame.
    World s5;
    RecordingBackend b5;
    SceneSync sync5(s5, b5);
    const entt::entity ghost = s5.create("stats only");
    sync5.sync();
    require(b5.creates == 0, "no node for an entity that draws nothing");
    require(!s5.registry().all_of<NodeRef>(ghost), "and no NodeRef either");
    s5.registry().emplace<MeshRenderer>(ghost, MeshHandle{3}, "Mat", false);
    sync5.sync();
    require(b5.creates == 1 && b5.meshes == 1,
            "adding a visual later materialises the node");

    // --- a child of a rotated, scaled parent is pushed fully composed -------
    // The bug this replaces pushed the world POSITION with the entity's LOCAL
    // rotation, so a child of a rotated parent drew in the right place facing
    // the wrong way -- which reads as an art bug, not a transform one.
    {
        World s6;
        RecordingBackend b6;
        SceneSync sync6(s6, b6);
        const entt::entity turntable = s6.create("turntable");
        Transform tt;
        tt.rotation = glm::angleAxis(glm::radians(90.0f),
                                     glm::vec3(0.0f, 1.0f, 0.0f));
        tt.scale = glm::vec3(2.0f);
        s6.setLocalTransform(turntable, tt);

        const entt::entity mounted = s6.create("mounted");
        s6.registry().emplace<RenderNode>(mounted);
        Transform mt;
        mt.position = {1.0f, 0.0f, 0.0f};
        s6.setLocalTransform(mounted, mt);
        s6.setParent(mounted, turntable);
        sync6.sync();

        // 90 degrees about +Y sends +X to -Z, and the parent's scale doubles it.
        const glm::vec3 pushed = b6.positions.back();
        require(std::abs(pushed.x) < 1e-4f && std::abs(pushed.z + 2.0f) < 1e-4f,
                "child position is composed through the parent");
        const glm::quat q = b6.orientations.back();
        require(std::abs(glm::degrees(glm::angle(q)) - 90.0f) < 0.01f,
                "child inherits the parent's rotation");
        require(std::abs(b6.scales.back().x - 2.0f) < 1e-4f,
                "child inherits the parent's scale");
    }

    // --- Visibility is pushed on change, and only on change ----------------
    {
        World s7;
        RecordingBackend b7;
        SceneSync sync7(s7, b7);
        const entt::entity prop = s7.create("prop");
        s7.registry().emplace<RenderNode>(prop);
        sync7.sync();
        require(b7.visibility.empty(),
                "a visible entity is never told it is visible");

        s7.registry().emplace<Visibility>(prop, Visibility{false});
        sync7.sync();
        require(b7.visibility.size() == 1 && !b7.visibility.back(),
                "hiding pushes once");
        sync7.sync();
        require(b7.visibility.size() == 1, "and is not repeated every frame");

        // Removing the component means visible again -- "no Visibility" is the
        // default, so it cannot be a way to leave something stuck hidden.
        s7.registry().remove<Visibility>(prop);
        sync7.sync();
        require(b7.visibility.size() == 2 && b7.visibility.back(),
                "removing Visibility shows the node again");
    }

    // --- the scene's camera --------------------------------------------------
    {
        World s8;
        RecordingBackend b8;
        SceneSync sync8(s8, b8);

        // A world with no Camera must never touch the renderer's: a game that
        // drives its own would otherwise lose it on the first sync.
        const entt::entity prop = s8.create("prop");
        s8.registry().emplace<RenderNode>(prop);
        sync8.sync();
        require(b8.cameraNodes == 0 && b8.lensPushes == 0,
                "no authored camera, no camera calls at all");

        const entt::entity shot = s8.create("shot");
        Camera lens;
        lens.fovDegrees = 52.0f;
        s8.registry().emplace<Camera>(shot, lens);
        sync8.sync();
        require(b8.cameraNodes == 1, "an authored camera is attached");
        require(s8.registry().all_of<NodeRef>(shot),
                "and gets a node to be carried by");
        require(b8.lensPushes == 1 && b8.lastFov == 52.0f,
                "its lens is pushed once");

        sync8.sync();
        // Re-attached every frame on purpose: anything may grab the renderer's
        // one camera (a player controller does, when it spawns after the scene
        // loaded), and the scene has to win.
        require(b8.cameraNodes == 2, "the attachment is re-asserted each frame");
        require(b8.lensPushes == 1, "the lens is not re-pushed unchanged");

        s8.registry().get<Camera>(shot).fovDegrees = 90.0f;
        sync8.sync();
        require(b8.lensPushes == 2 && b8.lastFov == 90.0f,
                "an edited fov reaches the renderer");

        // Highest active priority wins, so an override composes: neither camera
        // has to know the other exists.
        const entt::entity debugCam = s8.create("debug");
        Camera override_;
        override_.fovDegrees = 110.0f;
        override_.priority = 10;
        s8.registry().emplace<Camera>(debugCam, override_);
        sync8.sync();
        require(b8.lastFov == 110.0f, "the higher priority camera takes over");

        s8.registry().get<Camera>(debugCam).active = false;
        sync8.sync();
        require(b8.lastFov == 90.0f,
                "deactivating it hands the shot back, without deleting it");
    }

    // --- particle authored state reaches and refreshes the backend ---------
    {
        World particles;
        RecordingBackend bp;
        SceneSync syncParticles(particles, bp);
        const entt::entity dust = particles.create("dust");
        ParticleEmitter emitter;
        emitter.effect = "treasure_dust";
        emitter.offset = {1.0f, 2.0f, 3.0f};
        emitter.scale = 2.5f;
        particles.registry().emplace<ParticleEmitter>(dust, emitter);

        syncParticles.sync();
        require(bp.particleAttaches == 1,
                "a particle-only entity materialises and attaches");
        require(bp.particleEffect == "treasure_dust" &&
                    bp.particleOffset == glm::vec3(1.0f, 2.0f, 3.0f),
                "effect and local offset reach the backend");
        require(bp.particleOptions.sizeScale == 2.5f,
                "authored particle size scale reaches the spawn options");
        syncParticles.sync();
        require(bp.particleAttaches == 1 && bp.particleDetaches == 0,
                "unchanged particles are not restarted each frame");

        particles.registry().get<ParticleEmitter>(dust).scale = 0.5f;
        syncParticles.sync();
        require(bp.particleAttaches == 2 && bp.particleDetaches == 1 &&
                    bp.particleOptions.sizeScale == 0.5f,
                "editing scale restarts once with the new options");

        particles.registry().get<ParticleEmitter>(dust).playing = false;
        syncParticles.sync();
        require(bp.particleAttaches == 2 && bp.particleDetaches == 2,
                "disabling autostart detaches the live emitter");

        particles.registry().get<ParticleEmitter>(dust).playing = true;
        syncParticles.sync();
        require(bp.particleAttaches == 3,
                "re-enabling autostart attaches it again");
        syncParticles.clear();
        require(!particles.registry().all_of<ParticlesRef>(dust),
                "clearing a renderer view forgets its particle handle");
        syncParticles.sync();
        require(bp.particleAttaches == 4,
                "reattaching a renderer view starts authored particles again");
    }

    std::cout << "SceneSyncTests OK\n";
    return 0;
}
