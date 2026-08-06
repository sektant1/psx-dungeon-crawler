// Authoring layers: the queries every panel shares, and the per-layer extract
// and merge that make the chapter's division-of-labour claim (Gregory §15.4.1.5)
// real rather than aspirational.
//
// All of it is pure document logic, so none of it needs a window -- which is
// the point of Layers.cpp being its own file rather than four hundred lines
// inside the editor's frame callback.

#include <editor/scene/Layers.h>

#include <cstdlib>
#include <iostream>
#include <string>

using namespace game::content;
namespace layers = ed::layers;

static int gFailures = 0;

static void check(bool condition, const std::string& what)
{
    if (!condition) {
        std::cerr << "EditorLayerTests: " << what << '\n';
        ++gFailures;
    }
}

static Entity makeEntity(const std::string& id, const std::string& layer)
{
    Entity entity;
    entity.id = id;
    entity.name = id;
    entity.layer = layer;
    return entity;
}

// A scene with three layers of work in it, which is the shape the feature
// exists for: background, lighting and the people.
static SceneDocument sample()
{
    SceneDocument document;
    document.id = "scene.layers";
    document.layers.push_back(Layer{"bg", "Background", {0.5f, 0.5f, 0.5f}});
    document.layers.push_back(Layer{"lighting", "Lighting", {1.0f, 0.8f, 0.3f}});
    document.layers.push_back(Layer{"npc", "Characters", {0.3f, 0.7f, 1.0f}});

    document.add(makeEntity("wall_0001", "bg"));
    document.add(makeEntity("wall_0002", "bg"));
    document.add(makeEntity("torch_0001", "lighting"));
    document.add(makeEntity("smith_0001", "npc"));
    document.add(makeEntity("spawn_0001", "")); // the default layer
    return document;
}

static void testVisibility()
{
    layers::LayerSession session;
    check(!layers::isHidden(session, "bg"), "nothing is hidden by default");

    layers::setHidden(session, "bg", true);
    check(layers::isHidden(session, "bg"), "a hidden layer reports hidden");
    check(!layers::isHidden(session, "lighting"), "its neighbours do not");

    // Solo overrides the hidden list without destroying it: that is what lets
    // leaving solo put the author back where they were rather than showing
    // everything.
    layers::toggleSolo(session, "lighting");
    check(layers::isHidden(session, "bg"), "solo hides the rest");
    check(!layers::isHidden(session, "lighting"), "solo shows its own layer");
    check(layers::isHidden(session, ""), "solo hides the default layer too");

    layers::toggleSolo(session, "lighting");
    check(!session.soloing(), "toggling the soloed layer leaves solo");
    check(layers::isHidden(session, "bg"), "the hidden list survived solo");
    check(!layers::isHidden(session, "lighting"), "and lighting is back");

    layers::setHidden(session, "bg", false);
    check(!layers::isHidden(session, "bg"), "un-hiding works");

    const Entity torch = makeEntity("torch_0002", "lighting");
    layers::setLocked(session, "lighting", true);
    check(layers::locksEntity(session, torch), "a locked layer locks its members");
    check(!layers::hidesEntity(session, torch), "locking does not hide");
}

static void testRows()
{
    SceneDocument document = sample();
    // An entity in a layer nobody declared -- a botched merge, or a hand edit.
    document.add(makeEntity("orphan_0001", "vfx"));

    const std::vector<std::string> ids = layers::layerIds(document);
    check(!ids.empty() && ids.front().empty(),
          "the default layer is listed first");
    check(ids.size() == 5, "declared layers and the undeclared one are listed");

    check(!document.hasLayer("vfx"), "an undeclared layer is not declared");
    check(document.hasLayer(""), "the default layer always exists");
    check(document.hasLayer("bg"), "a declared layer exists");

    const std::vector<layers::LayerStat> rows = layers::stats(document);
    check(rows.size() == 5, "one row per layer");
    check(rows[0].id.empty() && rows[0].entities == 1,
          "the default layer counts its one entity");
    for (const layers::LayerStat& row : rows) {
        if (row.id == "bg")
            check(row.entities == 2 && !row.undeclared, "bg holds two walls");
        if (row.id == "vfx")
            check(row.entities == 1 && row.undeclared,
                  "the undeclared layer is counted and flagged");
    }

    const std::vector<AuthorId> members = layers::membersOf(document, "bg");
    check(members.size() == 2 && members[0] == "wall_0001",
          "members come back in document order");
}

