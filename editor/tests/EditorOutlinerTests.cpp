// The outliner's grouping, without a window.
//
// The property that matters: repeats of one prefab collapse into a single row,
// and the order is a function of the document alone -- placing a wall must not
// reshuffle the panel under the cursor.

#include <editor/scene/OutlinerTree.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_set>

using namespace ed;
using game::content::Entity;
using game::content::KitCatalog;
using game::content::SceneDocument;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "EditorOutlinerTests: " << message << '\n';
        std::exit(1);
    }
}

static Entity piece(const std::string& id, const std::string& prefab)
{
    Entity entity;
    entity.id = id;
    entity.name = id;
    entity.prefab = prefab;
    return entity;
}

static const OutlinerGroup* group(const OutlinerTree& tree,
                                  const std::string& key)
{
    for (const OutlinerGroup& candidate : tree.groups)
        if (candidate.key == key)
            return &candidate;
    return nullptr;
}


// Hiding geometry in hierarchy mode must hide it, not relabel it.
//
// buildNode is what marks a subtree visited, and the shared invalid-roots tail
// reports anything unvisited as an orphan or a cycle -- so skipping the build
// for hidden geometry brought every hidden row back wearing an
// "[invalid hierarchy]" warning.
static void testHiddenGeometryIsNotReportedInvalid()
{
    SceneDocument document;
    KitCatalog catalog;

    Entity& parent = document.entities.emplace_back();
    parent.id = "crate";
    // A prefab, because that is what isGeometry() means by geometry: a kit
    // piece carrying nothing else.
    parent.prefab = "kit.wall";
    Entity& child = document.entities.emplace_back();
    child.id = "crate_lid";
    child.parent = "crate";
    child.prefab = "kit.wall";

    OutlinerOptions options;
    options.groupRepeats = false; // hierarchy mode
    options.showGeometry = false;

    const OutlinerTree tree = buildOutliner(document, catalog, options);
    for (const OutlinerGroup& group : tree.groups) {
        require(group.label.find("invalid") == std::string::npos,
                "hidden geometry must not come back labelled invalid");
    }
    require(tree.groups.empty(), "it is hidden, so there is nothing to show");
    require(tree.hidden == 2,
            "and the count is the whole subtree, not one row");
}

