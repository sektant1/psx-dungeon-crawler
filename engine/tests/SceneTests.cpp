#include <eng/ecs/World.h>

#include <cstdlib>
#include <iostream>

using namespace eng::ecs;

static void require(bool c, const char* m) {
    if (!c) { std::cerr << "SceneTests: " << m << '\n'; std::exit(1); }
}

int main() {
    World scene;

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
        World s2;
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
        World s3;
        auto p = s3.create("parent");
        auto c = s3.create("child");
        s3.setParent(c, p);
        s3.updateWorldTransforms(); // both clean
        s3.destroy(p);
        require(!s3.registry().valid(p), "parent destroyed");
        require(s3.registry().valid(c), "child survives");
        require(s3.registry().get<Parent>(c).value == entt::null,
                "child orphaned");
        // The orphan's world transform still has the dead parent's baked in,
        // and nothing else will ever ask for it to be recomputed.
        require(s3.registry().all_of<Dirty>(c),
                "an orphan is re-dirtied so its world transform stops lying");
    }

    // destroyHierarchy() takes the subtree: a rig is one object.
    {
        World sh;
        auto root = sh.create("root");
        auto mid = sh.create("mid");
        auto leaf = sh.create("leaf");
        auto bystander = sh.create("bystander");
        sh.setParent(mid, root);
        sh.setParent(leaf, mid);
        sh.destroyHierarchy(root);
        require(!sh.registry().valid(root) && !sh.registry().valid(mid) &&
                    !sh.registry().valid(leaf),
                "the whole subtree is destroyed");
        require(sh.registry().valid(bystander), "and nothing else is");
        sh.destroyHierarchy(root); // already gone
        require(sh.registry().valid(bystander), "destroying twice is safe");
    }

    // A parent that never went through create() -- what a deserialised map is
    // made of -- has neither Dirty nor a WorldTransform. Resolving a child of
    // one used to read a component that was not there.
    {
        World sd;
        entt::registry& reg = sd.registry();
        const entt::entity group = reg.create();
        reg.emplace<Transform>(group, Transform{glm::vec3(4.0f, 0.0f, 0.0f),
                                                glm::quat(1, 0, 0, 0),
                                                glm::vec3(1.0f)});
        const entt::entity child = reg.create();
        reg.emplace<Transform>(child, Transform{glm::vec3(1.0f, 0.0f, 0.0f),
                                                glm::quat(1, 0, 0, 0),
                                                glm::vec3(1.0f)});
        sd.setParent(child, group); // dirties the child only
        sd.updateWorldTransforms();
        require(reg.all_of<WorldTransform>(group),
                "an unresolved parent is resolved on demand");
        require(reg.get<WorldTransform>(child).matrix[3][0] == 5.0f,
                "and the child composes against it");
    }

    // An entity with no Transform at all (the add/remove menu can take one off)
    // must not take the resolve pass down with it.
    {
        World sn;
        entt::registry& reg = sn.registry();
        const entt::entity bare = sn.create("bare");
        reg.remove<Transform>(bare);
        sn.updateWorldTransforms();
        require(reg.get<WorldTransform>(bare).matrix == glm::mat4(1.0f),
                "a transformless entity resolves to identity");
    }

    // --- transforms ---
    World s2t;
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
    World s3h;
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
    World su;
    const entt::entity up = su.create("up_parent");
    const entt::entity uc = su.create("up_child");
    su.setParent(uc, up);
    require(su.registry().get<Children>(up).value.size() == 1, "child linked");
    su.setParent(uc, entt::null);
    require(su.registry().get<Parent>(uc).value == entt::null, "child unparented");
    require(su.registry().get<Children>(up).value.empty(),
            "child removed from old parent on unparent");

    // --- 3-level dirty propagation chain ---
    World sc;
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
    World scy;
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
