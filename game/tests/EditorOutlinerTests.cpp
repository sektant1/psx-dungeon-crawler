// The outliner's grouping, without a window.
//
// The property that matters: repeats of one prefab collapse into a single row,
// and the order is a function of the document alone -- placing a wall must not
// reshuffle the panel under the cursor.

#include "OutlinerTree.h"

#include <cstdlib>
#include <iostream>
#include <string>

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

int main()
{
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

    // --- kinds --------------------------------------------------------------
    require(std::string(entityKind(enemy, catalog)) == "enemy",
            "gameplay wins over geometry in the kind tag");
    require(std::string(entityKind(*document.find("wall_0001"), catalog)) ==
                "MISSING",
            "an unresolved prefab is called out rather than guessed");

    std::cout << "EditorOutlinerTests: ok\n";
    return 0;
}
