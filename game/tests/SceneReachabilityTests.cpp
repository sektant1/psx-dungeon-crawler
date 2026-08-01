// Every other validation rule asks whether the scene data is well formed. These
// cases ask the only question that decides whether a dungeon is playable: can
// the player walk from the spawn to the exit? A room with the exit sealed
// behind a wall ring validates clean and cooks clean, so nothing but this test
// stands between a generated layout and an unfinishable level.

#include "GridMath.h"
#include "SceneValidate.h"
#include "TestAssets.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace game::content;

static void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "SceneReachabilityTests: " << message << '\n';
        std::exit(1);
    }
}

static int count(const std::vector<Issue>& issues, const std::string& code)
{
    return int(std::count_if(issues.begin(), issues.end(),
                             [&](const Issue& i) { return i.code == code; }));
}

// Asserting on the count rather than on presence, because "one issue for the
// whole stranded group" is itself the contract: reporting a cell at a time
// buries the panel the moment a level splits in half.
static void requireCount(const std::vector<Issue>& issues,
                         const std::string& code, int expected,
                         const std::string& message)
{
    const int actual = count(issues, code);
    if (actual != expected) {
        std::cerr << "SceneReachabilityTests: " << message << " -- expected "
                  << expected << " '" << code << "' issue(s), got " << actual
                  << '\n';
        for (const Issue& issue : issues) {
            std::cerr << "    " << severityName(issue.severity) << ' '
                      << issue.code << " (" << issue.entity
                      << "): " << issue.message << '\n';
        }
        std::exit(1);
    }
}

namespace {

// The catalogue and grid are the same for every case; loaded once in main.
const KitCatalog* gCatalog = nullptr;

void place(SceneDocument& document, const std::string& id,
           const std::string& prefab, CellPlacement cell)
{
    const KitPiece* piece = gCatalog->find(prefab);
    require(piece != nullptr, prefab + " resolves in the kit");
    Entity entity;
    entity.id = id;
    entity.prefab = prefab;
    entity.cell = cell;
    // Authored through the same maths the editor uses, so these fixtures do not
    // trip cell.transform_drift and drown the codes under test.
    entity.transform = placementToTransform(GridConfig::fromCatalog(*gCatalog),
                                            *gCatalog, *piece, cell);
    document.add(entity);
}

void floorCell(SceneDocument& document, int col, int row)
{
    place(document, "floor_" + std::to_string(col) + "_" + std::to_string(row),
          "kit.floor",
          CellPlacement{col, row, CellPlacement::Edge::None, 1, 0, 0.0f});
}

void edgePiece(SceneDocument& document, const std::string& id,
               const std::string& prefab, int col, int row,
               CellPlacement::Edge edge)
{
    place(document, id, prefab, CellPlacement{col, row, edge, 1, 0, 0.0f});
}

void marker(SceneDocument& document, const std::string& id, int col, int row,
            bool isSpawn)
{
    const GridConfig grid = GridConfig::fromCatalog(*gCatalog);
    Entity entity;
    entity.id = id;
    entity.transform.position = cellCentre(grid, col, row, 0.0f);
    if (isSpawn)
        entity.playerSpawn = true;
    else
        entity.exitYawDegrees = 0.0f;
    document.add(entity);
}

// A 3x3 floored room, spawn in the north-west cell, exit in the south-east one,
// nothing between them. Every broken case below is this with something added.
SceneDocument openRoom()
{
    SceneDocument document;
    document.id = "scene.test.reachability";
    for (int col = 0; col < 3; ++col)
        for (int row = 0; row < 3; ++row)
            floorCell(document, col, row);
    marker(document, "spawn", 0, 0, true);
    marker(document, "exit", 2, 2, false);
    return document;
}

// Walls the exit cell (2,2) off from the rest of the room. Its south and east
// sides face unfloored space already, so two pieces are the whole ring.
void sealExit(SceneDocument& document, const std::string& prefab)
{
    edgePiece(document, "seal_north", prefab, 2, 2, CellPlacement::Edge::North);
    edgePiece(document, "seal_west", prefab, 2, 2, CellPlacement::Edge::West);
}

} // namespace

