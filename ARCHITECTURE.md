# Architecture

How the code is layered, and what enforces the layering. For *what the game is*
see `AGENTS.md`; for how a file an artist made becomes something the game loads
see [docs/assets-pipeline.md](docs/assets-pipeline.md), and for what art still
has to be made see [docs/art-asset-checklist.md](docs/art-asset-checklist.md).

## Layers

Dependencies point downward only. Each layer is its own static library, so a
dependency that points upward is a **link error**, not a review comment.

| Target | Owns | May link |
|---|---|---|
| `game` (exe) | combat, dungeon generation, player, bosses, items, UI | `eng` |
| `eng` | application lifetime (`Engine`), the facade consumers link | `eng_script` |
| `eng_script` | Lua scripting: the VM, the bindings, the script host | `eng_framework` |
| `eng_framework` | the ECS world, its components and reconcilers, controllers | `eng_systems` |
| `eng_systems` | renderer, physics, audio, particles — the only layer that sees Vulkan/Jolt/SDL/miniaudio | `eng_platform` |
| `eng_platform` | window, input, config | `eng_core` |
| `eng_core` | log, io, geometry, events, profiling, clocks, string ids | glm only |

`eng_script` sits *at* the framework layer rather than above it — the lint files
`engine/src/script` as `framework`, so its bindings may reach `World`, `Physics`
and `Input`, and an upward include is still an error. It is a separate target
because `eng_framework` is linked by every engine test and by the editor's
preview world, and keeping the VM out of that makes "who depends on Lua" a link
fact instead of a habit. See [docs/scripting.md](docs/scripting.md).

`eng` is the only target an application names. Everything else arrives
transitively.

The layers link whole-archive: with LTO, a reference materialised during the
link-time optimisation pass comes too late to pull a member out of a normal
archive, which showed up as spurious undefined symbols between layers. The cost
is binary size; the alternative was disabling LTO on the renderer.

### Why not just headers

All public headers still share one include root (`engine/include`), so the link
split alone cannot catch `#include <eng/Renderer.h>` from a core `.cpp`.
`tools/check_layering.py` classifies every engine source and public header into
a layer and fails on an upward include. It runs as the `layering` ctest.

Splitting the include root per layer is deliberately *not* done yet — it would
move ~60 headers for an enforcement guarantee the lint already provides.

### Foundations in `eng_core`

Three facilities everything above is allowed to assume, documented in
[docs/engine-foundations.md](docs/engine-foundations.md):

- **`eng::Clock`** — the game timeline, separate from the wall clock. Simulation
  reads `FrameContext::dt` (scalable, pausable); anything that must keep moving
  over a frozen world reads `FrameContext::realDt`.
- **`eng::StringId`** — 64-bit hashed names, `constexpr` from a literal, so an id
  compares in a cycle and can be a `case` label.
- **`eng::Profiler`** — hierarchical in-game timings with self time and call
  counts. `runApplication()` annotates the loop phases; a game nests its own.

### The ECS

One `eng::ecs::World` per simulated world: one registry, one hierarchy, and the
reconcilers that drive the renderer and physics from it. Components are one file
each under `eng/ecs/components/` and carry field reflection, so adding one is a
header plus one registration line — the `.map` payload, the inspector and the
add-component menu all read the same table. See [docs/ecs.md](docs/ecs.md).

The camera is one of those components, which is what makes a shot authored
rather than coded: an orbiting camera is a `Camera` parented to a pivot with a
`Spin`. See [docs/authoring-shots.md](docs/authoring-shots.md) for the workflow
and how a scene becomes a GIF.

## The asset pipeline

`assets/` holds what artists commit; `build/cooked/` holds what the game loads.
Between them is `raven_acp`, the Asset Conditioning Pipeline: it classifies every
file into one row of Gregory's figure 1.33, runs that row's exporter, and
publishes a pack indexed by `pack.manifest`.

| Target | Owns |
|---|---|
| `eng_core` (`eng/content/`) | the format table, the resource database, and the readers for every intermediate — so the game, the editor, the pipeline and the tests share one definition of what a `.rmesh` is |
| `eng_acp` | the exporters and the build graph. Above the engine: it links `eng_core` and `eng_model_import`, and nothing links it back |
| `raven_acp` | the CLI, plus the World row whose cooker lives in `game_content` |

The runtime cost of all this is two functions —
`eng::detail::loadStaticModel()` and `rhi_renderer::loadImage()` — each of which
asks `assets::conditioned()` whether a faster form exists and falls back to the
source loader when it does not. Nothing else in the engine knows the pipeline
exists.

## Content checks

A missing mesh or material does not crash this engine: `eng::prototype` draws a
box or a checkered surface instead. That is right at runtime and wrong at build
time — it turns a typo into a scene that quietly looks wrong. `tools/assetlint.py`
(the `assetlint` ctest) resolves every texture, mesh and material reference in
each application's asset tree and fails on a dangling one, a duplicate material
name, or two textures sharing a basename (Ogre's lookup is flat).

## Content root

All shipped content lives under one root, `assets/`, split into **packs**:

```
assets/
  assets.toml     the manifest: packs, mount sets, resource dirs
  engine/         engine-owned: shaders, programs, materials, fonts, particles
  game/           the game: materials, meshes, textures, scenes, config TOMLs
  demo/           the sample's deltas only -- 4 files
```

`eng::assets` (in `eng_core`) discovers the root at runtime, reads the
manifest, and resolves a *logical* path — `assets::resolve("enemies.toml")` —
against the mounted packs in order. No application bakes an asset path into its
binary any more, and `RenderCore` registers exactly the directories the manifest
declares rather than walking the tree.

**Every app mounts every pack**, so `game`, `scene_editor` and `psx_demo` all
reach the same content. Mount *order* (`game > demo > engine`, with an app's own
pack on top) only decides who wins if two packs answer to the same logical path.

That makes material names globally unique by necessity, which is why they are
`Pack/<defining .material file stem>/Name` — a rule `assetlint` checks, so it
cannot drift. Ogre resolves both material names and file basenames flatly, and
a duplicate material name is not a warning: `ResourceManager::add` throws and
the app aborts during script parsing.

Source art (`.zip`, `.rar`, `.blend`) sits under `assets/` beside the packs and
is gitignored by extension — it is a pipeline input, not a build input.

## Running the checks

```sh
cmake --build build -j
ctest --test-dir build            # includes layering + assetlint
python3 tools/check_layering.py   # standalone
python3 tools/assetlint.py        # standalone
```
