#include <eng/ecs/Scene.h>

#include <cstdlib>
#include <iostream>

using namespace eng::ecs;

static void require(bool c, const char* m) {
    if (!c) { std::cerr << "SceneTests: " << m << '\n'; std::exit(1); }
}

int main() {
    Scene scene;

    const entt::entity a = scene.create("alpha");
    const entt::entity b = scene.create();
    require(scene.registry().valid(a), "created entity is valid");
    require(a != b, "distinct entities");
    require(scene.registry().get<Name>(a).value == "alpha", "name stored");
    require(!scene.registry().all_of<Name>(b), "no Name when unnamed");

    require(scene.registry().all_of<Transform>(a), "entity has Transform");
    require(scene.registry().all_of<Dirty>(a), "new entity is Dirty");

    scene.destroy(a);
    require(!scene.registry().valid(a), "destroyed entity invalid");
    require(scene.registry().valid(b), "sibling survives");

    // destroy() removes the entity from its parent's Children list.
    {
        Scene s2;
        auto p = s2.create("parent");
        auto c = s2.create("child");
        s2.registry().emplace<Children>(p, std::vector<entt::entity>{c});
        s2.registry().emplace<Parent>(c, p);
        s2.destroy(c);
        require(!s2.registry().valid(c), "child destroyed");
        require(s2.registry().get<Children>(p).value.empty(),
                "child removed from parent list");
    }

    // destroy() orphans the entity's children (keeps them alive).
    {
        Scene s3;
        auto p = s3.create("parent");
        auto c = s3.create("child");
        s3.registry().emplace<Children>(p, std::vector<entt::entity>{c});
        s3.registry().emplace<Parent>(c, p);
        s3.destroy(p);
        require(!s3.registry().valid(p), "parent destroyed");
        require(s3.registry().valid(c), "child survives");
        require(s3.registry().get<Parent>(c).value == entt::null,
                "child orphaned");
    }

    // --- transforms ---
    Scene s2t;
    const entt::entity root = s2t.create("root");
    Transform rt;
    rt.position = {10.0f, 0.0f, 0.0f};
    s2t.setLocalTransform(root, rt);
    require(s2t.registry().all_of<Dirty>(root), "setLocalTransform marks Dirty");

    s2t.updateWorldTransforms();
    require(!s2t.registry().all_of<Dirty>(root), "update clears Dirty");
    const glm::mat4& wm = s2t.registry().get<WorldTransform>(root).matrix;
    require(wm[3][0] == 10.0f, "world translation X applied");
    require(wm[3][1] == 0.0f && wm[3][2] == 0.0f, "world translation Y/Z zero");

    std::cout << "SceneTests OK\n";
    return 0;
}
