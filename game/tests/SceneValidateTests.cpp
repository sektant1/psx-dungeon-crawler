// One case per issue code, plus the quick fix that clears it. The codes are the
// contract the editor's Issues panel and the CLI both key off, so they are what
// this test asserts on rather than the human-readable message.

#include "SceneSource.h"
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
        std::cerr << "SceneValidateTests: " << message << '\n';
        std::exit(1);
    }
}

static bool has(const std::vector<Issue>& issues, const std::string& code)
{
    return std::any_of(issues.begin(), issues.end(),
                       [&](const Issue& i) { return i.code == code; });
}

static const Issue* get(const std::vector<Issue>& issues,
                        const std::string& code)
{
    for (const Issue& issue : issues)
        if (issue.code == code) return &issue;
    return nullptr;
}

// A scene that validates clean, to build the broken cases from.
static SceneDocument healthy()
{
    SceneDocument document;
    document.id = "scene.test";
    Entity spawn;
    spawn.id = "spawn";
    spawn.playerSpawn = true;
    document.add(spawn);
    Entity exit;
    exit.id = "exit";
    exit.exitYawDegrees = 0.0f;
    document.add(exit);
    return document;
}

int main()
{
    game::test::mountGameAssets();
    KitCatalog catalog;
    std::string error;
    require(KitCatalog::load(game::test::asset("kit.toml"), catalog, error), error);

    {
        const std::vector<Issue> issues = validate(healthy(), catalog);
        require(issues.empty(), "a healthy scene has no issues");
        require(!blocksCook(issues), "and cooks");
    }

    // --- document-level ----------------------------------------------------
    {
        SceneDocument document = healthy();
        document.remove("spawn");
        std::vector<Issue> issues = validate(document, catalog);
        require(has(issues, "spawn.missing"), "a missing spawn is reported");
        require(blocksCook(issues), "and blocks the cook");
        require(applyQuickFix(document, catalog, *get(issues, "spawn.missing")),
                "the quick fix adds one");
        issues = validate(document, catalog);
        require(!has(issues, "spawn.missing"), "which clears the issue");
    }
    {
        SceneDocument document = healthy();
        Entity second;
        second.id = "spawn2";
        second.playerSpawn = true;
        document.add(second);
        const std::vector<Issue> issues = validate(document, catalog);
        require(has(issues, "spawn.duplicate"), "two spawns are reported");
        require(blocksCook(issues), "and block the cook");
    }
    {
        SceneDocument document = healthy();
        document.remove("exit");
        const std::vector<Issue> issues = validate(document, catalog);
        require(has(issues, "exit.missing"), "a missing exit warns");
        require(!blocksCook(issues), "but does not block the cook");
    }

    // --- prefabs -----------------------------------------------------------
    {
        SceneDocument document = healthy();
        Entity ghost;
        ghost.id = "ghost";
        ghost.prefab = "kit.nope";
        document.add(ghost);
        const std::vector<Issue> issues = validate(document, catalog);
        require(has(issues, "prefab.unresolved"), "an unknown prefab errors");
        require(blocksCook(issues), "and blocks the cook");
    }

    // --- grid slots --------------------------------------------------------
    {
        SceneDocument document = healthy();
        for (int i = 0; i < 2; ++i) {
            Entity wall;
            wall.id = "wall_" + std::to_string(i);
            wall.prefab = "kit.wall";
            wall.cell = CellPlacement{0, 0, CellPlacement::Edge::North, 1, 0, 0.0f};
            wall.transform = placementToTransform(
                GridConfig::fromCatalog(catalog), catalog,
                *catalog.find("kit.wall"), *wall.cell);
            document.add(wall);
        }
        const std::vector<Issue> issues = validate(document, catalog);
        require(has(issues, "cell.overlap"),
                "two walls on the same edge are reported");
        require(has(issues, "cell.wall_orphan"),
                "and a wall with no floor under it warns");
    }
    {
        // A floor under the wall clears the orphan warning.
        SceneDocument document = healthy();
        Entity floor;
        floor.id = "floor";
        floor.prefab = "kit.floor";
        floor.cell = CellPlacement{0, 0, CellPlacement::Edge::None, 1, 0, 0.0f};
        floor.transform =
            placementToTransform(GridConfig::fromCatalog(catalog), catalog,
                                 *catalog.find("kit.floor"), *floor.cell);
        document.add(floor);
        Entity wall;
        wall.id = "wall";
        wall.prefab = "kit.wall";
        wall.cell = CellPlacement{0, 0, CellPlacement::Edge::North, 1, 0, 0.0f};
        wall.transform =
            placementToTransform(GridConfig::fromCatalog(catalog), catalog,
                                 *catalog.find("kit.wall"), *wall.cell);
        document.add(wall);
        const std::vector<Issue> issues = validate(document, catalog);
        require(!has(issues, "cell.wall_orphan"),
                "a wall on a floored cell is fine");
        require(!has(issues, "cell.overlap"), "and does not collide with it");
    }
    {
        // Drift: the transform was moved away from its authored cell.
        SceneDocument document = healthy();
        Entity floor;
        floor.id = "floor";
        floor.prefab = "kit.floor";
        floor.cell = CellPlacement{1, 1, CellPlacement::Edge::None, 1, 0, 0.0f};
        floor.transform.position = {99.0f, 0.0f, 99.0f};
        document.add(floor);
        std::vector<Issue> issues = validate(document, catalog);
        require(has(issues, "cell.transform_drift"), "drift is reported");
        require(applyQuickFix(document, catalog,
                              *get(issues, "cell.transform_drift")),
                "and snapping fixes it");
        issues = validate(document, catalog);
        require(!has(issues, "cell.transform_drift"), "which clears the issue");
    }

    // --- per-component ------------------------------------------------------
    {
        SceneDocument document = healthy();
        Entity light;
        light.id = "lamp";
        light.light = LightAuthor{LightAuthor::Type::Point, {1, 1, 1}, 0.0f, false};
        document.add(light);
        std::vector<Issue> issues = validate(document, catalog);
        require(has(issues, "light.no_range"), "a rangeless point light errors");
        require(applyQuickFix(document, catalog, *get(issues, "light.no_range")),
                "the quick fix sets a range");
        issues = validate(document, catalog);
        require(!has(issues, "light.no_range"), "which clears the issue");

        // A directional light needs no range.
        SceneDocument sun = healthy();
        Entity key;
        key.id = "sun";
        key.light =
            LightAuthor{LightAuthor::Type::Directional, {1, 1, 1}, 0.0f, true};
        sun.add(key);
        require(!has(validate(sun, catalog), "light.no_range"),
                "a directional light does not need a range");
    }
    {
        SceneDocument document = healthy();
        Entity box;
        box.id = "volume";
        box.collider = ColliderAuthor{{1.0f, 0.0f, 1.0f}, {}};
        document.add(box);
        std::vector<Issue> issues = validate(document, catalog);
        require(has(issues, "collider.degenerate"), "a flat collider errors");
        require(applyQuickFix(document, catalog,
                              *get(issues, "collider.degenerate")),
                "the quick fix repairs it");
        issues = validate(document, catalog);
        require(!has(issues, "collider.degenerate"), "which clears the issue");
        // ...and only the degenerate axis was touched.
        require(document.find("volume")->collider->halfExtents.x == 1.0f,
                "the healthy axes are left alone");
    }
    {
        SceneDocument document = healthy();
        Entity trigger;
        trigger.id = "trap";
        trigger.trigger = TriggerAuthor{{1, 1, 1}, ""};
        document.add(trigger);
        const std::vector<Issue> issues = validate(document, catalog);
        require(has(issues, "trigger.no_event"), "an eventless trigger errors");
    }
    {
        SceneDocument document = healthy();
        Entity marker;
        marker.id = "thing";
        marker.marker = "bossspawn"; // no group.name dot
        document.add(marker);
        require(has(validate(document, catalog), "marker.unknown"),
                "a marker off-convention warns");
        document.find("thing")->marker = "boss.spawn";
        require(!has(validate(document, catalog), "marker.unknown"),
                "and a conventional one does not");
    }
    {
        SceneDocument document = healthy();
        Entity broken;
        broken.id = "nan";
        broken.transform.position.x = std::nanf("");
        document.add(broken);
        std::vector<Issue> issues = validate(document, catalog);
        require(has(issues, "transform.non_finite"), "NaN is reported");
        require(applyQuickFix(document, catalog,
                              *get(issues, "transform.non_finite")),
                "and reset fixes it");
        require(!has(validate(document, catalog), "transform.non_finite"),
                "which clears the issue");

        SceneDocument zero = healthy();
        Entity flat;
        flat.id = "flat";
        flat.transform.scale = {1.0f, 0.0f, 1.0f};
        zero.add(flat);
        require(has(validate(zero, catalog), "scale.zero"),
                "a zero scale is reported");
    }

    // --- corner gaps --------------------------------------------------------
    // Two perpendicular walls meeting at a corner leave a hole the width of the
    // wall, because each sits entirely outside the boundary it faces. This is
    // the check that stops that shipping unnoticed.
    {
        const KitPiece* wall = catalog.find("kit.wall");
        require(wall != nullptr, "kit.wall resolves");
        SceneDocument document = healthy();
        // A west run along Z at x=-12.5, and a north run along X at z=-12.5:
        // they touch at exactly one point and leave a 1 m notch outside it.
        Entity west;
        west.id = "west";
        west.prefab = "kit.wall";
        west.transform.position = {-12.5f, 0.0f, -10.0f};
        west.transform.rotationDegrees.y = 90.0f;
        document.add(west);
        Entity north;
        north.id = "north";
        north.prefab = "kit.wall";
        north.transform.position = {-10.0f, 0.0f, -12.5f};
        north.transform.rotationDegrees.y = 180.0f;
        document.add(north);

        std::vector<Issue> issues = validate(document, catalog);
        const Issue* gap = get(issues, "cell.corner_gap");
        require(gap != nullptr, "a corner gap is reported");
        require(gap->severity == Severity::Warning,
                "it warns rather than blocking the cook: the hole is invisible "
                "from inside a sealed room");
        require(applyQuickFix(document, catalog, *gap),
                "the quick fix places something");
        issues = validate(document, catalog);
        require(!has(issues, "corner"), "which clears the gap");

        // Two parallel walls in a run must NOT be reported: they share a face,
        // which is a join, not a hole.
        SceneDocument run = healthy();
        for (int i = 0; i < 2; ++i) {
            Entity piece;
            piece.id = "run_" + std::to_string(i);
            piece.prefab = "kit.wall";
            piece.transform.position = {-12.5f, 0.0f, -10.0f + float(i) * 4.0f};
            piece.transform.rotationDegrees.y = 90.0f;
            run.add(piece);
        }
        require(!has(validate(run, catalog), "cell.corner_gap"),
                "a straight run of walls has no corners");
    }

    // --- the real shipped scene --------------------------------------------
    {
        SceneDocument shipped;
        require(loadSceneSource(game::test::asset("scenes/ritual_boss_showroom.scn"), shipped, error), error);
        const std::vector<Issue> issues = validate(shipped, catalog, game::test::gamePackDir());
        for (const Issue& issue : issues) {
            std::cerr << "  shipped scene: " << severityName(issue.severity)
                      << ' ' << issue.code << " (" << issue.entity
                      << "): " << issue.message << '\n';
        }
        require(!blocksCook(issues),
                "the scene we ship must not have blocking issues");
    }

    std::cout << "SceneValidateTests: ok\n";
    return 0;
}
