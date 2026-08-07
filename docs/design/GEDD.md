# Raven Engine Design Document (GEDD) {#doc-design-gedd}

_Raven Engine implementation reference for the untitled PSX dungeon crawler._
_Companion to the player-facing [GDD.md](GDD.md). Started 2026-07-24._

> **Visual freeze rule.** The PSX look (shaders / compositor / materials /
> presets) is **frozen**. No refactor or feature may change the rendered image.
> Structural work only. This is the engine's prime directive.

---

## 1. Purpose & philosophy

A **hand-built C++20 game engine** for a single-player PSX-style FPS dungeon
crawler. Not Unity/Unreal/Godot — a bespoke engine where **OGRE 14.x is only the
renderer**, and everything else (loop, input, physics, audio, ECS, content,
serialization) is first-party and owned.

**Core principles:**

1. **Third-party libraries never leak.** OGRE, SDL2, Jolt, EnTT, miniaudio all
   live behind the `eng::` public API (Pimpl everywhere). Game code includes
   `eng/*.h` and GLM — never an Ogre or SDL header.
2. **One scene model.** The EnTT `ecs::Scene` registry is the single source of
   truth for scene data; the renderer is a **pure view** driven by `SceneSync`.
3. **Determinism where it counts.** Fixed-step simulation, pinnable RNG,
   single-threaded physics under capture — so playtests and refactors are
   reproducible.
4. **Data-driven content.** Weapons, particles, dungeons, palettes, levels are
   TOML + components, not hardcoded.
5. **Everything builds from source, reproducibly.** All deps fetched & built via
   CPM; `make deps` bootstraps any major distro.

---

## 2. Technology stack

| Concern | Library | Wrapped by | Notes |
|---|---|---|---|
| Rendering | OGRE 14.x (GL3Plus) | `eng::Renderer` | Hand-written GLSL PSX shaders, **no RTSS**. Never leaks. |
| Window / loop / input | SDL2 | `eng::Engine`, `eng::Input` | Owns window + event pump. Forces `SDL_VIDEODRIVER=x11` on Wayland. |
| Physics | Jolt | `eng::Physics` | CharacterVirtual + rigid bodies. Built `-fno-rtti`. |
| ECS | EnTT | `eng::ecs::Scene` | Single source of truth for scene data. |
| Audio | miniaudio | `eng::Audio` | Ported from SPEngine (Sagel) audio layer. |
| Math | GLM | (public) | The one third-party type allowed in the public API. |
| Config / data | toml++ | `eng::Config`, `eng_toml` | All game data is TOML. |
| Debug UI | Dear ImGui (docking) | — | **Currently removed** from consumers (see §9). |
| Build | CMake ≥3.16 + CPM | — | C++20. LTO/IPO on first-party targets only. |

**Language standard:** C++20 (bumped from C++17 during the R0 refactor).

---

## 3. Repository layout

```
engine/include/eng/   Public API — the entire engine surface game code may touch
engine/src/           Ogre + SDL + Jolt implementation (Pimpl bodies)
  ├─ audio/           Audio, SoundInstance, SoundResource (miniaudio)
  ├─ content/         TextResource (Content/Resource/ResourceCache system)
  ├─ core/            Object (kept from SPEngine port)
  ├─ diagnostics/     Profiler, Trace
  ├─ ecs/             Scene, SceneSync, RendererSceneBackend
  ├─ io/              FileSystem, DirectoryWatcher (hot-reload)
  └─ systems/         Actions, Ease, Events
engine/assets/        PSX shader stack (GLSL, programs, materials, compositor)
game/
  ├─ src/             The game (main.cpp + systems + combat/ + scene/ + editor/)
  ├─ sim/             Headless simulation harness (game_sim)
  ├─ assets/          Game data (weapons/particles/dungeon/lobby/palettes .toml)
  └─ tests/           Unit + integration tests
samples/psx-demo/     Renderer regression sample (godot-psx-style port)
tools/                mapgen and other CLI tools
docs/design/          This GEDD + the GDD
docs/skills/          Role/skill definitions for AI-assisted dev
```

