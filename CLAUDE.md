# CLAUDE.md

You are a senior C++ game-engine programmer specialised in FPS systems, ballistics
and weapon handling, retro 3D rendering, ECS architecture, AI, animation, and
data-driven engine design.

You are working inside an existing custom C++ 3D engine. It uses C++20, Vulkan
1.3 behind an RHI, EnTT, Jolt Physics, ImGui, ozz-animation, and TOML for
configuration.

Do NOT assume the current architecture is correct, and do NOT introduce a second
parallel architecture where an equivalent engine system already exists. Inspect
before you build: engine/game separation, ECS usage, the scene and prefab
format, input, the Jolt character controller, camera rigs, the renderer, asset
and resource management, the fixed-timestep loop, the event systems, the editor,
and the existing player and weapon code.

---

# THE GAME

**A realistic post-apocalyptic PSX first-person milsim sandbox with zombies.**

Four words carry the whole design, and they pull against each other on purpose:

**REALISTIC.** Weapon handling is simulated, not abstracted. Rounds are
projectiles with muzzle velocity, drag, gravity and wind. Armour is defeated by
penetration class, not worn down by damage. Magazines are counted, reloads take
real time and can be interrupted, and a magazine swapped out half-full keeps the
rounds that were in it. The player is fragile and so is everything else.

**POST-APOCALYPTIC.** Scavenging is the loop. Ammunition is scarce enough that
which cartridge you load is a decision; medical supplies, batteries, fuel and
tools are all things you carry, weigh, and run out of. The world is overgrown
and looted, not blasted — treelines reclaiming roads, houses with the furniture
still in them.

**PSX.** The visual language is fifth-generation console: low-poly models,
64–256px hand-drawn textures sampled nearest-neighbour, vertex lighting,
affine texture warping, geometry snapping, dithering, and a low internal
resolution upscaled. This is a shipped, finished look — see "The image is
frozen" below.

**MILSIM SANDBOX.** Systems over scripting. The game does not stage set-pieces;
it runs ballistics, AI, needs and an economy, and the player's stories come out
of those interacting. There is no fail state authored into a mission — there is
a world that keeps going.

**ZOMBIES.** Slow, numerous, and attracted by sound. They exist to make firing a
gun a decision. A suppressed pistol and a rifle solve the same problem
differently, and the second one brings company.

## What that means concretely

The tension to keep resolving: **simulation depth vs PSX readability.** A round
that penetrates a wall must still be a thing the player can see and understand
at 320x240 with 15 colours on screen. When the two conflict, the readable
choice wins — an unreadable simulation is indistinguishable from a broken one.

Prioritise: responsive movement, honest weapon handling, sound as a first-class
game mechanic, scarcity that changes behaviour, and AI that reacts to the world
rather than to the player's position.

Avoid: regenerating health, ammo counters that never matter, hitscan weapons,
bullet-sponge enemies, quest markers, and anything that reads as a modern
military shooter's power fantasy. The player is not a special forces operator;
they are somebody who found a rifle.

---

# CONTENT: THE BUILT-IN LIBRARY

`assets/source/` holds vendor asset packs that ship WITH the engine — its
default content library, not one game's art. They are imported into engine
content by `tools/import_asset_pack.py`, driven by `assets/source/packs.toml`.

**Never hand-edit generated content.** These files say so in their own headers
and are overwritten by the next import:

```
assets/meshes/<domain>/          assets/materials/<domain>.mat
assets/textures/<domain>/        assets/prefabs/<domain>.prefab.toml
```

Change the manifest entry and re-import instead. To retexture, create a
*material variant* — see
`docs/design/2026-08-07-retexturing-and-material-variants.md`.

Domains, and what each is for in this game:

| Domain | Source pack | Role |
|---|---|---|
| `firearms` | modern-guns | the weapon set — 16 guns, real dimensions |
| `hands` | hands | 15 first-person arm rigs (see below) |
| `apocalypse` | Post-Apocaliptic-Pack | loot: medical, food, tools, gear |
| `creatures` | units | zombies, plus animals for the world |
| `forest` | PSXForest | overgrowth, the outdoor shell |
| `furniture`, `cozy` | furniture / CozyPack | building interiors |
| `dungeon` | modular-dungeon | basements, bunkers, sewers |
| `medieval`, `secret` | — | improvised weapons and oddments |

The **hand rigs** are authored, not imported: `tools/author_hand_rigs.py` turns
the pack's single right arm into fifteen two-armed animated rigs (human skin
tones, gloves, and the non-human variants) and cooks them with `gltf2ozz`. The
set is data — `assets/config/viewmodel_hands.toml` — and swapping the player's
hands is a rig id, not a code change.

---

# BUILD, RUN & DEBUG

Everything goes through the Makefile; `make help` is the full reference, and
`docs/build-system.md` explains the parts that are not self-evident.

## When the build misbehaves, ask it

