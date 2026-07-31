# Developer console

`eng::DebugConsole` (`engine/include/eng/debug/Console.h`) — one dockable imgui
window with a filtered log and a command line, shared by the game, the scene
editor and the psx demo. Toggle: `` ` `` in all three. `PSX_CONSOLE=1` opens it
on frame 1 (headless captures have no key to press).

It is not `eng::DebugTools`. That panel tunes subsystems with sliders and needs
a live pointer to each of them; this one only needs text, so it works before a
scene exists and keeps working when the thing being debugged is the scene build.

## Log

`eng::log::addSink` mirrors every `log::info/warn/error/fatal` into the console;
stderr keeps its copy, so a crash still leaves a terminal trace. Sinks may fire
from any thread — lines queue and drain on the UI thread.

| Feature | Notes |
|---|---|
| level chips | click to filter; the number is the count |
| filter box | case-insensitive substring over text + category |
| category filter | right-click a line → `filter to [x]` |
| repeats | identical consecutive lines collapse to `xN` |
| scrollback | 4096 lines, `setCapacity`, ring-dropped from the front |
| options popup | timestamps, categories, wrap, autoscroll, copy visible |

Rendering uses `ImGuiListClipper`: a full scrollback costs the ~40 visible rows.

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
| any `FpsGameApp` | `quit`, `r.preset`, `stats`, `colliders`, `perf`, `tune` |
| scene_editor | `quit`, `cook`, `play`, `frame`, `scene`, `save` |
| psx_demo | `quit`, `pause`, `restart`, `r.preset` |

Parsing and dispatch never touch imgui, which is what makes
`game/tests/DebugConsoleTests.cpp` a headless test.

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

## Themes

`eng::imguitheme` (`engine/include/eng/render/ImGuiTheme.h`) registers named
styles; `RenderCore` applies `one_dark` at startup, or `PSX_IMGUI_THEME`.
`registerTheme(id, fn)` adds one — `fn` mutates the live `ImGuiStyle` and is
re-run on every `apply()`, so themes hot-swap between frames.