---

## 4. Public API surface (`engine/include/eng/`)

The complete contract game code depends on:

| Header(s) | Subsystem |
|---|---|
| `Engine.h` | Runtime: SDL→Ogre ordering, frame clock, fixed-step, screenshot/bench hooks |
| `Renderer.h`, `SceneView.h`, `Handles.h`, `LightDesc.h`, `Sprite.h` | Rendering facade + handle-based scene graph |
| `Physics.h` | Jolt: character controller, bodies, raycasts |
| `Input.h`, `Action.h`, `ActionSet.h`, `Actions.h` | Input + action-mapping |
| `ecs/Scene.h`, `ecs/Components.h`, `ecs/SceneSync.h`, `ecs/SceneBackend.h` | ECS scene model + renderer sync |
| `Audio.h`, `AudioTypes.h`, `SoundInstance.h`, `SoundResource.h` | Audio |
| `Content.h`, `Resource.h`, `ResourceCache.h`, `TextResource.h`, `TextResource`… | Content/resource system |
| `Config.h` | TOML config |
| `Math.h`, `Ease.h` | GLM re-export + easing |
| `Object.h`, `System.h` | Base object + system registry (kept from SPEngine) |
| `Log.h`, `Profiler.h`, `Trace.h` | Diagnostics |
| `FileSystem.h`, `DirectoryWatcher.h` | IO + hot-reload |
| `Events.h`, `ParticleEffectDesc.h`, `SoundResource.h` | Events, particle descs |

**Rule:** if game code needs something, it belongs here behind Pimpl — never a
direct third-party include.

---

## 5. Architecture — the layers

```
        ┌─────────────────────────────────────────────────┐
        │  GAME  (game/src)                                │
        │  main → GameContext → systems (Player/Combat/    │
        │  Interaction/Prop) · combat/ · scene/ · editor/  │
        └───────────────┬───────────────────┬─────────────┘
                        │ reads/writes       │ drives
                        ▼                    ▼
        ┌───────────────────────┐   ┌──────────────────────┐
        │  ecs::Scene (EnTT)     │   │  eng:: public API    │
        │  SINGLE SOURCE OF TRUTH│   │  Engine/Renderer/    │
        │  for scene-object data │   │  Physics/Input/Audio │
        └───────────┬───────────┘   └──────────┬───────────┘
                    │ SceneSync (view)          │ Pimpl
                    ▼                            ▼
        ┌───────────────────────────────────────────────────┐
        │  IMPL  OGRE 14 · SDL2 · Jolt · EnTT · miniaudio    │
        │  (never visible above the eng:: line)             │
        └───────────────────────────────────────────────────┘
```

### 5.1 The scene-model decision (critical)

The engine previously had **three parallel entity/scene models** — the
"Frankenstein" fault line. The unification decision (2026-07-24):

- **EnTT `ecs::Scene` is the single source of truth.** Scene-object data lives in
  the registry.
- **The renderer is a pure view.** `SceneSync` walks the registry and pushes
  transforms/visibility into the Ogre scene graph each frame. The renderer never
  owns gameplay state.
- **Static level geometry is a separate path.** The batched dungeon shell
  (`DungeonMap` → `StaticGeometry`) stays batched — static geometry and
  per-entity actors are different paths, as in shipping engines.
- **One authoring path.** JSON `.scn` is the planned canonical source and binary
  `.map` is its derived runtime container. The future editor edits `.scn` and
  invokes the same cooker as CI; it does not save runtime registries as source.

Deleted: the SPEngine OOP layer (`Object/Entity/GameObject/Space/GameSession/
Factory`). Kept from that port: `Object`, `System`, `Content`, `ResourceCache`,
`Resource`.

---

## 6. Engine subsystems

### 6.1 Runtime / loop (`eng::Engine`)
Owns the SDL→Ogre ordering, frame clock, and the **fixed-step** simulation. Hooks
for `RAVEN_SCREENSHOT` (deterministic capture) and `RAVEN_BENCH_FRAMES`. `tick()`
returns a fixed `dt` under capture (`RAVEN_FIXED_DT`), pins `rand()`, and runs Jolt
single-threaded so captures are reproducible.

