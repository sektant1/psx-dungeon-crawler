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
| Materials / Particles|                SCENE VIEW                     |    INSPECTOR     |
|                      |              HUD Preview tab                  |                  |
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
| Asset Browser | Placeable kit, gameplay entities, material and particle libraries | Scene hierarchy |
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
