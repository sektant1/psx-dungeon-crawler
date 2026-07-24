# Engine Architecture — Audit & Refactor Roadmap

_Living document. Started during the 2026-07-24 architecture pass. Visual output
is frozen: no shader/compositor/material/preset change is permitted by any step
here — refactors are structural only._

## Diagnosis

The engine is clean at the unit level (Pimpl everywhere, Ogre fully hidden
behind `eng::Renderer`, no TODO/FIXME debt, ~19k LOC real source, broad tests).
The "Frankenstein" feeling came from **one structural fault line**: three
parallel entity/scene models, two of them live in different executables.

| Model | Location | Users | Status |
|---|---|---|---|
| Renderer scene graph (`NodeHandle`) | `eng::Renderer` | `game` (main.cpp, DungeonMap, LiveLevel) | live — shipped game |
| EnTT `ecs::Scene` (+ SceneSync/PhysicsSync/.map) | `eng/ecs/*`, `game/src/scene/*` | `level_editor`, `MapPlay`/`MapRuntime` | live — editor/map only |
| SPEngine OOP (`Object/Entity/GameObject/Space/GameSession/Factory`) | `eng/core/*`, `eng/systems/Factory` | **deleted 2026-07-24 (R0)** — was tests-only | removed |

**Decision (2026-07-24):** the EnTT `ecs::Scene` model is the single source of
truth. Renderer stays a pure view driven by `SceneSync`. Both game and editor
load `.map` into the same registry. C++ standard moved to **C++20**.

Kept from the SPEngine port (genuinely used): `Object`, `System`, `Content`,
`ResourceCache`, `Resource` (LevelResource/TextResource build on these).

## Subsystem notes

- **Runtime/loop** (`eng::Engine`) — owns SDL→Ogre ordering, frame clock,
  screenshot/bench hooks. Solid. `System` registry exists but `game` ignores it
  and hand-rolls a 1086-line `main.cpp` (god-main — R2 target).
- **Renderer** — strongest subsystem, Ogre never leaks. Weakness: 60-method
  mega-facade (R3: split into MeshRenderer/SceneGraph/MaterialSystem/PostFX/
  DebugDraw behind the same header, ABI + visuals frozen).
- **ECS** — correct but shallow, editor-only today. This is the model that wins.
- **Serialization** — `.map` binary (MapSerializer + ComponentRegistry +
  ByteStream) is good and extensible, but serializes the ECS model the shipped
  game can't yet load without MapPlay. Unifies once R1 lands.
- **Editor** — `EditorApp` (806 LOC) is a god class (viewport+gizmo+picking+
  outliner+inspector+palette+IO+dock). Has undo/command stack bones. R4 splits
  it into panels and makes it a first-class target consuming the engine.
- **Physics** — Jolt behind `eng::Physics`; PhysicsSync bridges ECS side, `main`
  drives it directly game side. Converges after R1.
- **Input/Config/Audio/Content/Log/FileSystem/Profiler** — small, consistent,
  keep as-is.

## Roadmap

- **R0 — Excise dead SPEngine OOP layer.** ✅ Done 2026-07-24. Removed 6 headers
  + 4 sources + FactoryTests; trimmed CoreObjectTests to Object-only; C++20 bump.
  34/34 tests pass, build clean.
- **R1 — Unify scene model.** Game gameplay actors move onto `ecs::Scene`
  (same components the editor uses); loop drives `SceneSync` each frame. The
  **batched dungeon shell (DungeonMap StaticGeometry) stays batched** — static
  level geometry is a separate path from per-entity actors (as in shipping
  engines). Bridge grows per slice.
  - **R1a ✅ done.** Added `game::GameScene` (Scene + RendererSceneBackend +
    SceneSync), pinned in `LiveLevel` via `unique_ptr`, synced each frame.
    Migrated the static lobby set-dressing (corner + brazier crate stacks, vault
    sword + shield) from direct `r.createNode/attachMesh` to
    `GameScene::spawnStatic`. Deleted the now-dead `place()` lambda. 34/34 tests.
  - R1b: grow bridge for per-frame light colour/range → migrate torch flicker,
    chest glow. R1c: sprites + particles components → labels, torch FX. R1d:
    world-transform composition in SceneSync → migrate hierarchical props
    (portals, market table, chest base→spin). R1e: PhysicsSync-driven actors →
    dynamic crates/barrels/dummy.

  **Capture oracle (built alongside R1b).** `PSX_SCREENSHOT` was originally
  useless for pixel-diff (`animTime` sums wall-clock `dt` → same-binary runs
  differed in 100% of pixels). Fixed by making capture deterministic:
  `Engine::tick()` returns a fixed `dt` (default 1/60, `PSX_FIXED_DT` overrides),
  `rand()` is pinned (`srand(1234)`) so Ogre ParticleFX emit identically, and
  Jolt runs single-threaded under capture. Run-to-run noise dropped 100% → ~0.2–1%
  (residual: Ogre advances particle time on its own wall-clock, cascading through
  bloom — not reachable without custom Ogre timer surgery). `docs/baselines/
  verify.py <ref> <new>` diffs two captures; behaviour-preserving refactors stay
  within the noise floor.
  **Known limits:** (1) not pixel-exact (particle floor); (2) under a tiling WM
  the window is resized by the compositor, so capture *resolution* varies across
  desktop layouts — baselines are only comparable within one layout. Proper fix:
  route `PSX_SCREENSHOT` through a fixed-size offscreen target (RenderCore already
  has offscreen RTT for the editor). Until then, refactor steps are verified by
  (a) behaviour-preserving-by-construction, (b) the test suite, (c) determinism
  (run-to-run within floor), (d) a gross-regression screenshot.
