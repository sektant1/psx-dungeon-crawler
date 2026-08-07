# CODEBASE_MIGRATION.md

Migrating `psx-dungeon-crawler` into `../raven-engine/`: a modular, independently
reusable engine with a Godot-like authoring workflow, carrying one FPS shooter as
its first game.

The order and method of this migration follow *Game Engine Architecture, 3rd ed.*,
Part I (Foundations) — `docs/game_engine_architecture_3e_markdown/part_01_foundations/`.
Gregory's Part I is not a rendering tutorial; it is the part of the book about
**how an engine is structured, built, and kept honest**, which is exactly the
problem here. Section references below (§1.6.5, §3.3, …) point at that tree.

---

## 0. The one-paragraph version

The engine inside this repository is better than its reputation: it has a real
layer lint, 131 registered tests, an asset conditioning pipeline, and a working
in-house Vulkan renderer. What it does not have is **modules you can lift out**.
Every public header lives in one include root, third-party types (EnTT, Jolt,
SDL, ImGui, toml++) are visible in that root, and the renderer, physics, audio,
particles, animation and UI are compiled into a *single* static library. So
"reuse the renderer" today means "link Jolt and miniaudio too." The migration is
therefore not a rewrite — it is a **decomposition**: same code, moved behind
per-module include roots and hard API boundaries, one module at a time, with the
tests moving with it.

---

## 1. What is actually here (audit, 2026-08-07)

Measured, not assumed. Every number below is reproducible with the command in
its footnote.

### 1.1 Size

| Tree | Files (`.cpp/.h/.hpp`) | Lines |
|---|---:|---:|
| `engine/` | 386 | 70,782 |
| `editor/` | 177 | 47,279 |
| `game/` | 239 | 42,932 |
| `samples/` | 8 | 2,275 |
| **Total** | **810** | **~161,000** |

5,257 tracked files, 1,120 of them assets. 131 `eng_add_test()` registrations.

This size matters for strategy: **161k lines is far past the point where a
big-bang rewrite succeeds.** The plan below never has a "stop the world" step.

### 1.2 The layers that already exist, and work

`ARCHITECTURE.md` describes eight link-enforced layers, and they are real:

```
eng_runtime → eng → eng_script → eng_framework → eng_systems → eng_platform → eng_core
                                                → eng_rhi ────┘
```

`tools/check_layering.py` classifies every source and public header into one of
`core, platform, systems, framework, app` and fails the `layering` ctest on an
upward include. Whole-archive linking makes an upward *link* impossible.