### 6.2 Renderer (`eng::Renderer`)
Strongest subsystem; Ogre never leaks. Handle-based scene graph (`NodeHandle`,
mesh/light/sprite handles). PSX look = hand-written GLSL post chain (pixelation,
dithering, affine warp, outline) via a compositor. **Render presets** (ps1 / n64 /
modern-ps1) extracted to `RenderPresets`, applied at launch + tunable at runtime.
Known scale note: it is a ~60-method mega-facade slated to split into
MeshRenderer / SceneGraph / MaterialSystem / PostFX / DebugDraw behind the same
frozen header (roadmap R3).

### 6.3 ECS (`eng::ecs`)
`Scene` (EnTT registry wrapper), `Components`, `SceneSync` (registry→renderer
view), `RendererSceneBackend` (the Ogre-side sink). The winning scene model.

### 6.4 Physics (`eng::Physics`)
Jolt behind Pimpl. `CharacterVirtual` for the player; rigid bodies for props
(crates/barrels), projectiles, and the training dummy. `PhysicsSync` bridges the
ECS side; the game loop drives it at a fixed phase. Default restitution 0 so
arrows stick and props don't bounce.

### 6.5 Content / resources
`Content` + `Resource` + `ResourceCache` (from SPEngine port). `TextResource`,
`LevelResource` build on these. `DirectoryWatcher` enables hot-reload.

### 6.6 Audio (`eng::Audio`)
miniaudio backend, ported from the SPEngine (Sagel) audio layer. Sound resources
+ instances behind the `eng::` line.

### 6.7 Particles (`eng::Particles`)
Data-driven pool + `particles.toml` + `ParticleLibrary` (game-side). Uses Ogre
ParticleFX under capture-safe timing caveats.

### 6.8 Input, Config, Log, Profiler, IO
Small, consistent, stable. Action-mapping (`ActionSet`/`Actions`), TOML config,
logging, a frame profiler + trace, filesystem + directory watching.

### 6.9 System registry (`eng::System`)
Exists for **order-independent** engine systems. The *game* deliberately does not
route gameplay through it — see §7.

---

## 7. Game architecture (`game/src`)

### 7.1 The loop is the determinism
The game loop has a **strict phase order** and that order *is* the determinism:

```
input-grab → fixed-step (physics) → prop-sync → world → player →
interaction → weapons → collider-debug → render
```

Because reordering would change behavior, gameplay is expressed as **cohesive
systems invoked at their exact phase** (sharing a `GameContext`), **not** a
coarse `System::update(dt)` that would force reordering. `eng::System` stays for
order-independent engine systems only.

### 7.2 GameContext + systems
`GameContext` holds non-owning refs (Renderer / Physics / Input / assets). The
former 1113-line god-`main.cpp` was decomposed to ~440 lines behind these:

| System | Responsibility |
|---|---|
| `PlayerSystem` | Player controller, 3 viewmodels, weapon selection, loadout, respawn |
| `CombatSystem` | Owns ProjectileSystem + SpellSystem + MeleeSystem + CombatConfig |
| `InteractionSystem` | Targeting, HUD prompts, torch toggle, portal descend/ascend |
| `PropSystem` | Lobby dynamic crates/barrels (spawn/sync/teardown) |
| `LiveLevel` | Level construction, animation, transitions (`buildLevel`) |
| `GameScene` | Scene + RendererSceneBackend + SceneSync wiring |
| `GameDiagnostics` | Debug/diagnostics HUD (extracted from main) |

`main.cpp` now = engine/physics bootstrap + level-stack (`enterLevel`) + the thin
phased loop.

### 7.3 Combat model (`game/src/combat/`)
The first data-driven content slice. EnTT components: `Health`, `Resistances`,
`FactionTag`, `StatusEffects`, `BodyLink`. `DamageType` + `CrowdControl` enums.

