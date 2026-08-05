# Developer console

`eng::DebugConsole` (`engine/include/eng/debug/Console.h`) — one dockable imgui
window with a filtered log and a command line, shared by the game, the scene
editor and the psx demo. Toggle: `` ` `` in all three. `RAVEN_CONSOLE=1` opens it
on frame 1 (headless captures have no key to press).

It is not `eng::DebugTools`. That panel tunes subsystems with sliders and needs
a live pointer to each of them; this one only needs text, so it works before a
scene exists and keeps working when the thing being debugged is the scene build.
`RAVEN_TUNE=1` opens *that* panel on frame 1, for the same reason `RAVEN_CONSOLE`
opens this one: a deterministic capture has no key to press, so it is the only
way a screenshot can prove the sliders draw.

## Log

`eng::log::addSink` mirrors every `log::info/warn/error/fatal` into the console;
stderr keeps its copy, so a crash still leaves a terminal trace. Sinks may fire
from any thread — lines queue and drain on the UI thread.

Two things feed it that are easy to assume and wrong:

**The backlog.** `eng::log` retains the last 512 lines whether or not anything
is listening, and replays them to each new sink under the sink lock (in order,
no gap, no duplicate at the seam). Without it every line written before
`captureEngineLog()` was dropped — window creation, GL capabilities, asset-root
registration, shader compile failures, the boot warmup. That is the part of a
run you open a console to read, and a console built in `onStart` could never
show it. `setBacklogCapacity(0)` restores the old free path; live output is
unaffected either way. Tested headless by `engine/tests/LogBacklogTests.cpp`.

**Ogre's log.** `RenderCore` attaches a listener to Ogre's default log and
mirrors `LML_WARNING`/`LML_CRITICAL` into `eng::log` as `Ogre: …`. This is where
`has no supportable Techniques`, GLSL compile output, missing resources and
compositor rejections come from — all of which used to exist only in
`ogre.log`, so a broken material presented as a blank object with a clean
console. Trivial/normal Ogre chatter stays in the file: it is one line per
resource and would bury everything else.

### The row

```
 0.69 info Warmup             100 materials, 0 unsupported
 0.69 WARN                    Unknown RAVEN_RENDER_PRESET 'bogus'; using …
 └ bar └ time └ level └ subsystem   └ message
```

Four fixed columns, because the imgui font is monospace and a ragged left edge
is what makes a log unscannable. Each carries one thing:

- **severity bar** — 2 px, full strength above info, a hint at info.
- **level tag** — `info` / `WARN` / `ERR ` / `DEAD`, and `>` for an echoed
  command. A *word*, not colour alone: severity used to be carried only by the
  bar and the text tint, so every info line — which is most of them — looked
  identical, and telling a warning from a note meant comparing two shades of
  brass. Colour is the fast path; the tag is the one that survives a
  colourblind viewer and a washed-out screenshot.
- **subsystem** — see below. Click it to isolate that subsystem; click again to
  clear.
- **message** — default text colour at info. A log tinted top to bottom by
  severity has no hierarchy, because the eye has nothing to compare against.

Warnings and above also get a 10 % background wash across the row: scrolling a
thousand info lines, a band is findable before any text is. Severity colours are
the active theme's accents (Ash Reliquary brass/blood/violet), not an
independent palette — keep them in step when the theme changes.

### Subsystems

Everything in this engine logs as `Subsystem: what happened`. `splitLogCategory`
recovers that prefix, so the captured line is filed under `Warmup`, `Ogre`,
`ParticleMaterials` … instead of one category called `engine` — which is what it
used to be, on every single row: the column showed the same word everywhere, the
category filter could not narrow anything, and the eye had no left edge to scan.

Only a plausible subsystem is split off: a short (≤ 18), space-free,
non-numeric token followed by `": "`. Paths (`C:/assets`), prose (`note that:
…`) and measurements (`42: …`) keep their full text and stay uncategorised.
`game/tests/DebugConsoleTests.cpp` pins both directions.

| Feature | Notes |
|---|---|
| level chips | click to filter; the number is the count |
| filter box | case-insensitive substring over text + category |
| category filter | click a subsystem cell, or right-click a line |
| repeats | identical consecutive lines collapse to `xN` |
| scrollback | 4096 lines, `setCapacity`, ring-dropped from the front |
| options popup | timestamps, categories, wrap, autoscroll, copy visible |

Rendering uses `ImGuiListClipper`: a full scrollback costs the ~40 visible rows.

## What a run reports

A console is only worth opening if the run says something. These fire once at
boot, on every app:

| Subsystem | Line |
|---|---|
| `GPU` | device name; render system, GLSL version, texture units, MRT count |
| `Window` | resolution, vsync |
| `Assets` | materials + textures parsed, and the two roots they came from |
| `Engine` | config path, actions bound, render profile (and every switch) |
| `Warmup` | materials compiled, unsupported count, textures resident |
| `Load` | per-step load timings |
| `Scene` | nodes/lights/sprites/batches destroyed on every `clearScene` |
| `Level` | generated: depth, **seed**, rows, spawn. Authored: map path, placements |

The seed matters most of those: it is the whole reproduction recipe for a
generated level, and without it "the layout that trapped the player" is
unrecoverable. `Scene` matters for the opposite reason — a wipe is the most
destructive thing the renderer API does, and in silence an empty room and a
failed build looked identical.

## Commands

```cpp
console.registerCommand("spawn", "spawn N of an enemy",
                        [&](const eng::DebugConsole::Args& a) { ... },
                        [&](const eng::DebugConsole::Args&) { return names; });
