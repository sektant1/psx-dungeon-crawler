// Parent/child transforms in an authored scene.
//
// The properties that matter:
//   1. A scene with no parents cooks exactly as it did before hierarchies
//      existed -- every .scn and every .map in the repo predates this.
//   2. A child follows its parent, in the game and not only in the viewport.
//   3. A damaged chain never hangs and never crashes. A cycle is something an
//      author can author, and the editor has to be able to open the file that
//      contains one in order to fix it.

#include <editor/content/KitCatalog.h>
#include <editor/content/SceneCook.h>
#include <editor/content/SceneDocument.h>
#include <editor/content/SceneValidate.h>

#include <eng/ecs/Components.h>

#include <unordered_map>

#include <cstdlib>
#include <iostream>
#include <string>

using namespace game::content;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "SceneHierarchyTests: " << message << '\n';
        std::exit(1);
    }
}

static bool near(const glm::vec3& a, const glm::vec3& b, float epsilon = 1e-4f)
{
    return glm::all(glm::lessThan(glm::abs(a - b), glm::vec3(epsilon)));
}

static Entity at(const AuthorId& id, glm::vec3 position, float yaw = 0.0f)
{
    Entity entity;
    entity.id = id;
    entity.transform.position = position;
    entity.transform.rotationDegrees.y = yaw;
    return entity;
}

static bool hasCode(const std::vector<Issue>& issues, const std::string& code)
{
    for (const Issue& issue : issues)
        if (issue.code == code)
            return true;
    return false;
}

static const Issue* issueFor(const std::vector<Issue>& issues,
                             const std::string& code)
{
    for (const Issue& issue : issues)
        if (issue.code == code)
            return &issue;
    return nullptr;
}