- **`DamageSystem`** — a *pure* resolver: percentage resistance (clamped −1..0.9,
  `True` bypasses), friendly-fire + i-frame gates, CC application; physics-free,
  returns knockback/kill. Deterministic → unit-tested.
- **`StatusEffectSystem`** — Burn DoT + movement/act/cast gates; one
  `StatusEffects` container so new effect kinds need no new component.
- **`WeaponDef` + `WeaponLibrary`** — `weapons.toml` over built-in defaults;
  weapons roll crit so the resolver stays deterministic.
- **`CombatDirector`** — owns the combat registry + body↔entity map, routes hits,
  applies Jolt knockback, fires death events.

### 7.4 Level pipeline (dungeon, current)
```
gen::generate / LevelDocument  →  gen::Layout (ASCII adapter)
                                           │
                                           ▼
                              DungeonMap direct materialisation
                         (batched shell + physics) + selected ECS actors
```
`layoutToScene` currently belongs to the separate `mapgen`/`.map` path; it is
not yet the normal game's materialisation path. Converging both producers on a
shared scene IR is required before editor preview can claim runtime parity.
`RAVEN_GEN_SEED` / `RAVEN_GEN_DUMP` control generation. Static shell is batched;
actors are per-entity in the registry.

### 7.5 Serialization (`.map`)
Binary runtime format: `MapSerializer` + `ComponentRegistry` + `ByteStream`.
`mapgen` can cook a generated layout and `MapRuntime`/`MapPlay` can inspect it,
but this path is not integrated with the normal combat/game loop. No `.scn`
loader, stable author identity, prefab expansion, shared cooker or editor target
exists yet.

---

## 8. Determinism & verification

The engine can't rely on pixel diffs (`RAVEN_SCREENSHOT` is **not** a pixel oracle —
`animTime` summed wall-clock `dt` → 100% pixel diff on identical binaries). The
verification stack instead:

1. **Behaviour-preserving-by-construction** refactors.
2. **The test suite** (30+ unit/integration targets: scene, scene-sync,
   damage-system, status-effect, map-serializer, physics-sync, level-resource,
   render-palette, …).
