# UI architecture: canvas, bitmap font, tooltips

Two UIs share one imgui context, and they want opposite things.

- **Tool UI** (scene editor, debug console) wants ordinary imgui widgets with
  legible vector text and hover help.
- **Game UI** (HUD, look tooltips) wants a pixel surface: integer coordinates,
  a bitmap font, no widget chrome, no input capture.

Rendering mechanics live in the engine. Game-specific visual language and
responsive policy live in `game/`; neither targeting nor combat data owns
packed colours or screen coordinates.

```
eng::ui::BitmapFont     atlas PNG + metrics TOML -> measured, wrapped, drawn text
eng::ui::UiCanvas       virtual-pixel surface: panels, bars, text, icons
eng::ui::UiLayout       pure integer flex rows, columns, growth and wrapping
eng::ui::TooltipView    fade + placement for one TooltipContent at a time
game::GameHudStyleSheet semantic theme + compact/standard/ultrawide policy
eng::imguihint          hover help for tool windows, text from hints.toml
```

## The virtual pixel canvas

`UiCanvas::begin(displayPixels, preferred)` picks the largest **integer**
magnification that still fits `preferred` (640x480 by default), then reports
the virtual size, which covers the whole window. Every primitive takes integer
virtual coordinates and is snapped to whole device pixels.

Consequences worth knowing:

- Layouts anchor to real window corners, not to a letterboxed box.
- A half-pixel is unreachable by construction, which is why borders are four
  filled bars rather than `AddRect` (whose stroke straddles the edge).
- `hud.scale` re-bases `preferred` instead of multiplying coordinates, so a
  user scale can never reintroduce fractional geometry.
- HUD content is constrained to a centred 16:9 safe region on ultrawide
  displays. Compact mode activates below 480 virtual pixels wide or 320 high.

The canvas draws into imgui's **foreground draw list**, which is painted after
the PSX post chain (`RenderCore::postRenderTargetUpdate`). The HUD is therefore
crisp: the retro look comes from the grid and the font, not from the
compositor. **The world image is untouched, and `make visual-test` must stay
green.**

## The bitmap font

`tools/gen_font_atlas.py` rasterises a TTF into a 16-column glyph grid and
writes the metrics beside it:

```sh
tools/gen_font_atlas.py --font /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf \
    --size 13 --name ui_regular
# -> engine/assets/fonts/ui_regular.png + ui_regular.toml
```

Glyph coverage is thresholded to full alpha: no anti-aliased fringe survives,
which is what makes a vector face read as a pixel font when magnified.
Advances come from the inked bounds, so the result is proportional. Line height
is packed tighter than the cell, because the cell reserves a full descender
under every glyph and most HUD strings are upper case.

Both files resolve by bare filename: `RenderCore` registers every subdirectory
of the asset roots as an Ogre resource location, so adding a font is a matter
of dropping two files into `engine/assets/fonts`.

The atlas is uploaded as a one-mip manual texture and forced to `GL_NEAREST`,
because a texture imgui samples directly carries its own filter state.

`BitmapFont::wrap` preserves explicit line breaks and splits words wider than
their box. `ellipsize` provides deterministic one-line overflow. Tooltip body
copy is capped at three lines; compact weapon and banner labels are ellipsized,
so authored text cannot escape panel bounds.

## Key caps

Bindings are a canvas primitive, not a string convention:

```cpp
const int w = canvas.keyCap({x, y}, "SPACE");        // returns the plate width
canvas.text({x + w + 6, y}, "pause", palette.textDim);
canvas.keyCapWidth("SPACE");   // measure without drawing, for layout
canvas.keyCapRow();            // vertical pitch for a stacked column
```

`keyCap` takes the *text* origin a `text()` call would take and places the plate
around it, so a cap and the words beside it align by construction. The plate is
sized from the glyph **cell** (`BitmapFont::cellHeight`), not from the line
height: the cell reserves rows above and below the ink, and a plate cut to a
line height clips the letters it is supposed to contain.

Nothing writes `"[" + key + "]"` any more. A binding packed into prose stops
reading as a binding at all — `` ` console  ESC quit `` scans as a typo, and
`, . fov` as a comma splice — so `TooltipContent::hints` carries
`{key, label}` pairs and the widget lays out the cap column with the
descriptions aligned beside it. `TooltipStyle::maxHints` bounds the list; when
the panel runs out of vertical room, body copy is dropped before bindings are.

Consumers: the tooltip's action verb, `TooltipContent::hints` (the demo
placard's control list), and the game's armament plate.

Two fonts exist in the imgui atlas and the order matters:

| slot | face | used by |
| --- | --- | --- |
| `Fonts[0]` | imgui built-in (ProggyClean) | `Renderer::attachTextSprite` world labels |
| `Fonts[1]` | vendored DejaVu Sans Mono, `io.FontDefault` | every tool window |

`Fonts[0]` may not be swapped: world-space labels blit glyphs out of it, and
changing the face would change the rendered world image.

## Tooltips in the game

The look-target seam is unchanged in shape -- `GameplayTarget` in, a value-only
`InteractionFocus` out, no pointers into scratch buffers -- and gained one
field, `catalogIndex`, so presentation can resolve a description without the
targeting code learning any strings.

```
LiveLevel::appendTargets ─┐
InteractionSystem::pushTarget ─┴─> aimedTarget() ─> InteractionFocus
                                                        │
                        DungeonMap::propInfo(catalogIndex)
                        combat registry (Health)         │
                                                         v
                                    game::buildTooltip(...) -> TooltipContent
                                                         │
                                        GameHud -> TooltipView::draw(canvas)
