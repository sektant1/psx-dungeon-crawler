# Raven Engine {#mainpage}

Two things in one place: the generated reference for the public C++ API, and
the handbook the engine is actually written against. The reference tells you
what a type is; the handbook tells you why it is shaped that way and what
breaks if you change it.

Everything here is generated from the tree by `make docs`. Nothing is
hand-maintained except the pages themselves.

## What the code is made of

| Layer | Owns | Talks to |
|---|---|---|
| `eng` | Platform startup, rendering, input, physics, audio, configuration, logging, the live debug UI | Nothing above it |
| `gen` | Deterministic dungeon generation and validated layout data | `eng` |
| game | Turning a generated layout into a playable level: locomotion, weapons, targeting, interaction, transitions | `eng`, `gen` |

Vulkan, OGRE and SDL implementation types stay behind the public headers in
`engine/include/eng`. Game code reaches the renderer through opaque handles,
never through an OGRE object. What enforces that, and what happens when
something tries to cross a layer, is in @ref doc-architecture.

### Entry points

- eng::Engine owns application lifetime and frame ordering.
- eng::Renderer is the handle-based rendering facade.
- eng::Input exposes configured actions and raw look input.
- eng::Physics owns collision-world integration.
- gen::Layout represents a validated generated dungeon.
- `FpsController` applies first-person locomotion.
- `DungeonMap` builds and queries the live dungeon geometry.

## Foundations

- @subpage doc-architecture — the layering, and the checks that keep it honest.
- @subpage doc-engine-foundations — timelines, the id type, and the profiler underneath everything else.
- @subpage doc-ecs — what an object *is* here, who owns its lifetime, and how to add a component.
- @subpage doc-scenes — a scene is a set of filled roles, and the roles decide what kind of scene it is.
- @subpage doc-projects — shipping a different game on this engine, without writing C++.
- @subpage doc-scripting — how a `.lua` file becomes behaviour, what it can reach, and what happens when it breaks.

## Rendering

- @subpage doc-render-presets — one complete look, picked by name and never assembled by hand.
- @subpage doc-particles — the CPU simulation and GPU instancing that replaced OGRE's particle system.
- @subpage doc-portals — the membrane shader, as distinct from the prop it lives in.
- @subpage doc-camera-systems — three camera shapes, one seam, selected by a component.
- @subpage doc-render-batching — what draw submission cost, and the measurement that fixed it.
- @subpage doc-psx-demo — the shop window: three set pieces built to show the renderer alone.

## Content pipeline

- @subpage doc-assets-pipeline — how a file an artist made becomes something the game loads.
- @subpage doc-asset-naming — stable id, display label, runtime path, and why they are three things.
- @subpage doc-authoring-a-prop — Blender to a spinning object in a playable scene, with no C++.
- @subpage doc-authoring-shots — a small scene that plays itself, and how to record it.
- @subpage doc-clips — short authored animations: doors, platforms, light ramps, camera pushes.
- @subpage doc-actor-animation — the one rigged humanoid every actor wears, and the shared clip library.
- @subpage doc-art-asset-checklist — the art bible and the order it has to be locked in.
- @subpage doc-level-start-hall — the scene the editor opens with no `SCENE=`.
- @subpage doc-level-turntable — the turntable staging scene, entity by entity.

## Gameplay

- @subpage doc-fps-gameplay — movement, and the path from a button press to a hit. Simulation.
- @subpage doc-fps-viewmodel — where the hands sit and how they move. Presentation.
- @subpage doc-enemies — enemies are a table in a TOML file; there is no C++ to write.
- @subpage doc-audio-system — the runtime mixer, and the authored cue policy on top of it.

## Tools and UI

- @subpage doc-world-editor — the editor measured against *Game Engine Architecture* §15.4.
- @subpage doc-scene-editor-entities — the scene tree, the component stack, and adding to either.
- @subpage doc-editor-ui-architecture — the editor shell, which is Godot's, deliberately and in full.
- @subpage doc-ui-architecture — two UIs sharing one imgui context while wanting opposite things.
- @subpage doc-ui-scenes — screen-space UI authored as entities, solved the same way in editor and game.

## Working on the engine

- @subpage doc-build-system — what the build output is telling you, and the two failures that look like slowness.
- @subpage doc-debug-console — the filtered log and command line shared by all three apps.
- @subpage doc-connector — a browser window onto the engine's debug channels.
- @subpage doc-memory-profiling — the three budgets a frame spends, and the tools pointed at each.
- @subpage doc-debugging-renderdoc — how to take a GPU capture of this frame graph and read it.
- @subpage doc-renderdoc-ai-testing — deterministic screenshots, frame-time reports, single-frame captures.

## Design record

Dated documents, kept as written. They record what was decided and why, not
what the code does now — when the two disagree, the code and the handbook win.

- @subpage doc-design-readme — what lives in this section.
- @subpage doc-design-gdd — the game design document. Player-facing intent.
- @subpage doc-design-gedd — the engine design document.
- @subpage doc-design-2026-08-06-scene-contract-and-one-component-standard
- @subpage doc-design-2026-07-31-unified-asset-root
- @subpage doc-design-2026-07-31-mesh-importer-plan
- @subpage doc-design-2026-07-31-asset-naming-map
- @subpage doc-design-2026-07-29-rhi-and-module-contracts
- @subpage doc-design-2026-07-29-modular-dungeon-rewrite
- @subpage doc-design-2026-07-27-prototype-content-reset-and-pipeline
- @subpage doc-design-2026-07-27-content-pipeline-format-research
- @subpage doc-design-2026-07-27-content-pipeline-design-review
