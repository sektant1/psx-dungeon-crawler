# Level Editor App — Design

**Date:** 2026-07-23
**Status:** Approved (design), pending implementation plan

## Goal

A standalone 3D level/map editor for the PSX dungeon crawler, in the mold of the
WarCraft 3 World Editor and Unity/Godot scene editors: author maps by placing and
transforming meshes, lights, gameplay markers, and trigger volumes directly in a
3D viewport with move/rotate/scale gizmos. Maps persist to a project-owned binary
file format (`.map`). The game loads `.map` files at runtime.

This replaces the flat 2D ASCII grid painter (`LevelEditor` / `LevelDocument`) and
the `DungeonMap` ASCII pipeline for hand-authored levels. The BSP generator
(`gen::DungeonGen`) is retained as a tool that emits an editable `.map` rather than
driving the runtime directly.

## Non-goals (YAGNI for v1)

Terrain/heightmap sculpting, cliff tiles, multi-map streaming, prefabs/nested
scenes, live in-editor script triggers, collaborative editing, binary asset baking
of meshes. Deferred until a concrete need appears.

## Chosen approach

**The editor mutates an `eng::ecs::Scene` registry directly.** The ECS registry is
the single source of truth for scene data. `eng::ecs::SceneSync` already reconciles
a registry into the renderer via `SceneBackend`, so the editor viewport is the
game's own renderer displaying the exact registry being edited. The same registry
serializes to `.map`; the runtime deserializes `.map` back into a registry and runs
the same `SceneSync` plus a new physics sync. One scene model, shared by editor and
game — they cannot diverge.

Rejected alternatives:
- **Separate editor `MapDocument` baked to ECS on load** — duplicates the scene
  model (document + registry), two representations to keep in lockstep, drift risk.
- **Brush/immediate-mode with no persistent model** — no undo, no inspector, does
  not match a WC3-style editor.

## Architecture

```
game/src/editor/                 the editor app (grows current editor_main.cpp)
  EditorApp            top loop, panel layout, viewport, play-launch
  EditorScene          owns eng::ecs::Scene + SceneSync; live render into RTT
  Selection            selected entity set + gizmo/pick state
  Picker               screen ray -> entity (CPU AABB raycast over world bounds)
  CommandStack         undo/redo (command objects over registry ops)
  commands/            Create/Delete/Transform/SetComponent/AddComponent/
                       RemoveComponent/Reparent
  panels/              Outliner, Inspector, AssetBrowser, Toolbar
  MapSerializer        registry <-> binary .map (read + write + text dump)
  ComponentRegistry    type table: name/id/factory/serialize/deserialize/inspect

engine/include/eng/ecs/          core components (already present)
game/src/scene/                  gameplay components + their sync (shared by both exes)
  GameComponents.h     Collider, PlayerSpawn, Exit, EnemySpawn, Pickup, Trigger
  PhysicsSync          Collider/Trigger components <-> eng::Physics bodies
  MapRuntime           load .map -> registry -> SceneSync + PhysicsSync (game exe)
```

The editor adds authoring (pick, gizmo, undo, panels). The game adds simulation
(physics stepping, gameplay systems). Both build the identical registry from the
same `MapSerializer` and share `game/src/scene/`.

## Components and the ComponentRegistry

### Component set

Core components (engine, already exist in `engine/include/eng/ecs/Components.h`):
`Name`, `Transform`, `Parent`, `Children`, `WorldTransform`, `Dirty`,
`MeshRenderer`, `LightRef`, `NodeRef`.

New gameplay components (game, `game/src/scene/GameComponents.h`):
- `Collider { Shape shape; glm::vec3 size; BodyLayer layer; }` — static collision.
- `PlayerSpawn {}` — unique player start.
- `Exit { float yawDegrees; }` — down-portal / level exit.
- `EnemySpawn { std::string type; }` — enemy placement.
- `Pickup { std::string type; }` — loot / item placement.
- `Trigger { Shape shape; glm::vec3 size; std::string event; }` — event volume.
- `BodyRef { eng::BodyHandle handle; }` — runtime-only, populated by PhysicsSync
  (never serialized).

`NodeRef`, `BodyRef`, `WorldTransform`, `Dirty` are runtime-derived and are **not**
serialized.

### ComponentRegistry (keystone)

