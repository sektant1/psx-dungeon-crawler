#pragma once

#include "../LevelDocument.h"
#include "../scene/LayoutToScene.h"

#include <entt/entt.hpp>

#include <string>
#include <vector>

namespace editor {

// Marks a registry entity as extruded terrain (the dungeon shell), as opposed
// to a hand-placed doodad. Re-extrude rebuilds only FromLayout entities, so
// painting a tile never disturbs a doodad. Editor-only, transient metadata:
// terrain is re-extruded from the Layout, never serialized as tagged entities.
struct FromLayout {};

// The editor's two-layer level document (Warcraft-III-adapted): a paintable
// terrain layer (LevelDocument -- the gen::Layout tile grid) plus the doodad
// layer (the registry entities placed on top). Painting the terrain re-extrudes
// the affected tiles through layoutToScene into the same registry; doodads are
// untouched.
//
// Deep behaviour behind a small interface: callers paint a cell or replace the
// grid and the entity-level consequences (destroy stale terrain, re-extrude,
// re-tag, leave doodads alone) happen inside. Renderer-free -- callers sync the
// registry to a view separately -- so the whole terrain<->entity bridge is
// testable headless.
class EditorDocument {
public:
    EditorDocument(entt::registry& registry, game::SceneGenOptions opts)
        : mReg(registry), mOpts(std::move(opts)) {}

    LevelDocument& terrain() { return mTerrain; }
    const LevelDocument& terrain() const { return mTerrain; }

    // Paint one tile and re-extrude. No-op if the cell is out of range.
    void paintTile(int col, int row, char brush);

    // Replace the whole terrain grid (Generate / Open) and re-extrude.
    void replaceLayout(std::vector<std::string> rows);

    // Rebuild every FromLayout entity from the current grid; doodads survive.
    // Called by paintTile/replaceLayout; exposed for load paths.
    void reExtrude();

    bool isTerrain(entt::entity e) const { return mReg.all_of<FromLayout>(e); }

    // World size of one tile cell, so callers can map a ground-plane point to a
    // grid cell (cell (col,row) centres at world {col*cell, 0, row*cell}).
    float cellSize() const { return mOpts.cell; }

private:
    entt::registry& mReg;
    LevelDocument mTerrain;
    game::SceneGenOptions mOpts;
};

} // namespace editor
