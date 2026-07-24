// EditorDocument: the two-layer terrain<->doodad bridge. Headless (no renderer):
// paint/replace the terrain grid, assert terrain entities extrude and, crucially,
// that hand-placed doodads survive a re-extrude untouched.
#include "editor/EditorDocument.h"

#include <eng/ecs/Components.h>

#include <cstdio>
#include <cstdlib>

using namespace editor;

static int failures = 0;
static void require(bool c, const char* m) {
    if (!c) { std::fprintf(stderr, "FAIL: %s\n", m); ++failures; }
}

static int terrainCount(entt::registry& r) {
    int n = 0;
    for (auto e : r.view<FromLayout>()) { (void)e; ++n; }
    return n;
}

int main() {
    entt::registry reg;
    game::SceneGenOptions opts; // empty dirs: layoutToScene stays registry-only
    EditorDocument doc(reg, opts);

    // A hand-placed doodad (no FromLayout tag) authored before any terrain.
    entt::entity doodad = reg.create();
    reg.emplace<eng::ecs::Transform>(doodad);
    reg.emplace<eng::ecs::Name>(doodad, eng::ecs::Name{"barrel"});

    // Replace the terrain grid -> it extrudes into tagged entities.
    doc.replaceLayout({"#######", "#S.C.X#", "#######"});
    const int t0 = terrainCount(reg);
    require(t0 > 0, "replaceLayout extrudes terrain entities");
    require(reg.valid(doodad), "doodad survives replaceLayout");
    require(!doc.isTerrain(doodad), "doodad is not tagged as terrain");
    require(reg.all_of<eng::ecs::Name>(doodad) &&
                reg.get<eng::ecs::Name>(doodad).value == "barrel",
            "doodad components are intact after extrude");

    // Re-extrude replaces terrain, it does not accumulate: applying the same
    // grid again yields the same terrain count (deterministic extrude, old
    // entities destroyed first).
    doc.replaceLayout({"#######", "#S.C.X#", "#######"});
    require(terrainCount(reg) == t0,
            "re-extrude replaces terrain rather than accumulating it");
    require(reg.valid(doodad), "doodad survives a second replaceLayout");

    // A validity-preserving paint re-extrudes and leaves the doodad untouched.
    // 'H' is a chest prop marker on floor: walkable, non-unique, and leaves the
    // required S/C/X markers in place, so the shell stays valid.
    doc.paintTile(2, 1, 'H');
    require(reg.valid(doodad), "doodad survives paint re-extrude");
    require(!doc.isTerrain(doodad), "doodad still untagged after paint");
    require(reg.get<eng::ecs::Name>(doodad).value == "barrel",
            "doodad name preserved across re-extrude");
    require(terrainCount(reg) > 0, "terrain re-extrudes after a valid paint");

    // Painting a wall that disconnects spawn from exit makes the grid invalid;
    // the terrain layer clears until the shell closes again (mid-edit safety),
    // and the doodad still survives.
    doc.paintTile(2, 1, '#');
    require(terrainCount(reg) == 0, "invalid mid-edit grid clears terrain");
    require(reg.valid(doodad), "doodad survives even an invalid paint");
    doc.paintTile(2, 1, '.'); // reconnect
    require(terrainCount(reg) > 0, "terrain returns once the shell re-closes");

    // The terrain layer keeps its own undo independent of the doodad layer.
    require(doc.terrain().canUndo(), "terrain paint is undoable");

    if (failures == 0) std::printf("EditorDocumentTests OK\n");
    return failures ? 1 : 0;
}