A single table with one entry per serializable/editable component type:

```cpp
struct ComponentType {
    const char* name;
    uint16_t    stableTypeId;                 // persisted in .map, never reused
    void (*addDefault)(entt::registry&, entt::entity);
    bool (*has)(const entt::registry&, entt::entity);
    void (*remove)(entt::registry&, entt::entity);
    void (*serialize)(const entt::registry&, entt::entity, ByteWriter&);
    void (*deserialize)(entt::registry&, entt::entity, ByteReader&);
    bool (*drawInspector)(entt::registry&, entt::entity); // returns true on edit
};
```

The palette (add-component menu), the serializer, the inspector, and copy/paste all
iterate this one table. Adding a component type is a single registration and it
appears everywhere. Engine registers core components; the game registers gameplay
components at startup. A `stableTypeId` is assigned once and never reused across
versions, so old `.map` files keep resolving.

## Editor interaction

- **Viewport**: existing renderer RTT (`enableEditorViewport` /
  `editorViewportTextureId`) with a free-fly `EditorCamera` driven via
  `setEditorCameraPose`. Controls: RMB-look + WASD, MMB-pan, wheel-dolly, `F` frames
  the selection.
- **Picking**: LMB casts a screen ray (unproject through the viewport camera) and
  runs a CPU AABB intersection against each entity's world bounds. Meshes use
  `Renderer::nodeWorldBounds`; lights/markers/triggers use a fixed icon-sized box.
  Renderer-independent and works for non-mesh entities. `Ctrl` extends the
  selection; empty click clears it. (Physics `rayCast` is available but not used —
  not every entity has a body.)
- **Gizmo**: add **ImGuizmo** (`third_party/imguizmo/`, ImGui-native). Translate /
  rotate / scale in the viewport; `W`/`E`/`R` switch mode; `X`/`Y`/`Z` axis-lock;
  grid-snap toggle with a configurable step. Gizmo output is written back through a
  `SetTransformCommand` (coalesced into one command per drag).
- **Overlays** (via `Renderer::setDebugLines`): selection AABB, light range
  wire-sphere, trigger volume box, spawn/exit glyphs, ground grid.
- **Panels** (ImGui, docked):
  - *Outliner* — entity hierarchy, drag-to-reparent, rename, delete.
  - *Inspector* — per-component editors dispatched through the ComponentRegistry;
    add/remove component menu.
  - *Asset Browser* — scans `.obj` meshes from the kit dirs; thumbnails via the
    existing `MaterialPreview` RTT; click/drag to spawn a `MeshRenderer` entity.
  - *Toolbar* — new/open/save, gizmo mode, snap settings, Play.

## Commands and undo/redo

Every registry mutation is a command object with `apply()` / `revert()`:
`CreateEntity`, `DeleteEntity` (captures the full component set for restore),
`SetTransform`, `AddComponent<T>`, `RemoveComponent<T>`, `SetComponent<T>`,
`Reparent`. `CommandStack` holds undo/redo vectors; `Ctrl+Z` / `Ctrl+Y`. Gizmo
drags coalesce into a single command on mouse release. A save-point index drives the
window dirty flag.

## `.map` binary format

Versioned, chunked, forward-compatible:

```
Header:
  magic     "PSXMAP\0"  (7 bytes + NUL)
  version   u16
  flags     u16
String pool:
  count     u32
  entries   [ u32 length, bytes ]      (materials, names, mesh asset paths)
Entities:
  count     u32
  per entity:
    localId        u32
    parentLocalId  u32                  (0xFFFFFFFF = scene root)
    componentCount u16
    per component:
      stableTypeId u16
      byteLen      u32                  (lets readers skip unknown types)
      payload      bytes
```

- On load, `localId` values are remapped to freshly created `entt::entity` handles
  via a `localId -> entity` map, so parent links survive.
- `byteLen` per component lets a reader **skip components with an unknown
  `stableTypeId`**, giving forward compatibility.
- `version` is bumped on any layout-affecting change; a migration switch upgrades
  older files on load.
- `MeshRenderer` serializes the **mesh asset path** (string-pool index) plus the
  material name, not the session-local `MeshHandle`.
- Runtime-derived components (`NodeRef`, `BodyRef`, `WorldTransform`, `Dirty`) are
  never written.
