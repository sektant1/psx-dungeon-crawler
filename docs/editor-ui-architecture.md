# Editor UI Architecture

Editor workspace follows
[`Engine_Editor_Visual_Main_Reference.png`](references/Engine_Editor_Visual_Main_Reference.png)
without copying its engine-specific controls. Layout keeps browsing, hierarchy,
scene authoring, properties, and diagnostics visible as separate jobs.

## Workspace Map

```text
+----------------------+---------------- COMMAND BAR ----------------+------------------+
|                      |                                               |                  |
| ASSET BROWSER        |                                               |                  |
| Placeables / Meshes  |                SCENE VIEW                     |    INSPECTOR     |
| Materials / Effects  |              HUD Preview tab                  |                  |
|----------------------|                                               |                  |
|                      |                                               |                  |
| HIERARCHY            |-----------------------------------------------|                  |
|                      |          Problems / Console                   |                  |
+----------------------+-----------------------------------------------+------------------+
| RAVEN // EDIT | scene + dirty | last activity | selection / camera                     |
+-----------------------------------------------------------------------------------------+
```

Panel responsibilities:

| Surface | Owns | Does not own |
|---|---|---|
| Command Bar | Authoring mode, transform, grid step, snap, work plane | View options |
| Scene View strip | Lighting, marks, volumes, frame stats, framing, walk view | Document edits |
| Asset Browser | Placeable kit, gameplay entities, the mesh tree, material and particle libraries | Scene hierarchy |
| Hierarchy | Selection, grouping, visibility, locking | Component properties |
| Inspector | Selected entity components or scene properties | Asset browsing |
| Problems | Validation and quick fixes | General log output |
| Console | Logs and commands | Persistent workspace status |
| Status strip | Scene/dirty state, last action, selection context | Long-form diagnostics |
| Settings | Autosave and interface scale | Duplicate live viewport/play controls |

Asset labels and stable identities follow
[Asset And Entity Naming](asset-naming.md). Browser labels may be friendly;
lookups and persistence always use full stable IDs.

`Settings` and `Keyboard Shortcuts` are transient, non-dockable windows. Open,
import, save, confirmation, and command-palette surfaces remain modal or overlay
flows rather than workspace panels.

## Two shared layouts

Two arrangements are described once, in `editor/include/editor/ui/EditorUi.h`,
and every panel is built out of them. Dear ImGui's defaults are built for a
debug overlay -- a label to the *right* of its widget, each window free-form --
and that is exactly wrong for a tool somebody works in for an hour.

### `ed::ui::PropertyGrid`

A two-column table: names in a fixed left column, widgets filling the rest.

```cpp
ed::ui::PropertyGrid grid("##camera");
grid.row("fov", "deg");   ImGui::DragFloat("##fov", &camera.fovDegrees, ...);
grid.row("near clip", "m"); ImGui::DragFloat("##near", &camera.nearClip, ...);
grid.full("far must be beyond near, or nothing draws");
```

`row()` opens the widget cell and sets the item width to fill it, so a caller
supplies only the widget -- with a `##` label, because the name is already on
screen. `full()` is a note spanning the value column. The label column is a
*share* of the panel rather than a constant: an inspector docked narrow has to
give the widget room, and one dragged wide should not leave a metre of space
between a name and its field.

Every component drawer in `ComponentInspector.cpp` uses it, including the
reflection-driven Portal drawer -- a generic drawer that looked different from
the hand-written ones would announce that it is generated, which is nobody's
business.

### `ed::ui::drawAssetPanel`

The shape shared by all four Asset Browser tabs, in this order:

```text
[ preview ]  name                 <- the swatch, and what the subject IS
             class | size | ...
             [ Use as Brush ] [ Apply to Selection ]
-------------------------------------------------
 [x] spin   [ ] hide kit meshes   <- how the list is shown
-------------------------------------------------
 Search...                        <- one filter, one rule (ed::ui::filterMatches)
-------------------------------------------------
 - group                          <- the list, scrolling, ending at the footer
   row
   row
 180 of 180 meshes                <- the footer
```

The order is the argument. An author's question in all four tabs is the same --
"what is this, and do I want it" -- so the answer belongs in the same place on
screen in all four. Before this, Materials led with the swatch, Effects led with
the list, and Placeables had no preview at all, so moving between tabs meant
re-finding every control.

Hover previews, click selects. That split is what makes scrubbing a list of two
hundred filenames work, and all four tabs now obey it.

All four share **one** offscreen swatch (`eng::MaterialPreview`'s thumbnail
target). It shows a lit sphere for a material, an animated quad for a procedural
one, a live particle system for an effect, and -- new -- an arbitrary mesh,
framed by its own bounds so a four-metre wall and a twenty-centimetre candle
both fill the square. Whichever tab is drawing owns the swatch and puts the
other subjects away; a rig patched half-way is how this preview has previously
leaked geometry into the level.

## Responsive Policy

`makeWorkspacePlan()` computes pixel targets, then converts them to dock split
ratios once when a clean workspace is built.

| Window | Browse rail | Inspector | Scene width | Diagnostics |
|---|---:|---:|---:|---:|
| 1280 x 720 | 248 px | 296 px | 736 px | 132 px |
| 1600 x 950 | 304 px | 360 px | 936 px | about 162 px |
| 2560 x 1080 | 320 px | 440 px | 1800 px | 192 px |

Rules:

- Side rails stop growing on ultrawide displays; extra width belongs to scene.
- At large interface scale, rails compress proportionally before scene width.
- Asset Browser and Hierarchy remain simultaneously visible.
- Command Bar and diagnostics span scene workspace only, not side rails.
- Status strip always remains visible below dockspace.
- User rearrangements persist. `Window > Reset workspace` rebuilds defaults.
- Dockspace ID carries schema version; incompatible old layouts cannot override
  new topology.

## Raven Chrome

Palette comes from Raven logo/avatar references: layered gunmetal, cold chrome,
black void, and one red optic.

| Token | Hex | Role |
|---|---|---|
| Void | `#090B0E` | Deep background |
| Canvas | `#0D1014` | Menu and viewport surround |
| Panel | `#14191F` | Window background |
| Raised | `#1C2229` | Titles and selected tabs |
| Control | `#20272F` | Inputs and idle buttons |
| Hover | `#2B343E` | Neutral hover |
| Border | `#3E4853` | Hairline divisions |
| Border strong | `#64707C` | Active separator |
| Text | `#D9DEE3` | Primary content |
| Text muted | `#98A1AA` | Secondary labels |
| Raven red | `#C63A40` | Active/selected/check state |
| Focus | `#F06A70` | Keyboard/gamepad focus |
| Warning | `#C79A50` | Warning state |
| Danger | `#E36A55` | Error/destructive state |

All rounding is zero. Red is reserved for selected, active, focused, or
destructive state; normal hover stays grey. State also carries icon, text, or
shape, never colour alone. Tool text remains readable monospace. Pixel identity
comes from hard edges, drawn icons, compact rhythm, and integer-aligned marks,
not a low-legibility pixel font.

## Ownership

- `editor/ui/EditorWorkspace` owns topology and responsive sizing.
- `EditorApp` owns screen flow and panel content.
- `eng::imguitheme` owns global tool styling.
- Dear ImGui INI owns user panel arrangement only.
- `EditorSettings` owns persistent behavior preferences only.

Window labels use `Visible Name###StableId`. Copy changes therefore do not reset
docking identity. Workspace code runs before dockspace submission, matching Dear
ImGui docking guidance.
