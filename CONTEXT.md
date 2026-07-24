# Domain glossary

Shared names for the seams in this engine. Architecture reviews and design work
should use these terms exactly (see the `codebase-design` vocabulary for
*module / interface / depth / seam / adapter / leverage / locality*).

## Scene & content

- **entity / registry** — an EnTT entity and the `entt::registry` that owns it.
  The registry is the single source of truth for scene-object data
  (`eng::ecs::Scene`); the renderer is a view driven by **SceneSync**.
- **`.map`** — the binary level format (MapSerializer + ComponentRegistry +
  ByteStream). Round-trips the registry.
- **Layout** — `gen::Layout`: the dungeon as an ASCII tile grid (one char per
  cell: `#` solid, `.` floor, `A` arch, `L` torch, `S` spawn, `X` exit, …).
  Produced by `gen::generate` (BSP) or authored.
- **layoutToScene** — extrudes a **Layout** into registry entities (floor/wall/
  arch/torch/marker tiles). The bridge from the tile grid to the 3D world.

## Editor (WC3-World-Editor–adapted)

The level editor is a **two-layer** authoring tool, mirroring Warcraft III's
terrain-layer + doodad-layer split, adapted to this 3D FPS dungeon crawler.

- **terrain layer** — the paintable tile grid, owned by **`LevelDocument`**
  (`paint(col,row,tile)`, undo/redo, `validated()`, toml IO; renderer-free,
  tested). "Terrain" here = the dungeon shell (floors, walls, arches, torches,
  spawn/exit), not heightmap terrain.
- **doodad layer** — hand-placed entities (meshes, lights, spawn/exit/enemy/
  pickup/trigger markers) authored on top of the terrain with the gizmo, held in
  the editor's `entt::registry`.
- **EditorDocument** — the deep module that owns *both* layers: a
  **`LevelDocument`** (terrain) and the doodad **registry**. `paintTile`
  re-extrudes the affected terrain through **layoutToScene**; doodads are
  untouched. Both layers round-trip through one **`.map`**.
- **from-layout tag** — `FromLayout`, a component marking a registry entity as
  extruded terrain (vs a hand-placed doodad). Re-extrude destroys and rebuilds
  only `FromLayout` entities, so painting a tile never disturbs a doodad.
- **tool mode** — which manipulation the viewport's LMB performs: *Terrain*
  (paint the tile under the ground-plane cursor) or *Doodad* (place / gizmo an
  entity). WC3's terrain/doodad toolbar tabs.
- **GizmoTool** — (planned) the deep module for the Doodad-mode manipulation
  state machine (begin/hover/drag/release → Command), lifting the drag state
  machine out of `EditorApp`.