```

`buildTooltip` is a pure function of values (`game/src/ui/TooltipBuilder.cpp`,
tested by `game/tests/TooltipBuilderTests.cpp`). `TooltipContent` is engine
data: title, subtitle, wrapped body lines, bars, a semantic accent tone and an
optional verb + key cap. The engine never learns what a torch is.

### Styles and responsive layout

`UiStyleSheet` is deliberately CSS-like without adding a browser layout tree.
It centralises palette variables, spacing scale and component chrome.
`UiTone` carries meaning (`Focus`, `Danger`, `Mystic`, etc.) from content to
paint; raw packed colours stay inside the resolved palette.

`UiLayout::layoutFlex` is pure, allocation-free integer layout over caller-owned
spans. HUD bottom plates use one end-aligned row, timed statuses use a wrapped
row, and all results stay on the same integer virtual-pixel grid as drawing.
Compact mode shortens status labels and collapses the weapon plate to one row.

`GameHudStyle.cpp` defines the game-owned **Ash Reliquary** theme: black iron
fills, bone text, brass focus, blood danger, moss recovery and ritual violet.
Panels share broken-corner frames, inset highlights and semantic accent rails.
Tooltips prefer the crosshair's lower-right side, flip around it before
overflowing, and remain above the bottom HUD cluster. Target banners occupy the
top safe rail. Accessibility variants resolve from immutable defaults on every
configuration change, preventing cumulative alpha or sticky contrast settings.

### Making a prop describe itself

Add the fields to its `[[prop]]` entry in `game/assets/dungeon_props.toml`:

```toml
[[prop]]
id = "loot_chest"
marker = "H"
display_name = "Iron-Bound Chest"
category = "Container"
description = "Banded oak, the lock long since rusted through."
rarity = "rare"          # common | uncommon | rare | arcane -> accent colour
interact = "PRISE OPEN"  # omit for an inert prop: no key prompt is drawn
```

A prop **with** `display_name` becomes a look target. A prop without one stays
scenery and is never emitted into the target list, which is how ambient
dressing stays out of the crosshair's way. No C++ change is involved.

### Making something else targetable

Publish a `GameplayTarget` each frame through
`InteractionSystem::pushTarget`; it arbitrates against level targets in the
same aim test rather than running a second one. `main.cpp` does this for the
training dummy. Targets are consumed per frame, so a system that stops
publishing simply stops being targetable.

## Tooltips in the tools

```cpp
ImGui::Button("60 Hz");
eng::imguihint::hover("debug.step_rate", "fallback literal");

eng::imguihint::marker("editor.staging_mode");     // the "(?)" marker + hover
eng::imguihint::showText(title, dynamicBody);      // inherently dynamic text
```

Hint text lives in `engine/assets/ui/hints.toml` and is loaded once by
`RenderCore` after the imgui context exists:

```toml
[[hint]]
id = "editor.staging_mode"
title = "Staging mode"
body = "Isolates one material on one object..."
```

An unknown id falls back to the literal at the call site, so migrating a
hand-rolled `SetTooltip` is a one-line change that cannot regress.

## Where things live

| path | what |
| --- | --- |
| `engine/include/eng/ui/`, `engine/src/ui/` | BitmapFont, UiCanvas, UiLayout, TooltipView |
| `engine/src/render/ImGuiHint.cpp` | tool hover help |
| `engine/assets/fonts/` | `ui_regular.{png,toml}`, `DejaVuSansMono.ttf`, `DejaVu-LICENSE` |
| `engine/assets/ui/hints.toml` | tool hint text |
| `game/src/ui/GameHud.{h,cpp}` | player HUD layout |
| `game/src/ui/GameHudStyle.{h,cpp}` | game theme and responsive policy |
| `game/src/ui/TooltipBuilder.{h,cpp}` | focus -> TooltipContent |
| `game/src/PropInfo.h` | prop presentation metadata |
| `tools/gen_font_atlas.py` | atlas generator |

Fonts are DejaVu (Bitstream Vera licence, see `engine/assets/fonts/DejaVu-LICENSE`).
No proprietary game font or artwork is vendored.
