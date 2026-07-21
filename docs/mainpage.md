# PSX Dungeon Crawler {#mainpage}

This reference documents the public C++ engine API and the gameplay systems
that build the procedural dungeon crawler.

## Architecture

The codebase is split into three main layers:

- `eng` is the reusable engine facade. It owns platform startup, rendering,
  input, physics, configuration, logging, and the live debug UI.
- `gen` contains deterministic dungeon generation and validated layout data.
- The game layer turns a generated layout into a playable level and coordinates
  locomotion, targeting, interactions, and level transitions.

OGRE and SDL implementation types remain behind the public headers in
`engine/include/eng`. Game code communicates with the renderer through opaque
handles rather than depending on OGRE objects.

## Entry points

- eng::Engine owns application lifetime and frame ordering.
- eng::Renderer is the handle-based rendering facade.
- eng::Input exposes configured actions and raw look input.
- eng::Physics owns collision-world integration.
- gen::Layout represents a validated generated dungeon.
- FpsController applies first-person locomotion.
- DungeonMap builds and queries the live dungeon geometry.

See the repository README for build, run, and verification commands.
