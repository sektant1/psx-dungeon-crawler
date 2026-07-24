#include "EditorDocument.h"

#include "../DungeonGen.h"

#include <eng/ecs/Components.h>

#include <unordered_set>

namespace editor {

void EditorDocument::paintTile(int col, int row, char brush)
{
    if (mTerrain.paint(col, row, brush))
        reExtrude();
}

void EditorDocument::replaceLayout(std::vector<std::string> rows)
{
    mTerrain.replace(std::move(rows));
    reExtrude();
}

void EditorDocument::reExtrude()
{
    // 1. Destroy the current terrain entities (doodads are untagged, so left).
    std::vector<entt::entity> stale;
    for (auto e : mReg.view<FromLayout>())
        stale.push_back(e);
    for (auto e : stale)
        mReg.destroy(e);

    // 2. Skip while the grid isn't yet a valid layout (mid-edit): the terrain
    //    just stays empty until it closes into a shell.
    gen::Layout layout = mTerrain.validated(/*requireExit=*/false);
    if (!layout.valid())
        return;

    // 3. Snapshot the survivors (doodads), extrude fresh terrain, and tag every
    //    entity that appeared. layoutToScene gives each tile a Transform, so the
    //    Transform view sees them all.
    std::unordered_set<entt::entity> before;
    for (auto e : mReg.view<eng::ecs::Transform>())
        before.insert(e);

    game::layoutToScene(layout, mOpts, mReg);

    for (auto e : mReg.view<eng::ecs::Transform>())
        if (before.find(e) == before.end())
            mReg.emplace<FromLayout>(e);
}

} // namespace editor
