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

    // --- hierarchy ---
    Scene s3h;
    const entt::entity parent = s3h.create("parent");
    const entt::entity child = s3h.create("child");
    Transform pt; pt.position = {5.0f, 0.0f, 0.0f};
    Transform ct; ct.position = {2.0f, 0.0f, 0.0f};
    s3h.setLocalTransform(parent, pt);
    s3h.setLocalTransform(child, ct);
    s3h.setParent(child, parent);
    require(s3h.registry().get<Parent>(child).value == parent, "parent set");
    require(s3h.registry().get<Children>(parent).value.size() == 1,
            "child recorded on parent");

    s3h.updateWorldTransforms();
    const glm::mat4& cw = s3h.registry().get<WorldTransform>(child).matrix;
    require(cw[3][0] == 7.0f, "child world = parent + child translation");

    Transform pt2; pt2.position = {0.0f, 3.0f, 0.0f};
    s3h.setLocalTransform(parent, pt2);
    require(s3h.registry().all_of<Dirty>(child), "moving parent dirties child");
    s3h.updateWorldTransforms();
    const glm::mat4& cw2 = s3h.registry().get<WorldTransform>(child).matrix;
    require(cw2[3][0] == 2.0f && cw2[3][1] == 3.0f, "child follows parent");

    // --- unparent ---
    Scene su;
    const entt::entity up = su.create("up_parent");
    const entt::entity uc = su.create("up_child");
    su.setParent(uc, up);
    require(su.registry().get<Children>(up).value.size() == 1, "child linked");
    su.setParent(uc, entt::null);
    require(su.registry().get<Parent>(uc).value == entt::null, "child unparented");
    require(su.registry().get<Children>(up).value.empty(),
            "child removed from old parent on unparent");

    // --- 3-level dirty propagation chain ---
    Scene sc;
    const entt::entity gp = sc.create("gp");
    const entt::entity pa = sc.create("pa");
    const entt::entity ch2 = sc.create("ch");
    Transform gpt; gpt.position = {1.0f, 0.0f, 0.0f}; sc.setLocalTransform(gp, gpt);
    Transform pat; pat.position = {2.0f, 0.0f, 0.0f}; sc.setLocalTransform(pa, pat);
    Transform cht; cht.position = {4.0f, 0.0f, 0.0f}; sc.setLocalTransform(ch2, cht);
    sc.setParent(pa, gp);
    sc.setParent(ch2, pa);
    sc.updateWorldTransforms();
    require(sc.registry().get<WorldTransform>(ch2).matrix[3][0] == 7.0f,
            "3-level world X composes (1+2+4)");
    // Moving only the grandparent re-dirties the grandchild.
    Transform gpt2; gpt2.position = {10.0f, 0.0f, 0.0f}; sc.setLocalTransform(gp, gpt2);
    require(sc.registry().all_of<Dirty>(ch2), "grandparent move dirties grandchild");
    sc.updateWorldTransforms();
    require(sc.registry().get<WorldTransform>(ch2).matrix[3][0] == 16.0f,
            "grandchild follows grandparent (10+2+4)");

    // --- cycle guard: setParent that would form a cycle is rejected ---
    Scene scy;
    const entt::entity x = scy.create("x");
    const entt::entity y = scy.create("y");
    scy.setParent(y, x);          // y under x
    scy.setParent(x, y);          // would make x under y -> cycle: must be rejected
    auto* xp = scy.registry().try_get<Parent>(x);
    require(!xp || xp->value == entt::null,
            "cycle-forming setParent rejected");

    std::cout << "SceneTests OK\n";
    return 0;
}