- **R2 — Dissolve god-main.** main.cpp was 1113 lines.
  - **R2a ✅** extracted the debug/diagnostics UI (`drawDungeonMap`, `ProfHud`,
    `drawDiagnostics`) → `game/src/GameDiagnostics.*`.
  - **R2b ✅** extracted `LiveLevel` + `buildLevel` (level construction/animation/
    transitions) → `game/src/LiveLevel.*`. main.cpp now 612 lines.
  - **R2c ✅ done** introduce `GameContext` (non-owning refs: Renderer/Physics/
    Input/assets) and turn inline loop gameplay into cohesive systems.
    Design note: the loop has a strict phase order (input→fixedstep→propsync→
    world→player→interaction→weapons→render) and that order *is* the determinism,
    so game systems are cohesive classes invoked at their exact phase (sharing
    GameContext) rather than a single coarse `eng::System::update(dt)` that would
    force reordering. The `eng::System` registry stays for order-independent
    engine systems.
    - `game::PropSystem` — lobby dynamic crates/barrels (spawn/sync/teardown).
    - `game::CombatSystem` — owns ProjectileSystem+SpellSystem+MeleeSystem+
      CombatConfig; consolidates their scattered call sites.
    - `game::PlayerSystem` — player controller + 3 viewmodels + weapon selection
      + loadout/respawn.
    - `game::InteractionSystem` — targeting + HUD prompts + torch toggle + portal
      descend/ascend (transitions fire via callbacks; level-stack stays in main).
    **main.cpp: 1113 → 440 lines.** The loop is now thin (input-grab, fixed-step,
    render sync, world/player update, interaction, attack input, collider debug,
    render). Remaining state in main = engine/physics bootstrap, debug-panel
    registration, `enterLevel` + level-stack, and the thin loop — a reasonable
    game bootstrap/orchestration surface. A future `GameApp` class could absorb
    `enterLevel`+loop to shrink `main` to ~10 lines, but that has diminishing
    returns versus R3/R4.
- **R3 — Split the `Renderer` facade** by concern behind the stable header.
- **R4 — Editor as its own decomposed target** (Viewport/Outliner/Inspector/
  Gizmo/AssetBrowser panels) editing the shared `.map`.
- **R5 — Data-driven content** (items/enemies/…) via components + TOML
  archetypes — trivial once there is one model.
  - **Combat model ✅ (first R5 slice).** `game/src/combat/`: EnTT components
    (Health/Resistances/FactionTag/StatusEffects/BodyLink), DamageType +
    CrowdControl enums, a **pure DamageSystem resolver** (percentage resistance,
    friendly-fire + i-frame gates, CC application; physics-free, returns
    knockback/kill), StatusEffectSystem (Burn DoT + movement/act/cast gates),
    WeaponDef + WeaponLibrary (weapons.toml over built-in defaults), and
    CombatDirector (owns the combat registry + body↔entity map, routes hits,
    applies Jolt knockback, fires death). Wired to the training dummy; a "Combat"
    debug panel shows live HP/resist/effects + test hits. Unit-tested
    (DamageSystemTests, StatusEffectTests). Design calls: % resist (uniform,
    clamped −1..0.9, True bypasses); one StatusEffects container (new effect
    kinds need no new component); weapon rolls crit so the resolver stays
    deterministic/testable. Next: enemy AI, player HP/death, floating combat
    text, more weapons + deliveries.

Every step must compile and keep the rendered image pixel-identical.

## Editor → Warcraft-III-style (in progress)

Direction (user, 2026-07-24): the level editor should work like the WC3 World
Editor — **terrain painting + doodad placement + playtest**, adapted to the 3D
FPS dungeon crawler on the existing ecs/.map pipeline. Domain vocabulary in
`CONTEXT.md` (terrain layer / doodad layer / EditorDocument / from-layout tag /
tool mode). Foundations already exist and are tested: `LevelDocument` (paintable
tile grid), `layoutToScene` (grid→entities), `Palette` (doodad browser),
`launchGame` (playtest). So WC3-style is mostly wiring, not a rebuild.

Locked design: terrain entities are tagged (`FromLayout`) and re-extruded on
paint (doodads, untagged, survive); the tile-paint tool is a new 3D ground brush
in the `level_editor` exe (retiring the old ASCII `LevelEditor.cpp` debug panel).

- **W1 ✅ done** — `game::editor::EditorDocument` (game/src/editor/): the deep
  two-layer module owning `LevelDocument` (terrain) + the doodad registry.
  `paintTile`/`replaceLayout`/`reExtrude` destroy+rebuild only `FromLayout`
  entities via `layoutToScene`; doodads untouched. Headless-tested
  (`editor_document_tests`): terrain extrudes, doodads survive every re-extrude,
  deterministic replacement, invalid mid-edit grids clear terrain. Drove out two
  domain contracts: the `C` anchor is required, and disconnecting spawn↔exit
  clears the terrain until the shell re-closes.
- W2: terrain tool mode + 3D ground brush (viewport ray→cell, brush palette,
  paint-on-drag) in `EditorApp`, driving `EditorDocument::paintTile`.
- W3: tool-mode tabs (Terrain/Doodad); fold the report's `GizmoTool` (Candidate
  A) in as the Doodad-mode tool.
- W4: playtest polish (spawn-at-cursor / test-from-here).
- Open (W2+): persist the terrain grid in the `.map` so a saved level stays
  re-paintable (today the grid lives only in the live `EditorDocument`).

Also fixed en route: `cmake/patches/ogre-cmake16-macrolog.patch` — captures the
CMake-16 `MACRO_LOG_FEATURE` quoting fix that a fresh OGRE clone needed (was a
hand-edit on the cached tree only; clean builds now apply it).