- `MapSerializer` ships a `--dump` mode that prints a `.map` as text, recovering the
  inspect/diff affordance given up by going binary.

`ByteWriter` / `ByteReader` are little-endian primitive readers/writers, unit-tested
in isolation.

## Runtime load path (game exe)

`MapRuntime::load(path)`:
1. `MapSerializer::read(path)` → populate a registry.
2. `SceneSync::sync()` → create renderer nodes for `MeshRenderer` / `LightRef`.
3. `PhysicsSync::sync()` → create Jolt bodies for `Collider` / `Trigger`.
4. Gameplay systems read `PlayerSpawn` / `Exit` / `EnemySpawn` / `Pickup`.

This replaces the `DungeonMap` ASCII path for hand-authored levels and wires through
the existing `eng::Content` / `LevelResource` seam.

## PhysicsSync (new; used by both exes)

`SceneSync` only handles mesh + light. `PhysicsSync` reconciles physics:
- For each entity with `Collider` and no `BodyRef`, create a Jolt body via
  `eng::Physics::createBody` from shape/size/world-transform, storing the handle in
  a `BodyRef` component.
- `Trigger` entities create sensor bodies on `BodyLayer::Trigger`.
- Remove the body when the entity is destroyed.
The editor keeps physics **paused** (authoring); the runtime steps it.

## Generator as a tool

`gen::generate(seed)` builds a registry (floors, walls, props, torches as entities)
and `MapSerializer::write`s it to a `.map`. Exposed as a `mapgen` CLI and/or an
editor "Generate" action that fills the current scene. The BSP algorithm is
unchanged; it now emits an editable `.map` instead of driving the runtime.

## Third-party and build

- Add `third_party/imguizmo/` (`ImGuizmo.h`, `ImGuizmo.cpp`).
- New `game/src/editor/` sources compile into the `editor` target.
- New `game/src/scene/` sources (components, `PhysicsSync`, `MapSerializer`,
  `MapRuntime`) compile into **both** the `editor` and `game` targets.
- No other new dependencies. `tomlplusplus` and `json.hpp` are untouched by `.map`.

## Testing

Renderer-free unit tests (the repo already tests `LevelDocument`, `LevelResource`,
and ECS this way):
- `.map` round-trip: serialize → deserialize yields an equal registry.
- Unknown-component skip: a `.map` with an unknown `stableTypeId` still loads the
  rest.
- Command apply/revert restores the registry to its prior state for every command.
- Picker ray/AABB math (hits, misses, nearest-of-many).
- ComponentRegistry completeness: every registered type has all function pointers
  set and a unique `stableTypeId`.
- `ByteWriter`/`ByteReader` primitive round-trips.

Rendering, gizmo feel, and physics remain manual plus the existing screenshot
verification hooks (`PSX_SCREENSHOT`, `PSX_DEBUG_UI`).

## Implementation order (v1)

1. `ByteWriter`/`ByteReader` + `ComponentRegistry` + gameplay components +
   `MapSerializer` + round-trip / skip / completeness tests.
2. `EditorScene` (registry + `SceneSync` rendering into the RTT viewport) with
   free-fly `EditorCamera`.
3. `Picker` + `Selection` + AABB/overlay rendering.
4. ImGuizmo translate/rotate/scale + snapping + `SetTransformCommand`.
5. `CommandStack` + full command set + undo/redo + dirty tracking.
6. Outliner + Inspector (registry-driven) + Asset Browser.
7. Save/Open `.map` + `--dump`.
8. `MapRuntime` + `PhysicsSync` — the `game` exe loads and plays a `.map`.
9. Generator → `.map`.
10. Polish: shortcuts, gizmo feel, entity icons, empty states, status/validation.

## Risks and tradeoffs

- **Binary format loses git-diff/hand-edit.** Mitigated by `--dump` and the
  forward-compatible skip-unknown design; accepted per the encoding decision.
- **`stableTypeId` discipline.** IDs must never be reused; enforced by a test that
  checks uniqueness and by keeping the registry in one file.
- **ImGuizmo is a new dependency.** Small (two files), ImGui-native, widely used;
  low risk.
- **Two sync systems (`SceneSync` + `PhysicsSync`) over one registry.** Mirrors the
  existing renderer/registry split; keeps physics testable behind the `eng::Physics`
  seam.