**This is an asset. The migration keeps it and sharpens it — it does not
replace it.** Gregory §1.6 opens on exactly this rule ("upper layers depend on
lower layers, but not vice versa… dependency cycles… inhibit code reuse"), and
this repo already got that part right.

### 1.3 A dead renderer is still haunting the tree

This is the most urgent finding, because everything else is planned against it.

OGRE **is gone as a dependency** — and the build files say so plainly.
`cmake/Dependencies.cmake:263`: "OGRE is gone. It was the fallback renderer…
The Vulkan RHI replaced it; the engine now talks to Vulkan through `eng_rhi`
and needs no renderer package at all." `cmake/BuildOptions.cmake:98`: "The
renderer is the Vulkan RHI… the RHI has since reached parity and OGRE is gone."
`RenderCore` owns an `rhi::Device` and every pass records into an
`rhi::CommandList`. Nothing fetches, builds or links OGRE.

But the *removal was never finished*. Left behind in `engine/`, `editor/`,
`game/` and the `Makefile`:

| Residue | Count |
|---|---:|
| Mentions of OGRE in comments and prose | 106 |
| Dead code behind `#if !defined(ENG_RENDERER_RHI)` (a macro that is always defined) | 11 blocks |
| Files still naming real `Ogre::` types | 4 |

The four are worse than comments. `engine/include/eng/particles/DecalSystem.h`
— a **public header** — forward-declares `namespace Ogre { class SceneManager; }`
and exposes `void attach(Ogre::SceneManager*)`; the shipping implementation in
`DecalSystemRhi.cpp` is an empty stub. `engine/src/render/MaterialPreview.cpp`
still `#include <OgreMaterial.h>`, compiling only because the include sits
behind a branch that is never taken. So the engine's public API advertises a
type from a library that is not in the build.

And the documentation was never updated at all:

| Document | Claims |
|---|---|
| `engine/src/rhi/README.md` | "`RenderCore`/`Renderer` still drive OGRE directly, which is what draws the game today" |
| `ARCHITECTURE.md` §Content root | "Ogre's lookup is flat", "Ogre resolves both material names and file basenames flatly" |
| `CMakeLists.txt` (dependency comment) | "Every dependency (OGRE, SDL2, glm, Jolt, toml++)" |
| `CLAUDE.md` | An entire build/debug section: "OGRE is compiled from source. A full rebuild is many minutes", "a half-linked `libOgreMain.so`", "how first-person viewmodels should integrate with OGRE" |

`CLAUDE.md`'s stated stack ("Vulkan 1.3 + RHI, EnTT, Jolt, ImGui, TOML") is
correct; its build/debug prose is a fossil of the OGRE era. **Anyone — human or
agent — who reads the docs before the code will plan against a renderer that
isn't there.** Fixing this is Phase 0, before a single file moves.

*(Status: the documentation half of this is **done** — see §9. The 11 dead code
blocks and the 4 `Ogre::` files are Phase 0's remaining work, deliberately left
as code changes rather than folded into a docs commit.)*

The actual runtime stack: SDL2 (window/input/events), Vulkan 1.3, EnTT, Jolt,
miniaudio, ozz-animation, assimp, toml++, ImGui + ImGuizmo, stb.

### 1.4 Why nothing can be reused today

Four concrete blockers, in descending severity.

**(a) `eng_systems` is a monolith.** One static library, 35 translation units:

```
renderer (RenderCore, Renderer, MaterialLibrary, BitmapFont, Image, LabelRaster)
+ physics (Jolt)      + audio (miniaudio)   + particles (5 TUs)
+ animation (ozz)     + UI (7 TUs)          + model import (assimp)
```

linking `SDL2::SDL2 Jolt miniaudio ozz_animation eng_model_import eng_stb`.
There is no build configuration in which you take the particle system without
also taking Jolt and an audio backend. This single target is the primary reason
"reuse a module" is currently impossible.

**(b) One include root, no module surfaces.** All 158 public headers sit under
`engine/include/eng/` — 56 top-level entries mixing `Renderer.h`, `Physics.h`,
`Audio.h`, `StringId.h`, `Config.h`. A consumer cannot express "I depend on the
math module" because there is no such thing to depend on. `ARCHITECTURE.md`
acknowledges this explicitly ("Splitting the include root per layer is
deliberately *not* done yet"). The lint compensates, but a lint is not an API.

**(c) Third-party types are in the public API.** Counted across
`engine/include/`:

| Library | Public headers exposing it |
|---|---:|
| glm | 60 |
| toml++ | 20 |
| EnTT | 16 |
| ImGui | 10 |
| Jolt | 6 |
| SDL | 5 |
| Vulkan | 2 |

Every one of these is a permanent dependency for any consumer of that header.
glm at 60 is a deliberate, defensible choice (see §4.3 — it stays, promoted into
`raven.math`). EnTT, Jolt, SDL, ImGui and toml++ in the public surface are not:
they make the ECS, physics, platform, debug-UI and config modules unusable
without adopting those exact libraries at those exact versions.

**(d) God objects.** `editor/src/app/EditorApp.cpp` is **10,518 lines** — 22% of
the editor in one file. `engine/src/render/rhi/Renderer.cpp` is 4,188 lines
behind a 467-line header declaring ~147 public members. `game/src/main.cpp` is
1,970. These are not style complaints: a 147-member facade cannot be
re-implemented, mocked, or partially adopted, which is what "reusable API" means.

### 1.5 Game concepts that leaked into the engine

The engine ships headers that only this game could want:

- `eng/render/Enchantment.h` — magic-school colour palettes, tint/hue-drift
  shader parameters. The header's own comment admits it: "Which schools of magic
  exist… is a game decision."
- `eng/ui/TargetBanner.h`, `eng/ui/Tooltip.h`, `eng/ui/LoadingScreen.h` — a
  specific game's HUD furniture, in the engine's UI module.

Grep for game vocabulary (`Enchant|Dungeon|Rpg|Loot|Boss|Mana|Spell`) hits 17
files under `engine/`, including `Renderer.h`, `Model.h` and `Primitive.h`.
Gregory §1.6.16 draws this line precisely: game-specific subsystems sit *above*
the engine. These move to the game.

### 1.6 Two things named "rhi"

- `engine/src/rhi/` — the device abstraction (`null/`, `gl/`, `vulkan/`),
  contract-tested, `gl/` still a skeleton.
- `engine/src/render/rhi/` — the actual scene renderer that draws the game.

Same word, different meanings, adjacent trees. Renamed in the target (§4.2).

### 1.7 Gaps against Part I

| Part I chapter | State here |
|---|---|
| Ch. 2 — Tools of the Trade | Strong. `make doctor`, ccache discipline, ninja dep-log repair, RenderDoc/gdb/valgrind/perf targets, deterministic screenshot capture. **Best-developed area in the repo.** |
| Ch. 3 — Software engineering fundamentals | Mixed. Layering enforced; error handling ad hoc; no allocator strategy (§3.3 data/code/memory layout is unaddressed — everything is `new`/`std::` default). |
| Ch. 4 — Parallelism | **Absent.** `std::thread` appears in exactly 3 files (Physics, ACP pipeline, Telemetry). No job system, no task graph, no frame-level parallelism. |
| Ch. 5 — 3D math | **Absent as a module.** `eng/Math.h` is 15 lines containing one helper function. glm *is* the math layer, used directly by 60 public headers. |

Chapters 4 and 5 are where the target engine gains something it does not have,
rather than merely reorganising what it does.

---

## 2. What the migration must not break

Three properties are shipped results. The plan is built around preserving them.

1. **The rendered image is frozen.** `CLAUDE.md`: "The PSX look (shaders,
   compositor, materials, presets) is a shipped result. No refactor may change
   the rendered image; `make visual-test` is how that is proven." Every phase
   below that touches rendering ends with a golden-image comparison.

   ⚠ **`make visual-test` does not currently work, and this blocks Phase 5.**
   The harness runs the game under `xvfb-run` with `LIBGL_ALWAYS_SOFTWARE=1`
   and a GLX-extended X server — a setup built for the OGRE/GL renderer. Xvfb
   offers no DRI3, so the Vulkan backend finds "No queue capable of present
   operations", fails device selection and segfaults (exit 139). Runs archived
   under `artifacts/visual/` show the identical failure on 2026-08-04, so this
   predates the migration. **Repairing it is a Phase 0 task**, not a Phase 5
   discovery: the plan's single most important safety net is currently absent.

   What does work, and what was used to verify Phase 0, is capturing against
   the real display, where there is a working present queue:

   ```sh
   RAVEN_SCREENSHOT=/tmp/x.png RAVEN_SCREENSHOT_FRAME=120 ./build/game
   ```

   The fix is likely to be either a headless Vulkan path (lavapipe /
   `VK_ICD_FILENAMES` pointing at a software ICD) or dropping Xvfb in favour of
   an offscreen swapchain. Until one exists, "the image is unchanged" is an
   eyeball claim rather than a test.
2. **`raven_player` links nothing under `game/`** — enforced by the
   `player_purity` ctest reading the built binary's symbol table. This is
   already the "engine is reusable" invariant in miniature; the migration
   generalises it to every module.
3. **The Godot-like project track.** `project.toml`, scene boot, instancing,
   declared components, export — `eng_runtime` + `raven_player` + the editor.
   This is the authoring workflow the user wants, and it is the *destination*,
   not something to be preserved incidentally.

**Baseline before starting:** `fps_controller` and `scene_template` are known to
fail at HEAD. Record the full `ctest` result as the baseline so a red run during
migration is attributable.

---

## 3. Strategy: strangler decomposition, never a rewrite

Gregory's layering rule (§1.6) is the target; the method to get there is
incremental extraction:

> Stand up `../raven-engine/` as the new home. Move **one module at a time**,
> lowest layer first. Each move gives the module its own include root, its own
> CMake target with an export set, its own tests, and a public API with no
> third-party types in it. The old tree keeps building against the moved module
> until the last one lands.

Why lowest-first: a module can only move once everything it depends on has
moved. `raven.core` and `raven.math` have no engine dependencies, so they go
first, and every later module lands on solid ground.

Why not a fresh rewrite: 161k lines, 131 tests, a working renderer, and a frozen
image. A rewrite discards the test suite — the only evidence the behaviour is
preserved — on day one.

**Repository shape.** `../raven-engine/` becomes the engine; the dungeon crawler
becomes a game *inside* it (`games/psx-dungeon-crawler/`), which is what proves
the engine is reusable — the same relationship `raven_player` already has.

---

## 4. The target architecture

### 4.1 Module stack, mapped to Gregory §1.6

Each row is one module: one directory, one CMake target, one include root, one
public umbrella header, its own tests. Dependencies point **down only**.

| Gregory §1.6 layer | Module | Depends on | Third-party (private) |
|---|---|---|---|
| §1.6.16 Game-specific | `games/psx-dungeon-crawler` | everything | — |
| §1.7 Tools | `raven.editor`, `raven.acp` | scene, assets, render | ImGui, ImGuizmo |
| §1.6.15 Gameplay foundations | `raven.scene` (ECS/world/prefabs) | core, math, assets | EnTT |
| | `raven.script` | scene | Lua/sol2 |
| | `raven.ui` | render, input | — |
| | `raven.runtime` (project/scene boot) | scene, script | toml++ |
| §1.6.13 Audio | `raven.audio` | core, math | miniaudio |
| §1.6.12 HID | `raven.input` | core, platform | SDL2 |
| §1.6.11 Animation | `raven.anim` | core, math | ozz |
| §1.6.10 Physics | `raven.physics` | core, math | Jolt |
| §1.6.9 Profiling/debug | `raven.diag` | core | — |
| §1.6.8 Rendering | `raven.render` (scene renderer) | rhi, assets, math | — |
| | `raven.gpu` (device abstraction) | core, platform | Vulkan, VMA |
| §1.6.7 Resource manager | `raven.assets` | core, io | assimp, stb |
| §1.6.6 Core systems | `raven.core` | math | toml++ |
| (Ch. 5) Math | `raven.math` | — | glm |
| §1.6.5 Platform independence | `raven.platform` | — | SDL2 |

Seventeen modules, versus today's one-include-root-and-a-monolith. The split of
`eng_systems` into `render / physics / audio / anim / ui / particles` is the
single highest-value change in the entire migration.

### 4.2 Renames that remove real confusion

| Today | Target | Why |
|---|---|---|
| `engine/src/rhi/` | `modules/gpu/` | It is a GPU device abstraction. Frees the word. |
| `engine/src/render/rhi/` | `modules/render/` | It is the scene renderer, not an RHI. |
| `eng::` | `rvn::` | Matches the repo/product name; a mechanical rename done once, by script, at the end. |

### 4.3 The module contract (this is the reuse requirement)

A module is "done" when all eight hold. This is the checklist every phase's exit
criteria refer to.

1. **Own include root.** `modules/<name>/include/raven/<name>/`. A consumer adds
   one include directory and gets exactly that module's API.
2. **One umbrella header.** `raven/<name>/<Name>.h` — the supported surface.
   Anything not reachable from it is internal.
3. **No third-party types in public headers.** Opaque handles, pimpl, or plain
   structs. `Jolt`, `EnTT`, `SDL`, `ImGui`, `Vulkan`, `toml++` become private
   implementation details. *(Exception: `raven.math` — see below.)*
4. **Own CMake target with an export set**, installable and consumable via
   `find_package(raven.<name>)` from outside this repository. This is the
   difference between "a folder" and "a reusable module."
5. **Explicit, acyclic dependencies**, declared in the target and checked by the
   layering lint (extended to understand modules, not just five layer names).
6. **Own tests**, buildable and runnable without the rest of the engine.
7. **No game vocabulary.** `Enchantment`, `TargetBanner`, dungeon, loot, mana:
   these live in `games/`.
8. **A README** stating what the module owns, its dependencies, and its
   extension points.

**On glm (rule 3's exception).** glm is in 60 public headers, it is header-only,
stable, and a de-facto lingua franca — replacing it would touch nearly every
file for little gain. Decision: **glm stays, but only through `raven.math`.**
`raven.math` re-exports `vec3/mat4/quat` under `rvn::` aliases and owns the
engine's math vocabulary (transforms, frustums, AABBs, random — Ch. 5's §5.6 and
§5.7). Other modules depend on `raven.math`, never on glm directly. If glm is
ever swapped, one module changes. This is the cheapest defensible reading of
Ch. 5, and it turns a 60-header liability into a single seam.

---

## 5. Migration order, following Part I

Gregory's Part I runs: **tools (Ch. 2) → engineering fundamentals (Ch. 3) →
architecture (§1.6) → math (Ch. 5) → parallelism (Ch. 4)**. That is a sensible
build order for this migration too, for a non-obvious reason: you cannot safely
move 161k lines without the tooling that proves you did not break anything, so
Ch. 2 genuinely comes first.

### Phase 0 — Truth and baseline *(no code moves)*

Ch. 2 §2.1. You cannot migrate against documentation that describes a different
engine.

- **[done]** Delete every OGRE claim in prose: `engine/src/rhi/README.md`,
  `ARCHITECTURE.md` (§Content root, ×2), `CMakeLists.txt` dependency comment,
  `CLAUDE.md` (build/debug + two viewmodel questions), `tools/assetlint.py`
  (×4 rule rationales). Each rule was *restated against what enforces it now*,
  not deleted — e.g. duplicate material names are still an error, but because
  `MaterialLibrary::insert_or_assign` silently overwrites, which is worse than
  the throw the comment used to describe.
- Remove the dead renderer: 11 `#if !defined(ENG_RENDERER_RHI)` blocks, the
  `Ogre::` includes in `MaterialPreview.cpp`, and `DecalSystem::attach`'s
  `Ogre::SceneManager*` parameter (public API + empty stub + its test).
- Retire the `ENG_RENDERER_RHI` macro itself once its last branch is gone: a
  macro that is always defined is a permanent invitation to write a second,
  untested code path.
- Fix the remaining ~90 stale comments, or delete them where they describe a
  trade-off no longer being made.
- **Repair `make visual-test`** (see §2). Nothing in Phase 5 is safe without it.
- Record the baseline: full `ctest` output, golden images, binary sizes,
  clean-build wall time. Commit it as `docs/baseline-2026-08-07.md`.
- Tag the pre-migration commit.

**Exit:** no present-tense OGRE claim remains in the tree; no
`#if !defined(ENG_RENDERER_RHI)` remains; `make visual-test` produces a
comparable image; baseline committed.

### Phase 1 — The new repository and its build spine

Ch. 2 §2.2. Stand up `../raven-engine/` with the module skeleton, the CMake
module template (target + export set + tests + README), and CI that builds and
tests an empty module. Port the genuinely good tooling verbatim: `make doctor`,
the ninja dep-log repair, the ccache single-configuration rule, the deterministic
screenshot hook.

**Exit:** `cmake -S . -B build && ctest` passes on a repo with zero engine code;
a module can be added in one file.

### Phase 2 — `raven.math`, then `raven.core`

Ch. 5, then §1.6.6. The two modules with no engine dependencies.

`raven.math`: glm aliases under `rvn::`, transform/AABB/frustum/ray types,
rotation-representation helpers (§5.4–5.5), and an explicit RNG (§5.7 — today
the engine has no seedable RNG story, which is a determinism hazard for a
shooter).

`raven.core`: `Log`, `StringId`, `Clock`/`StepClock`, `Config`, `FileSystem`,
`Handles`, `Reflect`, `Profiler`, events. Removes toml++ from the public surface
(config returns typed values, not `toml::node`).

**Exit:** both consumable standalone; the old tree builds against them.

### Phase 3 — `raven.platform` + `raven.input`

§1.6.5, §1.6.12. SDL disappears from the public API behind a window/event/input
abstraction. This is the module most likely to be reused first in an unrelated
project, so it is worth doing carefully.

### Phase 4 — `raven.assets` and the ACP

§1.6.7 + §1.7. The asset conditioning pipeline is already good work (Gregory's
fig. 1.33 rows, a resource database, `pack.manifest`, logical-path resolution
through mounted packs). It moves largely intact — mostly a relocation and an
include-root split.

### Phase 5 — Breaking `eng_systems` apart *(the big one)*

§1.6.8, .10, .11, .13. The monolith becomes six modules: `gpu`, `render`,
`physics`, `audio`, `anim`, `particles`, `ui`. Each drops its third-party
dependency out of its public headers.

Order within the phase: `gpu` → `render` → `physics` → `audio` → `anim` →
`particles` → `ui`, each landing green before the next starts.

**Do not attempt to also refactor `Renderer`'s 147-member facade here.** Move it
whole, then split it in Phase 5b against passing golden-image tests. One risk at
a time.

**Exit after every step:** `make visual-test` byte-identical. This phase is where
the frozen image is actually at risk.

### Phase 6 — `raven.scene` (gameplay foundations)

§1.6.15. The ECS world, components, reconcilers, hierarchy, prefabs/instancing.
EnTT leaves the public API — the biggest single-library decoupling in the plan,
and the one that makes the ECS reusable.

### Phase 7 — `raven.runtime`, `raven.script`, `raven.editor`

The Godot-like workflow: `project.toml`, scene boot, declared components,
instancing, export, and the editor on top. `raven_player`'s purity test
generalises here into a per-module purity check.

`EditorApp.cpp`'s 10,518 lines get split along panel boundaries during the move —
the editor is the one place where the God object must be broken *as part of*
the migration, because it is otherwise unmovable.

### Phase 8 — The game moves in

§1.6.16. `games/psx-dungeon-crawler/` — including `Enchantment`, `TargetBanner`,
`Tooltip`, `LoadingScreen` and every other game concept currently in `engine/`.

**Exit:** the game builds against installed engine modules only. At this point
the engine is provably reusable, because its only consumer consumes it the way a
stranger would.

### Phase 9 — Parallelism

Ch. 4. Now, not earlier: a job system imposed on tangled modules propagates the
tangle. With clean module boundaries, frame-level parallelism (animation
sampling, particle simulation, culling) becomes tractable. Start with a job
system (§4.5–4.6), and follow §4.8's rules of thumb rather than reaching for
lock-free (§4.9).

### Phase 10 — The FPS shooter slice

The gameplay goal from `CLAUDE.md` — movement, weapons, projectiles, viewmodel —
rebuilt on the clean stack as `games/`'s first real content, and as the proof
that adding a game requires no engine edits.

---

## 6. Verification

Nothing in this plan is trusted because it compiles.

| Property | Check | When |
|---|---|---|
| Behaviour preserved | the 131 ctests, moving with their modules | every phase |
| Image unchanged | `make visual-test` golden images — **currently broken, see §2**; until repaired, `RAVEN_SCREENSHOT` against the real display | every render-touching step |
| No upward dependency | layering lint, extended to modules | every phase |
| Module is standalone | build + test the module with the rest of the engine absent | module exit |
| No third-party leak | `tools/check_modules.py` in the new tree (only `raven.math` may match glm) | module exit |
| Engine has no game | grep engine modules for game vocabulary | Phase 8 |
| Consumable externally | a scratch CMake project doing `find_package(raven.render)` | Phase 8 |

Build discipline carries over from `CLAUDE.md` and is not optional here: never
clean-build, build single targets, run long builds in the background, and treat
"internal compiler error: Bus error" as memory pressure (build `-j1`), not a
corrupt tree.

---

## 7. Risks

| Risk | Severity | Mitigation |
|---|---|---|
| Image regression during Phase 5 | High | Golden images after every step; move `Renderer` whole before splitting it |
| Migration stalls half-done, two trees to maintain | High | Lowest-layer-first ordering means the old tree always builds against moved modules; no phase leaves both trees authoritative for the same code |
| Hidden coupling surfaces mid-phase | Medium | The layering lint already maps the tree; run it *before* each move to predict the breakage |
| EnTT removal from the ECS API is larger than estimated | Medium | Phase 6 is deliberately alone; if it overruns, ship it as `raven.scene` with EnTT still public and revisit — every other module is unaffected |
| `EditorApp.cpp` resists splitting | Medium | Split by panel, one panel per commit, against the editor's own tests |
| Doc drift returns | Low | Phase 0's rule: a document naming a technology the tree lacks is a failing check, not a stale comment |

---

## 8. What this buys

When the migration lands:

- `raven.render`, `raven.physics`, `raven.audio`, `raven.scene` and the rest can
  be dropped into an unrelated project individually, via `find_package`.
- Swapping Jolt, miniaudio, SDL or even glm touches exactly one module.
- A new game is a directory under `games/` and zero engine edits.
- The Godot-like workflow (project → scenes → instancing → export → play) sits
  on modules rather than on a monolith.
- Part I's four structural concerns — layering (§1.6), tooling (Ch. 2),
  engineering discipline (Ch. 3), math and parallelism (Ch. 5, Ch. 4) — each
  have a named owner in the tree.

---

## 9. Progress

### Done

**Phase 0 (mostly)** — branch `chore/phase0-retire-ogre`.
The OGRE residue is gone: 11 dead `#if !defined(ENG_RENDERER_RHI)` branches,
the 4 files naming `Ogre::` types (including `attach(Ogre::SceneManager*)` in a
public header), ~100 stale comments, and the `ENG_RENDERER_RHI` macro itself.
Rules were *restated against what enforces them now* rather than deleted.
Removing the dead branches exposed a native-window-handle chain that was dead
end to end (`Platform → Engine → RenderCore → rhi::DeviceDesc`, read by no
backend) and the X11 driver pin that existed only for OGRE — so the engine now
runs on native Wayland. Two real defects surfaced and were fixed: the editor
advertising an unloadable `.dds` import, and a `srand` comment claiming a
determinism guarantee nothing depended on any more.
Build 398/398; ctest 164/166 with the two pre-existing failures unchanged;
frame captured and inspected.

**Phase 1** — `../raven-engine/`, commit `fd1607b`.
The module spine: `raven_module()` (own include root, own export set, own
tests) and `tools/check_modules.py`, the `modules` ctest enforcing the two
contract rules CMake cannot — no third-party type in a public header, no
undeclared cross-module include. Both negative-tested.

**Phase 2 (first half)** — `raven.math`.
Transforms, AABBs, and a seedable RNG; the conventions the old tree carried as
folklore (forward is -Z, the empty AABB is `+inf/-inf`) stated in code. glm is
confined here, which is what turns a 60-header dependency into a one-module
seam. 4/4 tests pass.

### Next

1. **Repair `make visual-test`** (§2). It is the migration's safety net for the
   frozen image and it does not currently run — this is now the highest-value
   item in the plan, ahead of any further module work.
2. Finish Phase 0's baseline: commit `docs/baseline-2026-08-07.md` and tag.
3. Phase 2's second half: `raven.core` — `Log`, `StringId`, `Clock`, `Config`,
   `FileSystem`, `Handles`, `Profiler`, events; toml++ leaves the public API.
4. Phase 3: `raven.platform` + `raven.input`, which is the module most likely to
   be reused first somewhere else.