3. **Determinism** — fixed-`dt` capture, pinned RNG, single-threaded Jolt: run-to-
   run noise dropped 100% → ~0.2–1% (residual = Ogre's own particle wall-clock).
4. **Gross-regression screenshot** via `docs/baselines/verify.py <ref> <new>`.

**Known capture limits:** not pixel-exact (particle floor); under a tiling WM the
compositor resizes the window so capture *resolution* varies across layouts —
baselines only compare within one layout. Proper fix: route `RAVEN_SCREENSHOT`
through a fixed-size offscreen RTT (RenderCore already has offscreen RTT).

### 8.1 Headless simulation harness (`game_sim`)
A windowless, scriptable seam driving the game: feed an `InputFrame` stream
(move / look / attack / interact / descend) into the fixed-step loop, read back
state (player pos/HP, combat results, transitions). Advances the *same*
Player/Combat/Interaction systems the live loop does. Makes playtests and
regressions runnable as tests, built on the deterministic-capture work.

---

## 9. Tooling & editor

### 9.1 Level editor — status
A 3D WYSIWYG level editor existed (viewport / TRS gizmo / docked panels / grid
snap / materials) with binary `.map` serialization and a full author→save→play
loop. **It was deleted 2026-07-24** (commit `4cfff04`) along with all ImGui
consumers — the ImGui overlay flicker under the Ogre vsync buffer-swap was
unfixable in that setup. **Kept:** `.map` playback, `mapgen`, `LevelDocument`,
`layoutToScene`, and the `game/src/scene/` runtime seams. `EditorDocument`,
`GizmoTool`, `Palette`, and the editor application were deleted.

### 9.2 Editor direction — Warcraft-III-style (planned)
The replacement is a separate engine-consumer application whose source
authority is JSON `.scn`:

- `SceneDocument`/IR owns stable author IDs, hierarchy, prefab instances and
  typed logical asset references.
- Reversible author commands modify only that IR; ImGuizmo never stores runtime
  handles in history.
- A preview bridge mirrors author IDs to transient ECS/render/physics handles.
- The editor and CLI invoke one deterministic `.scn` → `.map` cooker.
- `LevelDocument` and procedural generation remain importers/producers, not a
  second scene authority.

### 9.3 CLI tools
`mapgen` (generator → `.map`), `game_sim` (headless sim), `make` targets for
build/run/deps/docs.

---

## 10. Build system

- **CMake ≥3.16, C++20.** Every dep (OGRE, SDL2, GLM, Jolt, toml++, EnTT,
  miniaudio) fetched + built from source via CPM; `make deps` bootstraps distros.
- **LTO/IPO** on first-party targets only (never Jolt/OGRE).
- Optional **AddressSanitizer / UBSan** (`ENABLE_ASAN`). UBSan vptr/function
  checks disabled because Jolt is `-fno-rtti`. Audit result: our code is clean
  (0 corruptions/leaks); fixed 2 upstream OGRE bugs en route.
- **Vendored ImGui docking branch** must win over OgreOverlay's bundled copy.
- OGRE clean-build patches captured in `cmake/patches/` (e.g.
  `ogre-cmake16-macrolog.patch`).

### Make targets
```
make game     # FPS test room: WASD + mouse-look, Esc releases/quits
make demo     # PSX shader demo (godot-psx-style port)
make deps     # install/build all dependencies
make docs     # Doxygen API reference → build/docs/html/index.html
```

### Env hooks
- `RAVEN_SCREENSHOT=/path.png` — render N frames, save, exit (verification hook).
- `RAVEN_FIXED_DT`, `srand` pin, single-thread Jolt — deterministic capture.
- `RAVEN_GEN_SEED` / `RAVEN_GEN_DUMP` — dungeon generation control.
- `RAVEN_BENCH_FRAMES` — benchmark (note: `game.toml` `vsync=true` caps to 60fps;
  set `vsync=false` to measure real GPU cost).

---

## 11. Refactor roadmap (structural, image-frozen)

Living roadmap toward the one-model engine. Status as of 2026-07-24:

| Step | Goal | Status |
|---|---|---|
| **R0** | Excise dead SPEngine OOP layer; C++20 bump | ✅ done |
| **R1** | Unify scene model (actors → `ecs::Scene`, `SceneSync` each frame; static shell stays batched) | 🔶 R1a done, R1b–e in progress |
| **R2** | Dissolve god-`main` (GameContext + systems) | ✅ done (1113 → 440 lines) |
| **R3** | Split the `Renderer` facade behind the frozen header | ⬜ planned |
| **R4** | Editor as its own decomposed target (Viewport/Outliner/Inspector/Gizmo/AssetBrowser) | 🔶 WC3 W1–W3 done, W4 pending |
| **R5** | Data-driven content (items/enemies/…) via components + TOML archetypes | 🔶 combat model shipped (first slice) |

**Every step must compile and keep the rendered image pixel-identical.**

---

## 12. Current state vs. game vision

The **engine is well ahead of the game**. What the [GDD](GDD.md) needs, and what
the engine already provides:

| GDD need | Engine support today |
|---|---|
| Fast FPS combat (melee/cast/archery) | Damage model, spells, melee, projectiles, viewmodels — built |
| Procedural dungeons | BSP gen → layout → 3D world, seed-stack descend — built |
| Data-driven enemies/items | Component + TOML archetype pipeline — foundations built (R5) |
| Authored content in procedural shell | `.map` + editor seams + `layoutToScene` — mostly built |
| Village hub (living, upgradeable) | Only static set-dressing — **not started** |
| Quests / factions / reputation | **Not started** — no system yet |
| Risk/loss economy (Tarkov loop) | **Not started** — no inventory/stash/extract |
| Bosses (Souls-like) | Enemy AI + boss systems — **not started** |
| Headless playtest of game logic | `game_sim` harness — built |

The next game-layer work (per the GDD) is the meta-loop: **inventory/stash →
extract/loss → village hub → quests/factions**. The combat, dungeon, and content
foundations to hang them on are already in place.
