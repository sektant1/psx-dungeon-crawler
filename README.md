# dungeon-crawler

C++17 engine scaffold for a PSX-style single-player FPS RPG dungeon crawler.
OGRE 14.x is the renderer (hand-written GLSL PSX shaders, no RTSS), SDL2 owns
the window and loop — both fully hidden behind the `eng` public API
(`engine/include/eng/`, GLM math, handle-based scene graph).

## Build & run

```sh
make game        # FPS test room: WASD + mouse-look, Esc releases/quits
make demo        # PSX shader demo (port of MenacingMecha's godot-psx-style)
```

Requires system OGRE 14.x (plugins `RenderSystem_GL3Plus`, `Plugin_ParticleFX`,
`Codec_STBI`), SDL2, GLM, CMake >= 3.16 — `make deps` installs all of it on
any major distro (builds OGRE from source where no >= 14 package exists).
On Wayland the Makefile forces
`SDL_VIDEODRIVER=x11` (XWayland). `PSX_SCREENSHOT=/path.png` renders 90 frames,
saves a screenshot, and exits (verification hook, both targets).

`F1` toggles the ImGui debug panel (stats, PSX shader tuning, camera, game
tunables; "Copy all as TOML" puts current values on the clipboard).
`PSX_DEBUG_UI=1` forces it open (screenshot verification).

## API documentation

Generate the engine and gameplay API reference with:

```sh
make docs
```

The command opens `build/docs/html/index.html` in the default browser after
generation. Doxygen is optional for normal builds; only the `docs` target
requires it.

## Layout

```
engine/include/eng/   public API (Engine, Renderer, Input, Config, Log, Math)
engine/src/           Ogre + SDL implementation (RenderCore, Platform, ...)
engine/assets/        PSX shader stack (GLSL, programs, materials, compositor)
game/                 the game: FPS test room scaffold + game.toml
samples/psx-demo/     renderer regression sample (godot-psx-style port)
docs/superpowers/     design specs and plans
```

Design docs: `docs/superpowers/specs/2026-07-13-engine-scaffold-design.md`.
Deviation notes for the original 1:1 Godot port live in git history
(`README.md` before the scaffold restructure).