int main()
{
    game::test::mountGameAssets();
    KitCatalog catalog;
    std::string error;
    require(KitCatalog::load(game::test::asset("config/kit.toml"), catalog, error), error);
    gCatalog = &catalog;

    // --- the happy path ----------------------------------------------------
    {
        const std::vector<Issue> issues = validate(openRoom(), catalog);
        requireCount(issues, "exit.unreachable", 0,
                     "an open room reaches its own exit");
        requireCount(issues, "cell.unreachable", 0,
                     "and strands nothing");
        require(!blocksCook(issues), "so it cooks");
    }

    // --- the exit sealed behind walls ---------------------------------------
    {
        SceneDocument document = openRoom();
        sealExit(document, "kit.wall");
        const std::vector<Issue> issues = validate(document, catalog);
        requireCount(issues, "exit.unreachable", 1,
                     "a walled-off exit is reported");
        require(blocksCook(issues),
                "and blocks the cook: the level cannot be finished");
        for (const Issue& issue : issues) {
            if (issue.code != "exit.unreachable") continue;
            require(issue.severity == Severity::Error,
                    "the unreachable exit is an error, not a warning");
            require(issue.entity == "exit",
                    "and points at the exit, not at some wall");
        }
        // The sealed cell is walkable floor nobody can stand on, so it is
        // stranded too -- and that is one issue, not one per cell.
        requireCount(issues, "cell.unreachable", 1,
                     "the sealed cell is also reported as cut off");
    }

    // --- an opening in the ring ---------------------------------------------
    // The whole point of the Opening socket: an arch claims the same edge slot
    // as a wall and is passable. Getting this wrong would make every doorway in
    // the game read as a wall and fail every real level.
    {
        SceneDocument document = openRoom();
        sealExit(document, "kit.wall");
        document.remove("seal_west");
        edgePiece(document, "seal_west", "kit.arch", 2, 2,
                  CellPlacement::Edge::West);
        const std::vector<Issue> issues = validate(document, catalog);
        requireCount(issues, "exit.unreachable", 0,
                     "an exit reachable through an arch is fine");
        requireCount(issues, "cell.unreachable", 0, "and nothing is stranded");
        require(!blocksCook(issues), "so it cooks");
    }
    {
        // The same edge stated from the other side must mean the same boundary:
        // the north edge of (2,2) and the south edge of (2,1) are one wall.
        SceneDocument document = openRoom();
        edgePiece(document, "seal_north", "kit.wall", 2, 1,
                  CellPlacement::Edge::South);
        edgePiece(document, "seal_west", "kit.wall", 2, 2,
                  CellPlacement::Edge::West);
        requireCount(validate(document, catalog), "exit.unreachable", 1,
                     "a wall authored from the neighbour's side still seals");
    }

    // --- a solid block instead of a wall ------------------------------------
    // A Fill piece occupies the cell volume, so the cell it sits on is not
    // walkable even though a floor is still under it.
    {
        SceneDocument document = openRoom();
        for (int row = 0; row < 3; ++row)
            place(document, "block_" + std::to_string(row), "kit.block",
                  CellPlacement{1, row, CellPlacement::Edge::None, 1, 0, 0.0f});
        const std::vector<Issue> issues = validate(document, catalog);
        requireCount(issues, "exit.unreachable", 1,
                     "a column of blocks cuts the room in two");
    }

    // --- an island of floor --------------------------------------------------
    {
        SceneDocument document = openRoom();
        floorCell(document, 5, 5);
        floorCell(document, 6, 5);
        floorCell(document, 5, 6);
        const std::vector<Issue> issues = validate(document, catalog);
        requireCount(issues, "cell.unreachable", 1,
                     "three stranded cells are ONE issue, not three");
        requireCount(issues, "exit.unreachable", 0,
                     "the exit itself is still reachable");
        require(!blocksCook(issues),
                "stranded floor warns but does not block the cook");
        for (const Issue& issue : issues) {
            if (issue.code != "cell.unreachable") continue;
            require(issue.severity == Severity::Warning,
                    "stranded floor is a warning");
            require(issue.message.find('3') != std::string::npos,
                    "and the message says how many: " + issue.message);
        }
    }

    // --- the early outs ------------------------------------------------------
    // spawn.missing and exit.missing already name these causes; complaining a
    // second time about the same thing is noise in the panel.
    {
        SceneDocument document = openRoom();
        sealExit(document, "kit.wall");
        document.remove("spawn");
        const std::vector<Issue> issues = validate(document, catalog);
        requireCount(issues, "exit.unreachable", 0,
                     "no spawn means no reachability verdict");
        requireCount(issues, "cell.unreachable", 0, "and no stranded report");
    }
    {
        SceneDocument document = openRoom();
        sealExit(document, "kit.wall");
        document.remove("exit");
        requireCount(validate(document, catalog), "exit.unreachable", 0,
                     "no exit means no reachability verdict");
    }
    {
        // A scene authored with free transforms has no cells to flood. Passing
        // silently is honest: there is no topology here to be wrong about.
        SceneDocument document;
        document.id = "scene.test.gridless";
        marker(document, "spawn", 0, 0, true);
        marker(document, "exit", 9, 9, false);
        requireCount(validate(document, catalog), "exit.unreachable", 0,
                     "a gridless scene is not judged on reachability");
    }
    {
        // The spawn is nowhere near the authored grid, so the flood has no
        // start. Every cell would come back unreachable, which is a verdict
        // about the spawn, not about the level's topology.
        SceneDocument document = openRoom();
        document.find("spawn")->transform.position = {500.0f, 0.0f, 500.0f};
        const std::vector<Issue> issues = validate(document, catalog);
        requireCount(issues, "exit.unreachable", 0,
                     "an off-grid spawn is not reported as a sealed exit");
        requireCount(issues, "cell.unreachable", 0,
                     "nor as nine stranded cells");
    }

    std::cout << "SceneReachabilityTests OK\n";
    return 0;
}