console.bindFloat("r.fov", &fov, 40.0f, 110.0f);   // read bare, assign clamped
```

`Args[0]` is the command name. Arguments split on whitespace; double quotes keep
one together. Tab completes the command name, or an argument through the
command's `Completer`; ambiguity completes the common prefix and prints the
candidates. Up/Down walk history. Built-ins: `help`, `clear`, `echo`, `history`.

Per app:

| App | Commands |
|---|---|
| any `FpsGameApp` | `quit`, `r.preset`, `stats`, `colliders`, `perf`, `tune`, `time.scale`, `time.pause`, `time.step`, `profile`, `profile.csv` |
| scene_editor | `quit`, `cook`, `play`, `frame`, `scene`, `save` |
| psx_demo | `quit`, `pause`, `restart`, `tune`, `perf`, `hud`, `r.preset`, `cam.*`, `demo.*` |

Parsing and dispatch never touch imgui, which is what makes
`game/tests/DebugConsoleTests.cpp` a headless test.

### Time and profiling

`time.*` acts on the engine's **game** clock only (`eng::Clock`, see
[engine-foundations.md](engine-foundations.md)). The loop, the renderer and the
console itself run off the real clock, so a paused world is still inspectable:

```
time.pause          freeze the simulation; the camera and UI keep running
time.step           advance exactly one 1/60 s step while paused
time.scale 0.15     slow motion; 2 fast-forwards; 0 is another way to freeze
profile             last frame's timing hierarchy, self time and call counts
profile.csv out.csv the same tree as a spreadsheet
```

`RAVEN_PROFILE=<n>` prints that tree to the log every *n* frames with no UI at
all — the version that works over ssh or inside a capture run.

## Adding it to a new app

```cpp
eng::DebugConsole mConsole;                    // member
mConsole.captureEngineLog();                   // onStart
mConsole.registerCommand(...);
if (in.wasPressed("dev_console")) mConsole.toggle();   // onFrameBegin
mConsole.draw();                               // onGui
```

`FpsGameApp` subclasses get all of that already — reach the instance with
`devConsole()` and only register the game's own commands.

## Layout

`eng::DebugTools` is a set of **dockable windows**, not one column of tabs.
Docking is enabled globally in `RenderCore` (`ImGuiConfigFlags_DockingEnable`),
so the game, the scene editor and the psx demo all get it.

A dockspace covers the viewport with a passthrough centre — the game keeps
rendering through the middle — and each panel is a plain window docked into it.
The shipped arrangement puts three or four tabs in a node, never eleven in one:

| Node | Group | Panels |
|---|---|---|
| left | `PanelGroup::World` | Player, Colliders, Animation, Viewmodel |
| right (upper) | `PanelGroup::Look` | Render, Shaders, Materials |
| right (lower) | `PanelGroup::Content` | Portal, VFX, Particles, Audio |
| bottom | `PanelGroup::Gameplay` | Combat, Feel, Enemies |

Viewmodel sits beside Player because FOV, look sensitivity and where the hands
are framed are one tuning session; it also draws manipulator handles over the
game view. See [fps-viewmodel.md](fps-viewmodel.md).

An app picks a group when it registers: `addPanel("Combat", fn,
eng::PanelGroup::Gameplay)`. The default is `Gameplay`, so an existing two-arg
call still compiles and lands at the bottom.

### The layout is saved

`eng::imgui_layout` (`engine/include/eng/render/ImGuiLayout.h`) persists the
arrangement — and **only** the arrangement. Dear ImGui's ini format holds window
position, size, collapsed state and dock node, and nothing else in this engine
lives in an imgui window: every tuning control writes straight through to the
system it edits, so no slider value, render profile or gameplay state can end up
in the file.

It exists so an arrangement can be *shipped*: drag the panels where you want
them, quit, commit `assets/ui/debug_layout_<app>.ini`.

| | |
|---|---|
| Path | `assets/ui/debug_layout_<game\|psx_demo\|scene_editor>.ini` |
| Named after | the running executable — not the window title, which carries a build tag |
| Saved | on imgui's timer, and again on shutdown so the last minutes of a session are not lost |

Turning it off, in precedence order:

1. `-DENG_UI_LAYOUT_PERSISTENCE=0` at build time. The engine never opens the
   file and `DebugTools` rebuilds the shipped layout every run — the behaviour
   that predates this, and what a shipping build wants.
2. `RAVEN_UI_LAYOUT=0` (or `off`/`none`) for one run.
3. `RAVEN_UI_LAYOUT=<path>` to redirect it — a scratch file, or one file shared by
   all three apps.

Set (1) or (2) for a headless capture of the tool UI: a restored layout would
make the screenshot depend on what somebody dragged last session.

Two consequences worth knowing:

- `DebugTools::draw` **skips** `buildDefaultLayout` when a layout was restored,
  or the arrangement would be undone on the first frame the console opened,
  every run. `resetLayout()` is the one call that overrides that and puts the
  shipped arrangement back.
- A panel added *after* a layout was saved has no entry in the file, and imgui
  would open it floating over the middle of the game. `placeNewPanels` docks
  those beside a panel of their own `PanelGroup` that the saved file does place,
  so a new tab lands where the default layout would have put it without
  disturbing anything else.

### Portal and VFX are engine panels

`eng::SurfacePanels` (`engine/include/eng/debug/SurfacePanels.h`) owns both.
They tune engine shaders — `portal.frag` over `surface_common.glsl`, and
`liquid.frag` / `lava.frag` over `scroll_common.glsl` — so they live with the
shaders rather than with whichever app happens to draw one. An app registers the
materials it actually has and gets the tabs:

```cpp
eng::SurfacePanels mSurfaces;                              // member
mSurfaces.addPortal("Descent", "Game/Vfx/PortalDown", tuning, "slime_stylized.png");
mSurfaces.addLiquid("Water", "Game/Vfx/Water");
mSurfaces.addLava("Lava", "Game/Vfx/Lava");
mSurfaces.setBloom(eng::renderPresetBloom(eng::kDefaultRenderPreset));
mSurfaces.install(mTools);                                 // onStart
mSurfaces.setRenderer(&engine.renderer());                 // onGui, per frame
```

Registering nothing skips the tab entirely, so an app that owns no surface
shader pays nothing. A material name is the identity, because the knobs are
shader uniforms shared by every mesh wearing it — which is also why the tuning
survives a level rebuild. Every widget writes straight through with
`setMaterialParam`; the panel caches a number so a slider has somewhere to keep
it between frames, and nothing else.

`setPortalDressing(fn)` adds an app-owned section at the bottom of the Portal
tab. The game uses it for the portal *prop* — the light the membrane throws into
the room and the wisps drifting in front of it — which is level state, not
shader state, and is the only part of that tab the engine must not know about.

The demo registers its showcase portal and its three liquid pools the same way,
which is what makes `make psx-demo` a place to tune a surface shader against
every render profile in turn.

### Particles is an engine panel too

`eng::ParticlePanel` (`engine/include/eng/debug/ParticlePanel.h`) browses
`eng::ParticleLibrary`, edits an effect field by field, and spawns it into
whatever scene is on screen — ahead of the view or at a fixed world point, with
the same `ParticleSpawnOptions` gameplay passes. Same argument as above: the
library, the texture import and `Renderer::spawnParticles` are all engine
machinery.

```cpp
eng::ParticlePanel mParticlePanel;                          // member
mParticlePanel.install(mTools, eng::PanelGroup::Content);   // onStart
mParticlePanel.setSources(&engine.renderer(), &mParticles); // onGui, per frame
mParticlePanel.update(f.dt);                                // per frame
mParticlePanel.releaseSpawns();                             // before a scene clear
```

`update(dt)` has to run whether or not the tab is visible: it retires the
one-shots the panel spawned, and a session that only ticked while the tab was
open would hold every burst it ever fired. The panel's editing and spawning
model, including why it has no delete and no rename, is in
[particles.md](particles.md#the-particles-tuning-panel).

Panels carry no position or size of their own — the dock node owns both, and the
viewer owns the node. imgui persistence is off (`io.IniFilename = nullptr`), so
the layout is rebuilt from `buildDefaultLayout()` every run rather than restored
from whatever was dragged last session; that is what keeps a headless capture of
the panels reproducible. `resetLayout()` drops it mid-session.

## Themes

`eng::imguitheme` (`engine/include/eng/render/ImGuiTheme.h`) registers named
styles; `RenderCore` applies `raven_editor` at startup, or `RAVEN_IMGUI_THEME`.

`raven_editor` uses black void, layered gunmetal, cold chrome and restrained red
sampled from the Raven logo references. Red is reserved for active, selected,
focused, and destructive states; normal hover stays steel grey. Hairline borders
and zero rounding preserve the logo's faceted mechanical language. Full editor
topology and palette tokens live in
[Editor UI Architecture](editor-ui-architecture.md).

The *font* is not matched. ImGui uses DejaVu Sans Mono; the HUD uses a bitmap
font (`ui_regular.png`) drawn by `UiCanvas`, which is a different mechanism and
is authored for the low-resolution canvas rather than for tool text.
`registerTheme(id, fn)` adds one — `fn` mutates the live `ImGuiStyle` and is
re-run on every `apply()`, so themes hot-swap between frames.