int main()
{
    // --- no parent: the transform is the world, untouched -------------------
    {
        SceneDocument doc;
        doc.add(at("free_0001", {3.0f, 1.0f, -2.0f}, 45.0f));

        const WorldTransform world = doc.worldTransform("free_0001");
        require(near(world.position, {3.0f, 1.0f, -2.0f}),
                "an unparented entity resolves to its own position");
        require(near(world.scale, {1.0f, 1.0f, 1.0f}), "and its own scale");
        require(near(authorRotationDegrees(world.orientation), {0.0f, 45.0f, 0.0f}),
                "and its own rotation -- this is every entity in every scene "
                "authored before hierarchies existed");
    }

    // --- a child is placed in its parent's frame -----------------------------
    {
        SceneDocument doc;
        doc.add(at("wall_0001", {10.0f, 0.0f, 0.0f}));
        Entity torch = at("torch_0001", {0.0f, 2.0f, 0.5f});
        torch.parent = "wall_0001";
        doc.add(torch);

        const WorldTransform world = doc.worldTransform("torch_0001");
        require(near(world.position, {10.0f, 2.0f, 0.5f}),
                "the child's offset is added to where its parent stands");
    }

    // --- and rotates with it -------------------------------------------------
    {
        SceneDocument doc;
        doc.add(at("wall_0001", {0.0f, 0.0f, 0.0f}, 90.0f));
        Entity torch = at("torch_0001", {0.0f, 0.0f, 2.0f});
        torch.parent = "wall_0001";
        doc.add(torch);

        const WorldTransform world = doc.worldTransform("torch_0001");
        // Yaw is measured about +Y, so a quarter turn sends +Z to +X.
        require(near(world.position, {2.0f, 0.0f, 0.0f}),
                "turning the parent swings the child around it, rather than "
                "leaving it behind at its authored offset");
        require(near(authorRotationDegrees(world.orientation), {0.0f, 90.0f, 0.0f}),
                "and the child inherits the facing");
    }

    // --- scale reaches the child's offset and its own size ------------------
    {
        SceneDocument doc;
        Entity room = at("room_0001", {0.0f, 0.0f, 0.0f});
        room.transform.scale = glm::vec3(2.0f);
        doc.add(room);
        Entity pillar = at("pillar_0001", {1.0f, 0.0f, 0.0f});
        pillar.transform.scale = glm::vec3(3.0f);
        pillar.parent = "room_0001";
        doc.add(pillar);

        const WorldTransform world = doc.worldTransform("pillar_0001");
        require(near(world.position, {2.0f, 0.0f, 0.0f}),
                "the parent's scale stretches where the child sits");
        require(near(world.scale, {6.0f, 6.0f, 6.0f}),
                "and multiplies how big it is");
    }

    // --- chains compose all the way to the root -----------------------------
    {
        SceneDocument doc;
        doc.add(at("a", {1.0f, 0.0f, 0.0f}));
        Entity b = at("b", {1.0f, 0.0f, 0.0f});
        b.parent = "a";
        doc.add(b);
        Entity c = at("c", {1.0f, 0.0f, 0.0f});
        c.parent = "b";
        doc.add(c);

        require(near(doc.worldTransform("c").position, {3.0f, 0.0f, 0.0f}),
                "three links deep still adds up");
    }

    // --- walking the tree ----------------------------------------------------
    {
        SceneDocument doc;
        doc.add(at("root", {}));
        Entity child = at("child", {});
        child.parent = "root";
        doc.add(child);
        Entity grandchild = at("grandchild", {});
        grandchild.parent = "child";
        doc.add(grandchild);
        doc.add(at("stranger", {}));

        require(doc.childrenOf("root").size() == 1, "one direct child");
        require(doc.childrenOf("").size() == 2,
                "an empty id asks for the roots, and 'stranger' is one");
        const std::vector<AuthorId> below = doc.descendantsOf("root");
        require(below.size() == 2, "both levels are descendants");
        require(doc.descendantsOf("grandchild").empty(), "a leaf has none");
    }

    // --- a cycle terminates, in every direction -----------------------------
    {
        SceneDocument doc;
        Entity a = at("a", {1.0f, 0.0f, 0.0f});
        a.parent = "b";
        doc.add(a);
        Entity b = at("b", {1.0f, 0.0f, 0.0f});
        b.parent = "a";
        doc.add(b);

        // The point is that these return at all: a document with a loop is one
        // the editor has to open, because opening it is how it gets fixed.
        const WorldTransform world = doc.worldTransform("a");
        require(near(world.position, {2.0f, 0.0f, 0.0f}),
                "the chain stops at the link that closes the loop");
        require(doc.descendantsOf("a").size() == 1,
                "and walking down visits each entity once");

        const std::vector<Issue> issues = validate(doc, KitCatalog{});
        require(hasCode(issues, "parent.cycle"), "the loop is reported");
    }

    // --- a self-parent is a cycle of one ------------------------------------
    {
        SceneDocument doc;
        Entity lonely = at("lonely", {5.0f, 0.0f, 0.0f});
        lonely.parent = "lonely";
        doc.add(lonely);

        require(near(doc.worldTransform("lonely").position, {5.0f, 0.0f, 0.0f}),
                "it resolves as if it had no parent");
        require(hasCode(validate(doc, KitCatalog{}), "parent.self"),
                "and is reported as its own parent, not as a generic cycle");
    }

    // --- a parent that is not in the document -------------------------------
    {
        SceneDocument doc;
        Entity orphan = at("orphan", {4.0f, 0.0f, 0.0f});
        orphan.parent = "deleted_0001";
        doc.add(orphan);

        require(near(doc.worldTransform("orphan").position, {4.0f, 0.0f, 0.0f}),
                "a missing parent resolves as the world -- the entity is still "
                "drawn, which is what makes it selectable and fixable");
        const std::vector<Issue> issues = validate(doc, KitCatalog{});
        const Issue* issue = issueFor(issues, "parent.missing");
        require(issue != nullptr, "and the break is reported");
        require(issue->severity == Severity::Error,
                "as an error: a level that silently moves an entity is worse "
                "than one that refuses to cook");
        require(issue->fix == QuickFix::ClearParent,
                "with a fix that detaches rather than deletes");
    }

    // --- the fix keeps the entity where it was drawn ------------------------
    {
        SceneDocument doc;
        doc.add(at("wall_0001", {10.0f, 0.0f, 0.0f}, 90.0f));
        Entity torch = at("torch_0001", {0.0f, 0.0f, 2.0f});
        torch.parent = "wall_0001";
        doc.add(torch);
        // Break the chain the way a delete would.
        doc.find("torch_0001")->parent = "gone";

        Issue issue;
        issue.code = "parent.missing";
        issue.entity = "torch_0001";
        issue.fix = QuickFix::ClearParent;
        require(applyQuickFix(doc, KitCatalog{}, issue), "the fix applies");

        const Entity* fixed = doc.find("torch_0001");
        require(fixed->parent.empty(), "the link is gone");
        require(near(fixed->transform.position, {0.0f, 0.0f, 2.0f}),
                "and it kept the place it was resolving to -- which, with the "
                "parent already missing, was its own transform");
        require(validate(doc, KitCatalog{}).empty() ||
                    !hasCode(validate(doc, KitCatalog{}), "parent.missing"),
                "and the issue is gone");
    }

    // --- cycle refusal, before one can be created ---------------------------
    {
        SceneDocument doc;
        doc.add(at("a", {}));
        Entity b = at("b", {});
        b.parent = "a";
        doc.add(b);
        Entity c = at("c", {});
        c.parent = "b";
        doc.add(c);

        require(doc.wouldCycle("a", "c"),
                "parenting an ancestor to its own descendant would loop");
        require(doc.wouldCycle("a", "a"), "and so would parenting to itself");
        require(!doc.wouldCycle("c", "a"),
                "but re-parenting a descendant further up is fine");
        require(!doc.wouldCycle("a", ""),
                "and detaching always terminates");
        require(!doc.wouldCycle("a", "nowhere"),
                "a parent that does not exist cannot close a loop");
    }

    // --- a live chain keeps its links; a static one is still baked flat ------
    // Baking a hierarchy into world transforms is exact only while the chain is
    // static. A camera under a spinning pivot baked flat is a camera that does
    // not orbit, and the failure is silent: the scene loads and draws.
    {
        SceneDocument doc;
        Entity pivot = at("pivot", {0.0f, 0.0f, 0.0f});
        pivot.spin = SpinAuthor{{0.0f, 1.0f, 0.0f}, 30.0f};
        doc.add(pivot);
        Entity orbiting = at("orbiting", {0.0f, 0.0f, 5.0f});
        orbiting.parent = "pivot";
        orbiting.camera = CameraAuthor{};
        doc.add(orbiting);

        Entity anchor = at("anchor", {10.0f, 0.0f, 0.0f});
        doc.add(anchor);
        Entity mounted = at("mounted", {0.0f, 0.0f, 2.0f});
        mounted.parent = "anchor";
        doc.add(mounted);

        entt::registry built;
        std::string error;
        std::unordered_map<AuthorId, entt::entity> ids;
        require(buildRegistry(doc, KitCatalog{}, built, error, &ids),
                "the scene cooks: " + error);

        const entt::entity live = ids.at("orbiting");
        require(built.all_of<eng::ecs::Parent>(live),
                "a child of a spinning pivot keeps its link");
        require(built.get<eng::ecs::Parent>(live).value == ids.at("pivot"),
                "and it points at the pivot");
        require(near(built.get<eng::ecs::Transform>(live).position,
                     {0.0f, 0.0f, 5.0f}),
                "with its LOCAL transform, for the runtime to compose");
        require(built.all_of<eng::ecs::Camera>(live), "and it is a camera");

        const entt::entity baked = ids.at("mounted");
        require(!built.all_of<eng::ecs::Parent>(baked),
                "a static chain is still baked flat, as every shipped .map is");
        require(near(built.get<eng::ecs::Transform>(baked).position,
                     {10.0f, 0.0f, 2.0f}),
                "with its world transform resolved once by the cooker");
    }

    std::cout << "SceneHierarchyTests: ok\n";
    return 0;
}