// The lighting team opens only their own layer. The torches hang off a wall
// they do not own, so the extract has to bring that wall along as a stub or
// every torch in the file lands at the origin.
static void testExtractCarriesAncestors()
{
    SceneDocument document;
    document.id = "scene.extract";
    document.layers.push_back(Layer{"lighting", "Lighting", {1, 1, 1}});

    Entity wall = makeEntity("wall_0001", "bg");
    wall.prefab = "kit.wall";
    wall.transform.position = {10.0f, 0.0f, 4.0f};
    document.add(wall);

    Entity torch = makeEntity("torch_0001", "lighting");
    torch.parent = "wall_0001";
    torch.transform.position = {0.0f, 2.0f, 0.0f};
    torch.light = LightAuthor{};
    document.add(torch);

    const SceneDocument out = layers::extractLayer(document, "lighting");
    check(out.entities.size() == 2, "the member and its ancestor came across");
    const Entity* stub = out.find("wall_0001");
    check(stub != nullptr, "the ancestor is present");
    check(stub && stub->prefab.empty(),
          "the ancestor came as a bare transform, not a second copy of the wall");
    check(stub && stub->transform.position.x == 10.0f,
          "the ancestor keeps its transform, so the child still resolves");
    const Entity* member = out.find("torch_0001");
    check(member && member->light.has_value(),
          "the member came across whole");
    check(out.findLayer("lighting") != nullptr,
          "the extracted file declares the layer it holds");
}

// The lighting team hands their work back. The wall is already in the scene,
// so it must be reused rather than duplicated; a torch whose id collides with
// something added meanwhile must be renamed rather than overwrite it.
static void testMergeReusesAncestorsAndRenamesCollisions()
{
    SceneDocument document;
    document.id = "scene.merge";
    Entity wall = makeEntity("wall_0001", "bg");
    wall.prefab = "kit.wall";
    document.add(wall);
    document.add(makeEntity("torch_0001", "")); // added meanwhile, unrelated

    SceneDocument incoming;
    incoming.layers.push_back(Layer{"lighting", "Lighting", {1, 1, 1}});
    incoming.add(makeEntity("wall_0001", "bg")); // the ancestor stub
    Entity torch = makeEntity("torch_0001", "lighting");
    torch.parent = "wall_0001";
    incoming.add(torch);

    const layers::MergeReport report =
        layers::mergeLayer(document, incoming, "lighting");

    check(report.skipped == 1, "the ancestor already present was reused");
    check(report.added == 1, "only the member was added");
    check(report.renamed.size() == 1, "the colliding id was renamed");
    check(document.entities.size() == 3, "the wall was not duplicated");

    const AuthorId renamed = report.renamed.front().second;
    check(renamed != "torch_0001", "the rename actually changed the id");
    const Entity* merged = document.find(renamed);
    check(merged != nullptr, "the renamed entity is in the document");
    check(merged && merged->parent == "wall_0001",
          "its parent still points at the reused ancestor");
    check(merged && merged->layer == "lighting",
          "the merged entity landed in the target layer");
    check(document.findLayer("lighting") != nullptr,
          "the target layer was declared on the way in");

    // The entity that was already there is untouched -- the whole point of
    // renaming rather than overwriting.
    const Entity* original = document.find("torch_0001");
    check(original && original->layer.empty(),
          "the pre-existing entity kept its own layer");
}

// Two incoming entities colliding with the same document id must not both be
// offered the same free name.
static void testMergeRenamesTwoWays()
{
    SceneDocument document;
    document.add(makeEntity("prop_0001", ""));

    SceneDocument incoming;
    incoming.add(makeEntity("prop_0001", "dressing"));
    Entity second = makeEntity("prop_0002", "dressing");
    incoming.add(second);
    // A second collision against the same stem.
    document.add(makeEntity("prop_0002", ""));

    const layers::MergeReport report =
        layers::mergeLayer(document, incoming, "dressing");
    check(report.renamed.size() == 2, "both collisions were renamed");
    check(report.renamed[0].second != report.renamed[1].second,
          "the two renames are distinct");
    check(document.entities.size() == 4, "everything landed");
}

int main()
{
    testVisibility();
    testRows();
    testExtractCarriesAncestors();
    testMergeReusesAncestorsAndRenamesCollisions();
    testMergeRenamesTwoWays();

    if (gFailures != 0) {
        std::cerr << "EditorLayerTests: " << gFailures << " failure(s)\n";
        return 1;
    }
    std::cout << "EditorLayerTests: ok\n";
    return 0;
}
