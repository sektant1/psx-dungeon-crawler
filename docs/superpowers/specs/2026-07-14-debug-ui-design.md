# Debug UI (Dear ImGui) — Design

Date: 2026-07-14
Status: approved

## Goal

In-game debug panel to live-tune shaders, engine, scene, and game parameters
in both targets (`game`, `psx-demo`). Always compiled; hidden by default,
toggled with F1. Session-only tweaks with a "copy as TOML" escape hatch.

## Integration

- Rendering: `Ogre::ImGuiOverlay` from the system OGRE 14.5.2 Overlay
  component (bundled `imgui.h` at `/usr/include/OGRE/Overlay/imgui.h`).
  No vendored ImGui, no new third-party dependency.
- `Ogre::OverlaySystem` is created in `RenderCore` and registered as a
  render-queue listener on the scene manager.
- Input: a hand-written SDL2 → `ImGuiIO` bridge (~100 lines) inside
  `engine/src`, compiled against Ogre's bundled `imgui.h` so there is no
  version-mismatch risk. We deliberately do not use `OgreBites` or upstream
  `imgui_impl_sdl2`.

## Module layout

- `engine/include/eng/DebugUi.h` — public facade, no Ogre/ImGui includes.
- `engine/src/DebugUi.cpp` — ImGuiOverlay setup, SDL event bridge,
  built-in panels, copy-as-TOML.
- Engine owns the instance: `Engine::debugUi()` accessor, same pattern as
  `renderer()` / `input()`.
- Game code may include `<imgui.h>` (Ogre's bundled copy is on the include
  path) inside registered panel callbacks. Ogre types never leak.

## Public API

```cpp
class DebugUi {
public:
    void addPanel(const std::string& name, std::function<void()> draw);
    bool visible() const;
    void setVisible(bool v);
};
```

- Panels render as collapsible headers inside a single "Debug" window:
  built-in panels first, registered game panels after, in registration
  order.

## Input routing and toggle

- F1 toggles visibility. Handled inside the engine event pump (hardcoded
  key for now; `[bindings]` integration is future work).
- Panel open → SDL relative mouse mode off, cursor visible. Panel closed →
  previous grab state restored.
- SDL events flow to the ImGui bridge first. While
  `ImGuiIO::WantCaptureMouse` is set, game-facing `Input` reports zero
  mouse delta and no mouse buttons; while `WantCaptureKeyboard` is set,
  key state is suppressed. Game code needs no capture checks.

## Built-in panels

1. **Stats** — FPS, frametime graph (ring buffer + `ImGui::PlotLines`),
   batch/triangle counts from `RenderWindow::getStatistics()`, window
   size, camera position.
2. **PSX Shaders** — precisionMultiplier (vertex snap), colDepth,
   ditherBanding toggle, dither compositor on/off, fog colour + density,
   ambient colour, background colour. Colour widgets pick sRGB; the engine
   linearises (pow 2.2) before applying, matching the existing convention.
3. **Camera** — vertical FOV, near/far clip.

Game target registers a **Player** panel (move speed, mouse sensitivity)
via `addPanel`.

## Renderer state cache

Fog, ambient, background, camera FOV/clip are currently write-only through
`Renderer`. `RenderCore` gains a small cached-state struct updated by the
existing setters, plus getters, so panels can display current values.

## Persistence

Session-only. A single global button serializes the current
tweakables to a TOML snippet, copies it to the clipboard via
`SDL_SetClipboardText`, and mirrors it through `log::info`. No config
write path, no override layering.

## Frame flow and compositor ordering

`Engine::renderFrame`: `ImGuiOverlay::NewFrame()` → build panel widgets →
`renderOneFrame()`. The UI must render after the dither compositor so it
stays crisp (not colour-crushed/dithered); the compositor output pass must
render overlays. Verify and adjust the compositor script during
implementation — known Ogre detail, low risk.

## Verification

- `PSX_SCREENSHOT` hook unchanged; panel hidden by default so existing
  screenshots are unaffected.
- New `PSX_DEBUG_UI=1` env var forces the panel open, so the screenshot
  hook can verify ImGui renders in both targets.

## Out of scope

- Docking, multi-window, standalone tool windows.
- Config file writing / startup override layer.
- Rebindable toggle key.
- Entity/node inspector beyond camera + lights already exposed.