int main()
{
    testHiddenGeometryIsNotReportedInvalid();
    // An empty catalogue: prefabs do not resolve, which is exactly the state a
    // scene is in when kit.toml lost a piece. Grouping must survive it.
    KitCatalog catalog;
    SceneDocument document;
    document.add(piece("wall_0003", "kit.wall"));
    document.add(piece("wall_0001", "kit.wall"));
    document.add(piece("wall_0002", "kit.wall"));
    document.add(piece("floor_0001", "kit.floor"));

    Entity enemy;
    enemy.id = "enemy_0001";
    enemy.name = "gatekeeper";
    enemy.enemySpawn = "goblin";
    document.add(enemy);

    Entity light;
    light.id = "light_0001";
    light.name = "brazier";
    light.light = game::content::LightAuthor{};
    document.add(light);

    OutlinerTree tree = buildOutliner(document, catalog, OutlinerOptions{});
    require(tree.shown == 6 && tree.hidden == 0, "everything is listed");
    require(tree.groups.size() == 4, "three walls collapse into one row");

    const OutlinerGroup* walls = group(tree, "kit.wall");
    require(walls && walls->nodes.size() == 3,
            "the wall group holds all three");
    require(walls->nodes[0].id == "wall_0001" &&
                walls->nodes[2].id == "wall_0003",
            "nodes are sorted by id, not by placement order");
    require(walls->geometry, "a bare kit piece group is geometry");
    require(!group(tree, "enemy")->geometry, "a spawn group is not");

    // Gameplay first, then geometry, each alphabetically: a stable panel.
    require(!tree.groups.front().geometry && tree.groups.back().geometry,
            "gameplay groups sort above geometry");
    require(tree.groups[0].key == "enemy" && tree.groups[1].key == "light",
            "and are alphabetical among themselves");

    // --- the geometry fold --------------------------------------------------
    OutlinerOptions folded;
    folded.showGeometry = false;
    tree = buildOutliner(document, catalog, folded);
    require(tree.shown == 2 && tree.hidden == 4, "the fold hides kit pieces");
    require(group(tree, "kit.wall") == nullptr, "and their group with them");

    // A gameplay component on a kit piece keeps it visible: that is the entity
    // somebody is hunting for, and it happens to have a mesh.
    Entity trap = piece("wall_0004", "kit.wall");
    trap.trigger = game::content::TriggerAuthor{};
    document.add(trap);
    tree = buildOutliner(document, catalog, folded);
    const OutlinerGroup* mixed = group(tree, "kit.wall");
    require(mixed && mixed->nodes.size() == 1,
            "the trapped wall survives the fold");
    require(mixed->geometry,
            "and its group is still the kit group, not retagged by one member");
    require(mixed->kind == "MISSING" && mixed->nodes.front().kind == "trigger",
            "the group keeps the piece's identity, the row keeps the entity's");

    // --- filtering ----------------------------------------------------------
    OutlinerOptions filtered;
    filtered.filter = "GATEKEEPER"; // case-insensitive, on the display name
    tree = buildOutliner(document, catalog, filtered);
    require(tree.shown == 1 && tree.groups.size() == 1,
            "the filter matches the name whatever its case");

    filtered.filter = "kit.floor"; // and on the prefab id
    tree = buildOutliner(document, catalog, filtered);
    require(tree.shown == 1 && tree.groups.front().key == "kit.floor",
            "the filter matches the prefab");

    filtered.filter = "nothing_here";
    tree = buildOutliner(document, catalog, filtered);
    require(tree.groups.empty() && tree.shown == 0, "a miss lists nothing");

    // --- composed objects ---------------------------------------------------
    // Parenting is what makes a chandelier and its candles one thing. The panel
    // has to show that as a tree, and it must not let the prefab grouping --
    // which exists to collapse a hundred identical walls -- swallow it.
    {
        SceneDocument composed;
        composed.add(piece("chandelier_0001", "kit.prop_chandelier"));
        for (int i = 1; i <= 3; ++i) {
            Entity candle =
                piece("candle_000" + std::to_string(i), "kit.prop_candle");
            candle.parent = "chandelier_0001";
            composed.add(candle);
        }
        // A loose candle of the same prefab, standing on its own.
        composed.add(piece("candle_0009", "kit.prop_candle"));

        OutlinerTree hierarchy =
            buildOutliner(composed, catalog, OutlinerOptions{});
        const OutlinerGroup* object = group(hierarchy, "chandelier_0001");
        require(
            object && object->composed,
            "the composed object gets a group of its own, keyed by its root");
        require(object->nodes.size() == 1, "holding one node -- the root");
        require(object->nodes.front().children.size() == 3,
                "with its children nested under it, rather than collapsed into "
                "a 'kit.prop_candle (3)' row somewhere else in the panel");
        require(groupIds(*object).size() == 4,
                "and acting on the group reaches the whole chain");

        const OutlinerGroup* loose = group(hierarchy, "kit.prop_candle");
        require(loose && loose->nodes.size() == 1,
                "the unparented candle still groups by prefab -- being part of "
                "an object is what moves an entity out of that grouping, not "
                "sharing a prefab with one that is");
        require(hierarchy.shown == 5, "every entity is accounted for");
    }

    // --- filtering a composed object keeps it whole -------------------------
    {
        SceneDocument composed;
        composed.add(piece("rig_0001", "kit.prop_rig"));
        Entity lamp = piece("lamp_0001", "kit.prop_lamp");
        lamp.parent = "rig_0001";
        composed.add(lamp);

        OutlinerOptions hunt;
        hunt.filter = "lamp";
        const OutlinerTree found = buildOutliner(composed, catalog, hunt);
        require(found.groups.size() == 1 && found.shown == 2,
                "matching a child keeps the whole object: half a chandelier is "
                "not a shorter list, it is a broken one");

        hunt.filter = "nothing";
        const OutlinerTree empty = buildOutliner(composed, catalog, hunt);
        require(empty.groups.empty() && empty.hidden == 2,
                "and a miss drops it whole, counted whole");

        hunt.filter = "kit.prop_lamp";
        const OutlinerTree byPrefab = buildOutliner(composed, catalog, hunt);
        require(byPrefab.groups.size() == 1 && byPrefab.shown == 2,
                "a composed child remains searchable by prefab");
    }

    // --- kinds --------------------------------------------------------------
    require(std::string(entityKind(enemy, catalog)) == "enemy",
            "gameplay wins over geometry in the kind tag");
    require(std::string(entityKind(*document.find("wall_0001"), catalog)) ==
                "MISSING",
            "an unresolved prefab is called out rather than guessed");

    // --- malformed hierarchies stay repairable -----------------------------
    {
        SceneDocument malformed;
        Entity orphan = piece("orphan", "kit.prop");
        orphan.parent = "missing";
        malformed.add(orphan);
        Entity self = piece("self", "kit.prop");
        self.parent = "self";
        malformed.add(self);
        Entity a = piece("cycle_a", "kit.prop");
        Entity b = piece("cycle_b", "kit.prop");
        a.parent = b.id;
        b.parent = a.id;
        malformed.add(a);
        malformed.add(b);

        const OutlinerTree visible =
            buildOutliner(malformed, catalog, OutlinerOptions{});
        require(visible.shown == malformed.entities.size(),
                "orphans, self-parents and cycles remain visible");
        std::unordered_set<std::string> ids;
        bool labelledInvalid = false;
        for (const OutlinerGroup& candidate : visible.groups) {
            labelledInvalid = labelledInvalid || candidate.invalid;
            for (const std::string& id : groupIds(candidate))
                require(ids.insert(id).second,
                        "every malformed entity appears exactly once");
        }
        require(ids.size() == malformed.entities.size() && labelledInvalid,
                "invalid hierarchy rows are explicit and complete");
    }

    std::cout << "EditorOutlinerTests: ok\n";
    return 0;
}