```sh
make doctor          # toolchain, tree, ccache, memory headroom, clangd
make doctor FIX=1    # and repair what is safely repairable
```

Every check exists because the failure it looks for cost real time here, and
none of them announce themselves — they all look like the build merely being
slow. Run this before theorising.

## Never clean-build

Every dependency compiles from source (SDL2, Jolt, assimp, ozz, toml++). A full
rebuild is many minutes, and **killing a build mid-link corrupts the tree**.

- Build single targets: `cmake --build build --target <t> -j3`.
- Keep `-j` low. This machine's limit is memory, not cores; an
  `internal compiler error: Bus error` means memory pressure, not a broken tree.
- **Implement the whole change first, then build once.** Repeated builds thrash
  swap and make the session slow.
- If the tree breaks, `cmake -S . -B build` regenerates makefiles without
  discarding objects. Never `rm -rf build`.

## Reading build output

Builds digest their diagnostics: errors in full, this repo's warnings collapsed
to one line, dependency warnings counted. The untouched transcript is at
`build/last-build.log`.

## Targets

```sh
make run                 # the game
make editor SCENE=x.scn  # world editor  (F5 cooks + playtests)
make cook SCENE=x.scn    # .scn -> .map
make test                # ctest
make acp                 # condition assets into build/cooked
make doctor              # diagnose the build tree
```

## Verify on screen, not just in the compiler

A change that compiles is not a change that works.

```sh
RAVEN_SCREENSHOT=/tmp/x.png RAVEN_SCREENSHOT_FRAME=120 timeout 120 ./build/game
make screenshot SHOT=/tmp/x.png FRAME=200
```

Then **read the PNG**. Real bugs in this repo were invisible to the compiler and
obvious in a screenshot: a viewport showing the font atlas, a world rendered
upside down, an entire scene stacked at the origin.

A black screenshot is usually the window being unfocused or offscreen, not a
regression — confirm against a known-good binary before chasing it.

## GPU and native debugging

```sh
make renderdoc APP=scene_editor      # RenderDoc UI, F12 to capture
make gdb APP=game BATCH=1            # run, backtrace, exit
make valgrind APP=game FRAME=120
make perf APP=game BENCH=600         # CPU profile + hot paths
```

Read `docs/debugging-renderdoc.md` before opening a capture. Two failures that
look like GPU problems and are not:

- The world renders at **a third of the window**. The pixelation is a real
  low-resolution framebuffer; tiny draw calls in `mrt` are correct.
- A material that renders blank is almost always a **GLSL compile error** logged
  at startup, not a GPU issue. Check the console first.

## The image is frozen

The PSX look — shaders, compositor, materials, presets — is a shipped result.
No refactor may change the rendered image; `make visual-test` is how that is
proven. Editor-only materials are exempt; nothing in the game references them.

---

# ARCHITECTURE RULES

1. Gameplay must not depend on ImGui.
2. Weapon configuration must not spread through PlayerController.
3. Rendering must contain no weapon-specific gameplay logic.
4. Physics knows only generic ownership and collision data about a projectile,
   never which weapon made it.
5. Projectiles, weapons, cartridges and prefabs are **data**. Adding one is a
   TOML edit; adding a new *kind* of one is the only thing that is C++.
6. Input issues gameplay commands rather than mutating renderer objects.
7. Viewmodel presentation stays separable from weapon simulation.
8. Reuse the engine's conventions where they are sane. Refactor local
   architecture where it blocks you; do not perform repo-wide rewrites.
9. Do not build an enterprise framework for three of something. Do create an
   abstraction where variation already exists — weapons, cartridges,
   projectiles, viewmodels, prefabs.

## Engine conventions that cost real debugging time

- **A node's forward is its local -Z.** The camera view matrix is
  `inverse(cameraWorld)` and `FpsController::forward()` is `(-sin y, 0, -cos y)`.
  An authored character model must face -Z.
- **`Renderer::clearScene()` destroys every mesh, skinned ones included.**
  Anything uploaded once at start-up is a dead handle after the first level
  build. Reload after each build. The symptom is a silent fallback, not a crash.
- **Anchors differ**: the player's character node is at the FEET, a Jolt capsule
  reports its CENTRE, NPC nodes are authored raised. `game::actor::ActorAnchor`
  names which.

---

# WORKING STYLE

- Inspect before modifying; search the repo before assuming something is absent.
- Do not fabricate APIs.
- Follow the project's naming and comment conventions
  (`docs/asset-naming.md`).
- Remove dead implementation after replacing it. Do not leave TODO architecture
  in place of essential functionality.
- Do not hide compile errors. Fix what your changes broke.
- Keep changes logically separable.
- Report honestly: if tests fail, say so with the output; if you skipped
  something, say that.

When you find a design decision, prefer **simple, data-driven, composable,
tunable, extensible** over generic frameworks, deep inheritance, hardcoded
logic, and God classes.
