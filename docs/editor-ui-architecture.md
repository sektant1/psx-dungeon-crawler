# Editor UI Architecture

The scene editor's shell is **Godot's**, deliberately and in full: menus at the
top left, a main-screen switcher centred, play controls at the top right, open
scenes as tabs beneath them, three docked columns, and a bottom panel that stays
a strip of buttons until you click one.

The reason to copy an arrangement rather than invent one is that an author
already knows it. Nothing below is Godot's *code* — every panel is this tree's
own, and the things it edits (kit pieces, the scene contract, layers, the
material stage) have no Godot equivalent.

## Workspace Map

```text
+-- Scene Edit Debug Arrange Editor View Help --- [3D][2D][Material] --- [Play][Cook] --+
| level.scn x | menu.scn x | +                                                          |
+------------------+--------------------------------------------------+----------------+
| SCENE (tree)     | [tools] Transform Snap View  [3D Top Front Side]  |   INSPECTOR    |
| Layers           |                                                   |   Contract     |
|                  |                                                   |   History      |
|------------------|                 3D  /  2D  /  Material            |                |
| FILESYSTEM       |                                                   |                |
| Placeables       |                                                   |                |
| Materials        |                                                   |                |
| Effects          |                                                   |                |
+------------------+---------------------------------------------------+---------------+
|                       BOTTOM PANEL (one of Output / Problems / Timeline)              |
+---------------------------------------------------------------------------------------+
| [Output][Problems 2][Timeline]     what just happened      level.scn | Level | x y z   |
+---------------------------------------------------------------------------------------+
```

Panel names are Godot's where Godot has one — the hierarchy is **Scene**, the
asset browser is **FileSystem** — while the `###id` behind each name is
unchanged, so no saved layout was invalidated by the rename.

| Surface | Owns | Does not own |
|---|---|---|
| Top bar | Menus, the main screen, launching | Anything about one scene |
| Scene tabs | Which documents are open | What is in them |
| Viewport toolbar | Tool, transform space, snap, work plane, view options, projection | Document edits |
| Scene | Selection, grouping, visibility, locking, parenting | Component properties |
| Layers | Layer identity, membership, per-layer visibility and lock | Which entities exist |
| FileSystem | Placeable kit, gameplay entities, meshes, materials, effects | Scene hierarchy |
| Inspector | Selected entity components | Asset browsing, scene properties |
| Contract | Scene kind, roles, quick fixes, palette | Per-entity anything |
| History | The undo stack, and walking it | Making edits |
| Bottom panel | Output, Problems, Timeline | Persistent workspace status |
| Status row | Bottom-panel buttons, scene, kind, activity, selection | Long-form diagnostics |

## The contract is the workflow

`docs/scenes.md` defines what a scene *is*: a set of **roles**, and which of
them are filled decides the scene's **kind**. That was previously a report
buried at the top of a Problems tab. It is now the spine the editor is built
around:

- **New scene** asks for the kind first. Its previous form produced a document
  with no view and no spawn — the exact failure the vocabulary exists to make
  loud.
- **Opening a scene picks the main screen** from its kind. A `Screen` scene
  opens in the 2D editor; everything else opens in 3D. Opening a flat page in
  the 3D view showed it edge-on, and the fix was something you had to know.
- **Play refuses** a scene the contract calls unplayable, names the reason, and
  raises the Contract dock. It used to launch a black window.
- **Every unfilled role carries its own fix button.** The `QuickFix` was always
  in the data and only ever reached the Problems list, so the one panel that
  named a hole could not close it.
- The **status row** carries the kind, so "what is this scene" is answered
  without opening anything.

## Open scenes are tabs

`editor/app/SceneTabs.h`. A tab owns everything that is *about one scene*: the
document, its path, its dirty flag, its undo history, its selection, its camera,
and what the author hid or locked in it. Everything else in `EditorState` — the
tool, the grid, the brush — is about the *author*, stays shared, and
deliberately does not switch.

`EditorState` still mirrors the active tab, which is why no panel had to learn
that tabs exist. `captureActiveTab()` and `activateTab()` are the entire
mechanism.

The payoff is that **opening a scene is no longer destructive**. It went through
the same save/discard/cancel prompt as quitting, because it threw the open
document away; an author comparing two rooms had to choose which one they were
allowed to have. Quitting now asks about every dirty tab, not just the visible
one.

Undo is per tab and not shared: commands address entities by `AuthorId`, so a
single stack across documents fails *silently* rather than loudly.

## The bottom panel is not a dock node

Output, Problems and the Timeline are read *against* the viewport — you scrub a
clip and watch the door move — so they want full width and a height set once,
which is exactly what a dock node's tab bar and drag handles keep taking away.
They live in a region between the dockspace and the status row, opened by the
buttons at the bottom left, and cannot be dragged somewhere they make no sense.

`eng::DebugConsole::drawBody()` and `eng::ClipPanel::drawBody()` exist for this
host: a region has no window for `Begin()` to attach to.

## Nothing overflows

Dear ImGui clips whatever runs past a window's right edge and, by default, gives
no way to reach it. Two rules, in order:

1. **Content that can wrap, wraps.** `ed::ui::sameLineIfItFits()` continues a row
   only when the next run fits; the decision itself (`rowHasRoom`) is pure and
   tested. The viewport toolbar uses it throughout, because a viewport is an
   image and cannot scroll.
2. **Everything else gets a scrollbar.** `kPanelFlags` carries
   `ImGuiWindowFlags_HorizontalScrollbar`, so a panel docked narrow can always
   be scrolled to its content.

The Contract panel is the worked example of preferring layout over both: it was
a four-column table, and at the Inspector rail's width its detail column
rendered sentences one character per line. Nothing about a role is tabular, so
each is now a block that reflows at any width.

## The scene tree's two kinds of row

This is the panel's canonical meaning, and it used to be only implicit.

- A **composed** row is an object: the rows under it are its parts, and clicking
  it selects one thing.
- A **bucket** row is a pile: a hundred and forty-six unrelated walls that share
  a prefab, and clicking it selects a hundred and forty-six things.

They looked identical, and the second is destructive to mistake for the first.
A bucket now wears `Icon::Stack` rather than `Icon::Group`, and says what it is
on hover.

A bucket is a *view* device — the document has no such node — which is why the
panel can switch it off entirely:

- **Grouped** (the default) collapses repeats, which is the only way a blockout
  of hundreds of identical walls stays readable.
- **Hierarchy** is the document's own structure: one row per entity, children
  under their parent, nothing merged and nothing invented. Until it existed, the
  parent/child structure of a mostly-flat scene was not visible anywhere in the
  editor.

## Viewport marks: two budgets

`editor/viewport/EntityGizmos.h`. The original rules were each individually
right — "only the selected light shows its reach", "only attended marks are
labelled" — and assumed one attended mark at a time. Selecting a room's eleven
lights drew eleven overlapping range spheres and twenty-two lines of text, and
the level underneath was gone.

- `volumeBudget` (3): full volumes for at most this many attended marks, in
  attention order — hovered, then the Inspector's primary, then the rest of the
  selection. Above it a mark keeps its body and highlight and loses only the
  part that overlaps.
- `labelBudget` (20), plus `LabelPacker`, which **skips a label whose rectangle
  would overlap one already drawn**. The cap alone still lets twenty labels
  stack on one point.

The kind is spelled out only for the mark under the cursor; the body's shape and
colour say it for the rest. The Inspector's primary wears a ring, so "which of
these eleven is the panel about" has an answer on screen.

## Responsive Policy

`makeWorkspacePlan()` computes pixel targets, then converts them to dock split
ratios once when a clean workspace is built.

| Window | Left column | Right column | Centre width |
|---|---:|---:|---:|
| 1280 x 720 | 248 px | 296 px | 736 px |
| 1600 x 950 | 304 px | 360 px | 936 px |
| 2560 x 1080 | 320 px | 440 px | 1800 px |

Rules:

- Side rails stop growing on ultrawide displays; extra width belongs to centre.
- At large interface scale, rails compress proportionally before centre width.
- The left column is **split**, not tabbed: the tree and the asset list are each
  read top-to-bottom, so tabbing them wasted the rail. `sceneTreeFraction` tips
  toward the tree on tall windows and away on short ones, because the file
  browser's preview and metadata block above its list do not shrink.
- The bottom panel is never more than half the workspace, and never smaller than
  about six rows of text.
- User rearrangements persist. `Editor > Reset workspace` rebuilds defaults.
- The dockspace id carries a schema version (`v7`); an incompatible saved layout
  cannot override new topology.

## Theme

`godot_dark`, registered in `eng::imguitheme` and the editor's default. The
values are Godot 4's own construction rather than an eyeballed approximation:

```text
base   = interface/theme/base_color   = #333B4F
accent = interface/theme/accent_color = #699CE8
dark_N = base lerped toward black by contrast(0.3) * {1, 1.5, 2}
font   = white lerped toward base by 0.25
```

Two departures. Rounding is 3px — Godot's `corner_radius` default, which
contradicts the zero-rounding rule `raven_editor` was built on; keeping hard
edges here would have read as a near-miss rather than a deliberate borrowing.
And the status colours (warning, danger) stay this tree's, because the panels
using them name them by meaning.

`raven_editor` is still registered and selectable in `Editor > Editor
settings > Theme`, and remains the *engine* default, so the game's debug UI is
unaffected.

## Ownership

- `editor/ui/EditorShell` owns the top bar's zone arithmetic and the bottom
  panel's height rule. Pure; tested without a context.
- `editor/ui/EditorWorkspace` owns dock topology and responsive sizing.
- `editor/app/SceneTabs` owns which documents are open.
- `EditorApp` owns screen flow and panel content.
- `eng::imguitheme` owns global tool styling.
- Dear ImGui INI owns user panel arrangement only.
- `EditorSettings` owns persistent behaviour preferences, including the theme.

Window labels use `Visible Name###StableId`. Copy changes therefore do not reset
docking identity. Workspace code runs before dockspace submission, matching Dear
ImGui docking guidance.
