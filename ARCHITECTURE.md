# Architecture

How the code is layered, and what enforces the layering. For *what the game is*
see `AGENTS.md`; for asset authoring see `ASSETS_PIPELINE.md`.

## Layers

Dependencies point downward only. Each layer is its own static library, so a
dependency that points upward is a **link error**, not a review comment.

| Target | Owns | May link |
|---|---|---|
| `game` (exe) | combat, dungeon generation, player, bosses, items, UI | `eng` |
| `eng` | application lifetime (`Engine`), the facade consumers link | `eng_framework` |
| `eng_framework` | ECS scene, scene sync, controllers | `eng_systems` |
| `eng_systems` | renderer, physics, audio, particles — the only layer that sees Ogre/Jolt/SDL/miniaudio | `eng_platform` |
| `eng_platform` | window, input, config | `eng_core` |
| `eng_core` | log, io, geometry, events, profiling, step clock | glm only |

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

## Content checks

A missing mesh or material does not crash this engine: `eng::prototype` draws a
box or a checkered surface instead. That is right at runtime and wrong at build
time — it turns a typo into a scene that quietly looks wrong. `tools/assetlint.py`
(the `assetlint` ctest) resolves every texture, mesh and material reference in
each application's asset tree and fails on a dangling one, a duplicate material
name, or two textures sharing a basename (Ogre's lookup is flat).

Uniqueness is checked per application: `game` and `psx-demo` never have their
asset roots registered at the same time, so both may define `Game/Demo/Floor`.

## Running the checks

```sh
cmake --build build -j
ctest --test-dir build            # includes layering + assetlint
python3 tools/check_layering.py   # standalone
python3 tools/assetlint.py        # standalone
```
