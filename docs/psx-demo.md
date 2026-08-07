# The PSX showcase demo {#doc-psx-demo}

`psx_demo` (`samples/psx-demo/`) is the engine's shop window: three set pieces
on a turntable — crystal shrine, descent portal, levitating chest — lit and
posted so the renderer, not the content pipeline, is what is on display. It is
an `eng::Application` on the same engine as the game and the editor, so every
debug surface below is the shared one.

```sh
make psx-demo                              # or ./build/psx_demo
RAVEN_RENDER_PRESET=ps1 ./build/psx_demo     # start on a specific profile
RAVEN_TUNE=1 RAVEN_CONSOLE=1 ./build/psx_demo  # open the panels on frame 1
```

## Controls

| Input | Action |
|---|---|
| mouse wheel | dolly the camera (proportional: same feel near and far) |
| left/right drag | orbit yaw + pitch, on top of the running turntable |
| `=` / `-` | dolly, for trackpads and remote sessions with no wheel |
| `.` / `,` | field of view, held rather than tapped |
| `[` / `]` | turntable rate, in 0.25x steps |
| `Space` | pause the turntable and the station animation |
| `R` | reset the whole view (camera, spin, tuning) |
| `Tab` / `Backspace` | next/previous render profile |
| `F1` | tuning panel (`eng::DebugTools`) |
| `F4` | frame-time HUD (`eng::PerfOverlay`) |
| `F2` | wireframe debug view (engine-wide, not bound in `demo.toml`) |
| `H` | the on-screen placard |
| `` ` `` | developer console |
| `Esc` | quit |

Bindings live in `assets/demo/demo.toml`; the camera is not a
character controller, so mouse input is read unclamped and ungrabbed and gated
on `Engine::imguiWants{Mouse,Keyboard}` — dragging a slider must not also fly
the camera, and typing `restart` in the console must not restart four times.

The wheel itself is engine input: `eng::Input::wheelDelta()` accumulates
`SDL_MOUSEWHEEL` per tick, folds in `SDL_MOUSEWHEEL_FLIPPED`, and reports
fractional notches on trackpads.

## The placard

The caption in the top-left corner is `eng::ui::TooltipView` — the same widget
the game puts under the crosshair for look-targets — driven by `DemoHud`
(`samples/psx-demo/src/DemoHud.{h,cpp}`) from a plain `DemoHud::Status` struct.

It replaced a world-space `attachTextSprite` placard hanging over the dais. That
sprite was a scene object: it swam with the turntable, took the render profile's
pixelation and dither (unreadable at the low-resolution profiles, which is where
a caption matters most), and had to be destroyed and rebuilt to change one word.
The tooltip is a virtual-pixel UI surface drawn after the post chain, so the
caption stays crisp under every profile while the *world* keeps the look the
demo exists to show.

Content is rebuilt every frame under one stable id (`demo/placard`), so changing
preset swaps the text without restarting the fade. The control list is
`TooltipContent::hints` — key caps in a column with their descriptions aligned
beside them (see [`ui-architecture.md`](ui-architecture.md)), not sentences: as
prose the punctuation bindings were invisible. Dolly and lens are `bars`
rather than body lines: they are the two controls whose effect on the image is
easy to confuse (6 m at 46° and 12 m at 90° frame the dais similarly and distort
it completely differently), so both carry their real value.

## Live tuning

`F1` opens `eng::DebugTools`. The engine owns the tabs that tune the engine —
render profile, stylize shaders, step clock, colliders, materials, and the
Portal/VFX surface shaders — and the demo adds one tab of its own:

| Group | What it edits |
|---|---|
| Camera rig | distance, height, pitch, FOV, zoom step, drag sensitivity |
| Motion | pause, turntable rate, animation rate, reset view |
| Surfaces | the material on each generated stone surface |
| Particles | `Renderer::setParticleQuality` |
| Presets | the render profile, same list as `Tab` |

Everything writes into one `DemoTuning` struct that `OrbitRig::apply()` pushes to
the renderer every frame, so there is no apply step and `R` is one assignment.
The same values are console bindings (`cam.distance`, `cam.height`, `cam.pitch`,
`cam.fov`, `demo.spin`, `demo.anim`): the panel is for hunting a value, the
console for setting one you already know.

The **Portal** and **VFX** tabs beside it are `eng::SurfacePanels`, not the
demo's — the membrane and the liquid shaders are engine assets, so the panels
that tune them are too (see [`debug-console.md`](debug-console.md)). The demo
registers its showcase portal and its water/slime/lava pools with their shipped
values, which makes this the fastest place to judge a surface shader: one frame
holds all four, and `Tab` walks every render profile under them.

## Dungeon materials

The sample draws the game's kit atlases, stylized surfaces and liquid profiles —
the *same* materials the dungeon is built from, not copies of them. It used to
ship a mirror of `assets/materials/{kit,fantasy_surfaces,liquids,props}.material`
plus their textures, because Ogre only ever had one app's asset roots
registered. It no longer does: the demo mounts the **game pack** underneath its
own (`assets/assets.toml`, `[mounts] demo = ["demo", "game", "engine"]`), so
`Kit/Dungeon`, `Fantasy/CarvedStone`, `Fantasy/Lava` and the prop profiles
resolve to the originals and cannot drift out of step.

What is left in `assets/demo` is only what is genuinely the demo's:
four materials in `materials/showcase.material` (the pink crystal pair and the
two showcase stones), `materials/portal.material`, and six particle effects. Adding
a copy of a game material back would not merely duplicate it — Ogre's
`ResourceManager::add` *throws* `ItemIdentityException` on a duplicate name and
the demo would abort during script parsing.

`Demo/PortalDown` is the one material that genuinely differs. The game's
`Game/PortalDown` (`assets/materials/vfx.material`) and the showcase's tune
the same `PixelVfx/PortalFS` to different ends, so the showcase's is named for
its pack.

The dais, the chest plinth and the portal backing are dressed from that set by
default, and every one of them is a `SurfaceSlot` the Surfaces group re-dresses
live. Only tiling profiles are offered: a generated primitive has 0..1 UVs over
each face, and an atlas material (`Kit/Dungeon`, `Kit/Doors`, `Kit/Containers`)
stretches its whole sheet across one of them. The kit *meshes* — the portal's
pillars and arch — are atlas-mapped and keep `Kit/Dungeon` in code.

## Verifying a change

The demo honours the engine's deterministic capture hook, but a bare run under a
compositor often writes a black PNG (unfocused/offscreen window). Drive it
through a virtual display instead:

```sh
RAVEN_SCREENSHOT=/tmp/demo.png RAVEN_SCREENSHOT_FRAME=200 \
  xvfb-run -a -s "-screen 0 1280x900x24" ./build/psx_demo
```

Then read the PNG. `Warmup: N materials, 0 unsupported` in the log is the
companion check: a material that fails to compile is logged there, not drawn.

The capture is reproducible to the pixel: two runs of the same binary at the
same frame differ in zero pixels, for the demo and for the game. That is worth
stating because it was not true until `Engine::setLoadingPhase(false)` started
rewinding the step clock — the load loop pumps work against a wall-clock
millisecond budget, so a slower shader compile means more loading frames, and
every one of them advanced the clock that drives stepped animation. Frame
counting already restarted at that boundary; the animation phase did not, so a
capture pinned to frame 300 still landed on a different viewmodel pose each run
(64k differing pixels between two runs of one binary). If a capture ever goes
non-reproducible again, compare two runs *before* blaming the change under test.
