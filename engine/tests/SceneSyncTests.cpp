#include <eng/ecs/Scene.h>
#include <eng/ecs/SceneBackend.h>
#include <eng/ecs/SceneSync.h>

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
    std::vector<glm::vec3> positions;

    NodeHandle createNode(NodeHandle, glm::vec3, const std::string&) override {
        ++creates; return NodeHandle{nextNode++};
    }
    void setPosition(NodeHandle, glm::vec3 p) override { positions.push_back(p); }
    void setOrientation(NodeHandle, glm::quat) override {}
    void setScale(NodeHandle, glm::vec3) override {}
    void destroyNode(NodeHandle) override { ++destroys; }
    void attachMesh(NodeHandle, MeshHandle, const std::string&, bool) override {
        ++meshes;
    }
    LightHandle attachLight(NodeHandle, const LightDesc&) override {
        ++lights; return LightHandle{nextLight++};
    }
};

int main() {
    Scene scene;
    RecordingBackend backend;
    SceneSync sync(scene, backend);

    const entt::entity e = scene.create("box");
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
    Scene s2;
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

    // --- reparent updates pushed world position on next sync ---
    Scene s3;
    RecordingBackend b3;
    SceneSync sync3(s3, b3);
    const entt::entity par = s3.create("par");
    Transform prt; prt.position = {5.0f, 0.0f, 0.0f};
    s3.setLocalTransform(par, prt);
    const entt::entity kid = s3.create("kid");
    Transform kdt; kdt.position = {2.0f, 0.0f, 0.0f};
    s3.setLocalTransform(kid, kdt);
    sync3.sync(); // kid world = 2 (no parent yet)
    s3.setParent(kid, par);
    sync3.sync(); // kid world = 7 now
    require(b3.positions.back() == glm::vec3(7.0f, 0.0f, 0.0f),
            "reparented child pushes composed world position");

    // --- double destroy is safe (destroyNode fires once) ---
    Scene s4;
    RecordingBackend b4;
    SceneSync sync4(s4, b4);
    const entt::entity d = s4.create("d");
    sync4.sync();
    s4.destroy(d);
    sync4.sync();
    sync4.sync(); // second sync after destroy must not re-destroy
    require(b4.destroys == 1, "destroyNode fires exactly once");

    std::cout << "SceneSyncTests OK\n";
    return 0;
}
