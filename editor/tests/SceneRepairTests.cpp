// The two mechanical repairs, and the property that makes each of them safe to
// run on a shipped level without looking at the result afterwards.
//
//   repairCellRecords          never moves an entity
//   removeDuplicatePlacements  never removes anything that was drawing a pixel
//                              the survivor does not draw

#include <editor/content/KitCatalog.h>
#include <editor/content/SceneRepair.h>
#include <editor/content/SceneValidate.h>
#include "TestAssets.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace game::content;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "SceneRepairTests: " << message << '\n';
        std::exit(1);
    }
}

Entity prop(const std::string& id, glm::vec3 position,
            const std::string& prefab = "kit.prop_barrel")
{
    Entity entity;
    entity.id = id;
    entity.prefab = prefab;
    entity.transform.position = position;
    return entity;
}

void duplicatesAreRemoved()
{
    SceneDocument document;
    document.entities.push_back(prop("a", {4.0f, 0.0f, 4.0f}));
    document.entities.push_back(prop("b", {4.0f, 0.0f, 4.0f})); // exact copy
    document.entities.push_back(prop("c", {4.0f, 0.0f, 4.0f})); // and another
    document.entities.push_back(prop("d", {8.0f, 0.0f, 4.0f})); // elsewhere

    const DuplicateReport report = removeDuplicatePlacements(document);
    require(report.removed == 2, "both copies should go");
    require(report.groups == 1, "they are one placement, not two");
    require(document.entities.size() == 2, "the original and the other remain");
    require(document.entities[0].id == "a",
            "the FIRST in document order survives, so the result is stable "
            "and an author's undo history still names something real");
}

void aNudgedCopyIsNotADuplicate()
{
    SceneDocument document;
    document.entities.push_back(prop("a", {4.0f, 0.0f, 4.0f}));
    document.entities.push_back(prop("b", {4.0f, 0.001f, 4.0f}));
    require(removeDuplicatePlacements(document).removed == 0,
            "a millimetre apart is two decisions, not one mistake -- this "
            "compares exactly on purpose");
}

void entitiesThatMeanSomethingAreKept()
{
    // Two spawns in one place is a question for an author (and its own error);
    // silently deleting one would answer it wrongly and invisibly. Same for a
    // light, a trigger, a marker: the mesh is not all they are.
    SceneDocument document;
    Entity first = prop("spawn_a", {0.0f, 0.0f, 0.0f});
    first.playerSpawn = true;
    Entity second = prop("spawn_b", {0.0f, 0.0f, 0.0f});
    second.playerSpawn = true;
    document.entities.push_back(first);
    document.entities.push_back(second);

    Entity lightA = prop("light_a", {4.0f, 0.0f, 0.0f});
    lightA.light = LightAuthor{};
    Entity lightB = prop("light_b", {4.0f, 0.0f, 0.0f});
    lightB.light = LightAuthor{};
    document.entities.push_back(lightA);
    document.entities.push_back(lightB);

    require(removeDuplicatePlacements(document).removed == 0,
            "gameplay entities carry meaning past their mesh and are kept");
}

void aParentIsNeverDropped()
{
    // The survivor may not be the one the child names, so dropping a parent
    // would orphan it -- a worse state than the duplicate.
    SceneDocument document;
    document.entities.push_back(prop("root_a", {0.0f, 0.0f, 0.0f}));
    document.entities.push_back(prop("root_b", {0.0f, 0.0f, 0.0f}));
    Entity child = prop("child", {0.0f, 1.0f, 0.0f});
    child.parent = "root_b";
    document.entities.push_back(child);

    removeDuplicatePlacements(document);
    require(document.find("root_b") != nullptr,
            "the parent of a child must survive deduplication");
    require(document.find("child") != nullptr, "and so must the child");
}

void repairNeverMovesAnEntity()
{
    KitCatalog catalog;
    std::string error;
    require(KitCatalog::load(game::test::asset("config/kit.toml"), catalog,
                             error),
            error.empty() ? "kit did not load" : error.c_str());
    const GridConfig grid = GridConfig::fromCatalog(catalog);

    SceneDocument document;
    // A wall carrying a cell record that describes somewhere it is not.
    Entity drifted;
    drifted.id = "wall";
    drifted.prefab = "kit.wall";
    drifted.transform.position = {1.5f, 0.0f, -3.25f}; // off the grid entirely
    drifted.cell = CellPlacement{};
    drifted.cell->col = 7;
    drifted.cell->row = 7;
    document.entities.push_back(drifted);

    const glm::vec3 before = document.entities[0].transform.position;
    const CellRepairReport report = repairCellRecords(document, catalog, grid);
    require(report.changed() == 1, "the drifted record should be repaired");
    require(document.entities[0].transform.position == before,
            "repairing a cell record must never move the entity -- that is "
            "the whole property that makes it safe to run unattended");
}

} // namespace

int main()
{
    game::test::mountGameAssets();
    duplicatesAreRemoved();
    aNudgedCopyIsNotADuplicate();
    entitiesThatMeanSomethingAreKept();
    aParentIsNeverDropped();
    repairNeverMovesAnEntity();
    std::cout << "SceneRepairTests: ok\n";
    return 0;
}
