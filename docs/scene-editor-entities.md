# Scene editor: entities and components

The editor's left panel is a scene tree, the right panel is a component stack.
An entity is an id and a transform; everything else about it -- a mesh, a light,
a trigger, an enemy spawn -- is a component you add and remove. Nothing in the
editor asks what *kind* an entity is before letting you edit it.

That is not a new model bolted onto the editor: it is what the format and the
cooker already do. `SceneWriter` writes each field it finds independently, and
`SceneCook` turns each one into its own runtime component, so a wall that is
also a trigger has always been representable. The panels used to hide it behind
fixed sections and a New menu.

```
Outliner (left)                Inspector (right)
  enemy   Sentry                 id / name / transform     <- every entity has these
  light   light (9)   <-.        Mesh          [x]
  wall    kit.wall (54) |        Collider      [x]         <- one section per component
    wall_0001           |        Light         [x]
    wall_0002        the repeats   [ Add Component ]       <- everything it is missing
    ...              folded away
```

## The tree

`buildOutliner` (`editor/include/editor/scene/OutlinerTree.h`) groups the document:

- entities with a prefab group by **prefab id** -- forty `kit.wall` pieces that
  differ only by id become one row with a count;
- entities without one group by **kind** (`light`, `marker`, `enemy`, ...);
- a group with one member draws as a leaf, so the tree does not make you expand
  a row to find a single spawn;
- gameplay groups sort above geometry, each alphabetically, and members sort by
  id -- placing a wall never reshuffles the panel under the cursor.

Clicking a group row selects **everything in it**. That is the point of the
grouping: "give all fifty-four walls a collider" is then one click, one menu
item, and one undo entry. Clicking a leaf selects the single entity it stands
for -- for a spawn or a trigger, that row is the only handle it has, since it
has no mesh to click in the viewport.

### Composed objects

An entity may name a `parent`. Its transform is then **local to that parent**,
and it moves, turns and scales with it. A chandelier and its four candles, a
wall and the torch on it, a patrol route and the room it guards: one thing to
select, one thing to drag, one thing to delete.

Composed objects are lifted out of the prefab grouping and drawn as their own
tree, root row first, children nested under it. Sharing a prefab with something
that is part of an object does not move a loose entity out of its group -- only
being in one does. The filter matches an object whole: half a chandelier is not
a shorter list, it is a broken one.

To compose: drag a row onto another row in the outliner, or select several and
use **Parent to first selected**. To break one apart: drag a row into the empty
space below the tree, or **Detach from parent**. Either way the entity keeps the
place it was drawn in -- `localFromWorld` re-expresses the transform against the
new frame, so nothing teleports.

Two things the editor refuses rather than reports: a cycle (dragging a parent
onto its own descendant), because a looping tree is one you cannot use to fix
the loop; and keeping a `cell` on a parented piece, because the cooker reads the
cell against the *grid*, not against a parent, and the two would disagree about
where the piece is.

The runtime never sees any of this. `SceneCook` resolves each entity's chain
once and writes a world transform, so a `.map` is the flat list it always was --
which is why `cook_parity` still passes on every scene in the repo.

Deleting, duplicating, copying and cutting all take descendants with them
(`withDescendants`), and a duplicated object's children re-point at the
*duplicated* root rather than the original -- otherwise moving the copy would
leave its candles behind on the first chandelier.

The rows live in `editor/src/ui/OutlinerPanel.cpp`, apart from the app, so
`editor_outliner_panel_tests` can drive ImGui headless and actually press them.
That test exists because of a real bug: the panel queried `IsItemClicked()`
*after* drawing the kind tag beside the row, and those queries answer for the
last item submitted -- the tag, which is not interactive. Group and leaf rows
silently stopped selecting anything, and the compiler had nothing to say. Every
row query is now sampled the moment the row's own item is submitted.

`show geometry` folds away kit pieces that carry no gameplay component. A wall
with a trigger on it stays visible -- it is the entity you are hunting for, and
it happens to have a mesh.

The tree is rebuilt when the document revision or the panel's options change,
not per frame: it walks and sorts every entity, and the panel is open while the
gizmo is being dragged.

## Placing: the brush

The Place tool holds a **brush** (`ed::Brush`, `editor/include/editor/scene/EntityComponents.h`)
-- either a kit piece or a gameplay kind, plus the quarter turn it will land at.

That it can be either is the point. A light and a wall are the same gesture to
an author: point at the level, click. They used to be two different ones,
because a light has no mesh to name -- kit pieces painted under the cursor while
lights, spawns and triggers appeared at the *centre of the screen* from a button
and had to be dragged into place. Selecting anything in the Catalog now arms the
brush.

The directional light is the one exception, and stays a button: it is a
scene-wide key light that is aimed rather than positioned, so painting a row of
them down a corridor is not a thing anyone means to do.

| Input | Effect |
|---|---|
| wheel (or `,` / `.`) | rotate the brush a quarter turn |
| drag | paint -- one piece per slot crossed |
| Shift+drag | erase what the cursor crosses |
| Alt+click | eyedropper -- adopt the piece under the cursor, rotation included |
| Ctrl (held) | ignore geometry, place on the work plane |
| Escape (during a Room drag) | cancel the rectangle |

Rotation is the brush's own state. It was previously a field on the app that
three call sites read and *nothing* ever wrote: the Catalog displayed `rot 0
deg` forever, and the only way to turn a piece was to place it, select it and
use the gizmo.

## Height comes from what you point at

Placement resolves through `resolvePlacement`
(`editor/include/editor/scene/BrushPlacement.h`), and the work plane is no longer the only
source of height -- it is the fallback for when the cursor is over nothing.

```
mouse -> mouseRay()
      |- Ctrl held? ---------------------> work plane            (override)
      `- raycastDocument()
            |- hit  -> surface point + face normal
            `- miss -> work plane                                (as before)
      -> snap XZ to the grid (or nearestWallSlot for Wall/Opening)
      -> apply the brush's quarter turn
```

Two rules, because the two cases genuinely differ:

- **architecture** (`Floor`, `Wall`, `Fill`, `Opening`) stacks on the **top** of
  whatever was pointed at, so pointing anywhere on a wall's face still lands the
  next wall squarely on top of it rather than partway down the side;
- **dressing** (`Prop`, and the meshless gameplay entities) lands **exactly
  where the cursor touched**, so a torch goes where you point it on a wall.

XZ still comes from the grid in both cases: the surface decides height, not
footprint, which keeps every kit piece on cells the runtime can represent. Y is
never snapped to the grid step -- the height was taken from a surface precisely
so the thing would rest on it, and rounding that is how a barrel ends up
hovering over the table it was dropped on.

The ghost is **green on the work plane and amber on a surface**, with a stalk
down to the plane when it is off it. The two land in different places and used
to be indistinguishable until after the click.

`raycastDocument` (`editor/include/editor/scene/DocumentRaycast.h`) is the same traversal
viewport picking uses, so a click and the thing a ghost would rest on can never
disagree. A running stroke excludes its own pieces, so a row of floor tiles
painted in one drag all land on the same surface instead of climbing the one
before it.

## Strokes and slots

The Place tool paints. It drops a piece **every frame the button is held**, so
dragging across a room fills it, and a whole drag closes as one undo entry.
Shift makes the stroke subtract instead; erasing keeps the same shape, one
target per slot, applied live and closed as a single undo entry.

What keeps that from stacking a pile in one spot is the slot
(`editor/include/editor/scene/PaintSlot.h`): a stroke remembers the slots it has filled and
skips them. A grid piece's slot is its cell, edge and work plane. A free prop
has no cell, so its slot is its **position, quantized** -- to the grid step when
snapping is on, to `kFreePaintSpacing` (0.5 m) when it is off.

That quantization is the fix for props landing in a pile: the slot used to be a
counter, which is unique every frame, so the frame rate decided how many props a
click produced. Separate clicks are separate strokes, so clicking twice in one
spot still gives two props.

### The ghost always has a place to be

The piece under the cursor is drawn wherever the cursor points, always. The
work-plane point comes from `workPlanePoint` (`editor/include/editor/scene/Picker.h`), which
falls back to a point out along the ray when the ray misses the plane -- which
is most of the time once the camera looks anywhere near the horizon. A ghost
that blinks out whenever the view tips up is the most confusing thing a
placement tool can do.

### The grid follows the camera up

`gridViewFor` (`editor/include/editor/viewport/ViewportGrid.h`) coarsens the drawn work plane with
the camera's height above it, always by a power of two, so every line stays a
line a piece could snap to. Framed on a whole dungeon two hundred metres up, the
old fixed patch of sixteen cells was a postage stamp floating under the view.
The toolbar says `(drawn x16)` when the spacing is not the level's own cell.

## Getting around

The editor is meant to be the whole authoring surface, so nothing it can do is
reachable only by knowing where it lives.

**Ctrl+P** opens the command palette: every verb the editor has, by name, plus
every kit piece as `Place kit.<id>`. Matching is subsequence-based, so `cksc`
finds "Cook scene"; the arrows move the highlight and Enter runs it. Adding an
action to `EditorApp::paletteActions` makes it findable with no menu to place it
in and no key left to bind.

**F1** lists every keybind in one table. The bindings themselves are
`assets/config/editor.toml`; the panel is the reading of them. Letter keys are
mute while a text field has focus and while the camera is flying, which is why
`W` can be both "forward" and "place tool".

**Ctrl+O** opens a scene, with the recently opened listed above the directory.
The console's `open` does the same thing and completes on the same list. Opening
always goes through the unsaved-work prompt.

**Autosave** writes `<scene>.autosave.scn` beside the scene while the document
is dirty -- and `untitled.autosave.scn` for a scene that has no file yet, which
is the work that was previously possible to lose outright.
`Scene > Recover autosave` lights up only when a backup is newer than the scene
it belongs to; recovery opens it like any other scene, so the backup is not
consumed until you `Save as` over it.

## Settings

`Edit > Settings...` (or the palette: "settings", "autosave"). Three groups:
what protects the work, what the editor draws with, and what a playtest starts
with.

### Autosave

The group the editor cannot be trusted without.

| | |
|---|---|
| **Back up automatically** | off is a real choice -- a scene on a network share, a machine where the write stutters -- and it should be visible rather than a constant in C++ |
| **every _n_ min** | 15 s to 15 min. The clock only counts while there is unsaved work, and it re-arms on save |
| **Back up now** / **Recover autosave** | the two verbs the window is about, next to the schedule that drives them |

The panel also states what the schedule is *currently* doing -- "next backup in
1:23", "nothing unsaved", or a warning when it is off -- because a backup
schedule you cannot see is one you have to take on faith.

### Viewport

The same toggles as the View menu -- render profile, game lighting, entity
marks, volumes, frame stats -- with one difference: they are **remembered**.
Every launch used to start with the profile, the lighting and the marks back at
their defaults, so an author who works under one profile re-picked it every
morning. Changing one from the menu persists it too; the settings file is
written whenever the live state and the stored state disagree.

The render profile is first in the group because a per-entity `ShaderParams` --
a rim light, a tint, an opacity -- is only meaningful *under a profile*. The
same rim strength reads as a hot outline under `ps1` and as nothing under
`dungeon`.

### Playtest

What F5 launches the game with. Every row is an environment variable the game
already reads, so the editor is choosing between the game's own options rather
than growing a second set of them:

| | Sets |
|---|---|
| **Play under the viewport's look** | `RAVEN_RENDER_PRESET` = whatever the viewport is on |
| ...or an explicit profile | `RAVEN_RENDER_PRESET=<name>` |
| **Start where the camera is** | `RAVEN_PLAY_FROM=x,y,z` |
| **Open the debug console** | `RAVEN_DEBUG_UI` |
| **Show colliders** | `RAVEN_SHOW_COLLIDERS` |
| **Fullscreen** | `RAVEN_FULLSCREEN` |

Linking the look to the viewport is the default, and it is the row that makes
the other two panels honest: shader params tuned in the editor and played under
a different profile are tuned blind. Unticking it is for the deliberate
comparison -- author under `dungeon`, check under `ps1`.

The bottom line of the group prints the launch as it will happen
(`game <scene>.map  RAVEN_PLAY_FROM=<camera>  RAVEN_RENDER_PRESET=dungeon`). A
launcher whose switches are invisible is one you end up debugging by reading
its source.

A flag the game tests by *presence* is exported as `1` or omitted, never set to
`0`: `RAVEN_DEBUG_UI=0` would turn the console on. Any variable the launch sets is
also removed from the inherited environment rather than duplicated -- a stale
one silently winning is a setting that appears to do nothing.

Written to `artifacts/editor/settings.txt` on every change, not on close: the
setting most worth having is the one that protects work from a crash, and it has
to survive the crash that proves it was needed. Per-user state, beside
`recent.txt`, for the same reason -- the key bindings and window size in
`config/editor.toml` are project content and stay in version control.

Plain `key = value`. An unknown key is skipped rather than diagnosed, so a file
written by a newer editor opens in an older one, and every value is clamped on
load: a hand-edited `interval_seconds = 0` would otherwise write the whole
document every frame.

Above the viewport sits everything that changes what you *see* -- entity marks,
volumes, frame stats, game lighting, the player's eye, framing, play and cook.
The Toolbar panel keeps what changes the *document*: tools, grid, work plane and
gizmo. One control per piece of state; the copy you are not looking at is the
one that looks stale.

`View > Render preset` switches the shipped look while authoring. A room that
reads under the editor's flat key light and disappears under the dungeon profile
is a room that has not been checked, and the check used to mean cooking and
launching the game.

## Seeing what has no mesh

Half of a level is entities with nothing to draw: spawns, exits, markers, enemy
spawns, pickups, triggers, lights. They were in the outliner and could be
clicked in the viewport *if you guessed where they stood*, which meant the only
reliable way to verify a spawn was to cook the map and run the game.

`EntityGizmos` draws a screen-space mark for each -- a diamond, an arrow for the
ones where facing is part of the authoring (spawn, exit, sun), a wire box for a
trigger or collider volume, and a ground ring for a light's range. The marks are
drawn over the viewport image rather than as world geometry, on purpose: the
rendered image is a shipped result and no editor affordance may change what the
renderer produces.

They are also the **hit target**. An entity with no mesh has a one-metre box
somewhere inside a room, so hitting it with a ray is luck; its icon is exactly
where the eye already is, and `pickGizmoMark` gets first refusal on every click.
A collider outline never takes the click -- it is drawn around something that
has its own bounds, and stealing the click would make that thing unselectable.

Labels and light rings appear only for what is selected or under the cursor.
Thirty marks in a room is thirty overlapping words and eight overlapping
circles, and the result is that none of them can be read -- including the one
being looked for.

## Reading the list

Rows carry an **icon** for their kind rather than a padded word. The kind used
to be seven characters before every name, which at the width this panel docks to
was a quarter of the row spent saying something a shape says instantly.

The icons are drawn with `ImDrawList` (`editor/include/editor/ui/EditorIcons.h`), not typed
from a glyph font: a dozen shapes made of circles and polylines stay sharp at
any size, tint per row, and add no asset the pipeline has to know about.

Every row has an **eye** and a **padlock**, right-aligned:

- **hidden** is not drawn and not pickable -- how a ceiling gets out of the way;
- **locked** is drawn and not pickable -- how a room gets dressed without
  catching the floor on every click.

A **group** row carries the pair too, acting on everything under it: a hundred
and sixty doors used to be hideable only one at a time, after opening the group,
which is the same amount of work as not having the switch.

Both take descendants with them, and both are **session state**, not document
state: they live in `EditorState`, never in the `.scn`, because "I am working on
this corner" is not something two authors share through a file.

A row is three columns: **name**, then the group's **count** or an entity's
**component count**, then the switches. The name clips (with the full text on
hover) rather than running under the icons -- it used to draw through the
padlock, so the row read as a name with a smudge on the end and the switch it
hid could not be aimed at. The count sits in its own column so every count in
the panel lines up, and because a name is recognisable from its first eight
characters while `(146)` is not recoverable from `(146`.

The component count only appears from two components up: one component is the
thing the row's icon already says it is, and a badge on every row is a column of
noise. Hovering it lists them. What actually distinguishes two identical pillars
is that one of them has the trigger.

The header carries the same four as buttons: fold kit geometry away, show
everything hidden, unlock everything, and **isolate** -- hide everything the
selection does not need, which is the move a dressed room needs constantly and
which no list panel has until somebody adds it.

### Selecting

The modifiers every other list uses, because the muscle memory arrives with the
author:

| | |
|---|---|
| click | replace the selection, and set the anchor |
| **Ctrl**+click | toggle one row |
| **Shift**+click | take every row between the anchor and here |

Shift used to mean toggle and there was no range at all, so selecting a run of
thirty rows was thirty clicks -- and every other application's habits selected
the wrong thing on the way. A second Shift-click re-ranges from the same anchor
rather than unioning, which is the only way to shrink a range that overshot.

The range is resolved against the rows **as drawn**, so it never reaches into a
collapsed group. That order is captured each frame and handed back the next one
(`OutlinerRowOrder`): a row's position is only known once it is drawn, by which
time the row that starts the range has been submitted and the one that ends it
has not.

### Following the world

Selecting in the viewport scrolls the panel to that row and opens whatever was
collapsed above it. Picking something and then hunting its row in a
three-hundred-row list is the single most common thing an editor makes people do
twice; `EditorApp::selectAndReveal` is the one path every non-panel selection
goes through -- viewport pick, placement, a jump from the Issues list,
`RAVEN_EDITOR_SELECT`.

### Searching

Free text matches name, id, kind and prefab. Two prefixes narrow instead:

```
has:collider     entities carrying that component
kind:enemy       entities of exactly that kind
has:trigger kind:wall     terms AND together
```

`kind:` is exact rather than a substring: if it matched substrings it would be
no narrower than the free text, which is the whole reason to type a prefix.
Free text answers "where did I put stone_arch"; `has:` answers "which of these
three hundred rows has a trigger on it", and the second question is the one that
arrives once a level is dense.

## Composed objects

An entity parented under another is one *thing* to the author -- that is the
entire reason for parenting a chandelier's candles to it. Four verbs already
honoured that (delete, duplicate, copy, hide/lock all take descendants); three
did not, and each failure was silent.

**Clicking.** The ray hits whichever mesh is in front, which is a candle.
Selecting the candle and dragging pulls one candle out of the chandelier, and
the author finds out by looking at the result. So:

| | |
|---|---|
| click | the outermost ancestor: the object |
| **Alt**+click | exactly what the ray hit, however deep |
| click again, from inside | drill into the object's parts |

The third rule is what makes adjusting one candle two clicks rather than a
modifier nobody remembers, and it is how Unity, Godot and Unreal all behave.
The rule is `resolvePickTarget` in `editor/include/editor/scene/PickTarget.h` -- a pure function
with a test, because "which entity did that click mean" is not something a
screenshot can check.

**Framing and the outline.** `boundsOf` takes descendants now. Pressing **F** on
a chandelier used to frame the hub and leave the candles outside the view, and
the selection outline drew a box a third the size of what the gizmo was about to
move -- so the outline actively lied about its subject.

**Knowing where you are.** The status bar appends `(in <object>)` whenever the
selection is inside one. Otherwise `candle_0003` is the whole story the editor
tells about a selection whose drag will move something bigger.

## The 2D viewport

A second tab beside the 3D one, because they are two views of one game.

It draws the game's real `GameHud`, on the real `eng::ui::UiCanvas`, from a real
`HudSnapshot` -- not a mock-up. That is the point: a HUD preview that
reimplements the HUD tells you about the preview. `GameHud::drawInto` is the
same layout as `GameHud::draw` with the canvas pointed at a panel rectangle
instead of the window (`UiCanvas::beginTarget`), so nothing in the layout knows
it moved.

What it is for: this HUD is authored in code plus a style sheet, not in a
document, so there is nothing to drag. What was missing was the ability to *see*
it at states and resolutions the window never produces:

- **virtual resolution** — 320x240 through 640x480, or fit the panel. The
  failures are at the extremes and a developer's window is never at an extreme.
- **the player's state** — health, stamina, arcana, status count, weapon name
  and discipline, tooltip. "Does the vitals block still fit at 320x240 when the
  weapon name is long and three statuses are up" was a question you could only
  answer by playing until it happened.
- **safe area** — a console HUD that ignores it is legible on a monitor and
  cropped on a television, and the crop is not something the developer sees.
- **grid** — every 16 virtual pixels, the layout's own unit, so a widget one
  pixel off its column is visible rather than merely wrong.

The scale is always an integer: the canvas is a bitmap font on a pixel grid, and
a fractional scale is how a retro HUD gets soft edges and uneven letter spacing.

The state → HUD-input mapping is `editor/include/editor/ui/UiStage.h`, pure and tested. The
interesting failures live in that gap (a resource that reads full when it is
empty, a status count past the array) and none of them show in a screenshot.

## Reading the inspector

Sections are grouped and always in the same order, whatever the entity carries:

| band | holds |
|---|---|
| *(identity + transform)* | id, name, parent, position/rotation/scale |
| **appearance** | Mesh (and its material), Shader, Particles, Light |
| **physical** | Collider, Trigger |
| **gameplay** | Camera, Spin, Player Spawn, Exit, Marker, Enemy Spawn, Pickup |
| **placement** | Grid Cell |

Before, sections came out in whatever order the component table happened to be
written in, so "where is the material" depended on which entity was selected —
and a fixed question whose answer moves around the screen is what makes a panel
unscannable. The order is `ComponentGroup` in `EntityComponents.h`; every
listing (inspector, add menu, outliner tooltip) sorts by it, stably, so the
hand-picked order inside a band survives.

Each section is a collapsing header with an **x** to remove it. **Ctrl+A** over
the panel opens the add menu, which is grouped into the same bands — so the menu
is a map of where the thing you are adding will appear.

### The right column is the Inspector

Material and Particles used to take its lower half, which cost the panel that
answers "what is this thing" half its height so two panels nothing was selected
in could be visible at once. They are on the **left** now, beside the Catalog,
because they are the same kind of thing: a library you browse and pick from.

What used to need them on the right is on the entity:

- the **material** combo is on the Mesh component;
- the **effect** combo is on the Particles component, filled from the live
  library so a name that does not resolve cannot be typed;
- the material **swatch follows the selection** — selecting a wall and looking
  at a sphere wearing something else was the panel answering a question nobody
  asked. Clicking a name in the material list still previews *that* name: the
  author is then asking about the material, not about the entity.

## Materials that fit the mesh

The material list was 126 names in one flat column and applying any of them to
any entity was one click. Most of those combinations do not render, and the ones
that matter fail *quietly*.

`MaterialCatalog` reads the shipped `.material` scripts and classifies each one
by the vertex program it binds, because that is what decides which meshes can
supply what it reads:

| class | needs | on an entity |
|---|---|---|
| `surface` | ordinary UVs, wrapping texture | anywhere |
| `atlas` | UVs inside the sheet's regions (`tex_address_mode clamp`) | kit pieces only |
| `vfx` | a flat quad with 0..1 UVs (`PixelVfx/*`) | liquids and portals |
| `particle` | a per-instance stream (`Particles/*`) | **draws nothing** |
| `sprite` | engine-generated billboards or decals | **draws nothing** |
| `post` | the framebuffer (`Dither_VS`) | **draws nothing** |
| `editor` | the editor's own content | missing once cooked |

The panel groups by class, hides the ones that can never go on an entity behind
a toggle, and colours the rest against **what the selection's meshes are**: a
kit piece declares the material it was authored with, and that declaration is
the only reliable statement of whether its UVs index an atlas or wrap.

Applying a `Broken` combination is **refused** and said; a `Risky` one is
applied and said, because an author who wants a liquid on a kit piece is allowed
to want that -- they should just not be surprised by it. The reasons name the
symptom ("the whole sheet stretches across each face"), not the rule.

The catalogue is also crossed with `Renderer::materialNames()`: a script whose
pack the editor did not mount resolves to the missing-material pink, and
offering it is offering a broken result.

## The level's own look

A scene names the palette it is lit and graded with -- a table in
`assets/config/palettes.toml` -- and the Inspector shows it when nothing is
selected, which is where every engine puts world settings and which was two
lines of dead space here.

A name, not the values: `palettes.toml` already holds forty tuned fields per
look, and a copy of them in every `.scn` is forty fields that drift.

```
.scn  "palette": "showroom"
  |
  v  SceneCook  ->  one entity carrying SceneEnvironment
  |
  v  MapRuntime::palette()
  |
  v  LiveLevel  ->  loadRenderPalette + applyRenderPalette
```

The viewport's light switch applies the same palette through the same loader
the game uses. An approximation would be worse than nothing: the point of that
switch is to answer a question about the shipped look, and an editor that
answers it with its own numbers is an editor that lies about the level.

A scene that names no palette writes no field, cooks no extra entity and plays
under the dungeon look -- which is what keeps every shipped `.map` identical.
A name that no longer exists is *kept* on load and reported, because opening the
scene is how the name gets fixed.

## What a room costs

The corner of the viewport carries frame time, draw batches, triangles, entity
count and the selection size, coloured against a budget (`FrameBudget`: 16.7 ms,
400 batches, 120k triangles). Timings are smoothed over about twenty frames --
a raw per-frame millisecond count flickers through three digits and reads as
noise. The counts are **not** smoothed: a batch count that lags the placement
that caused it is not smoother, it is wrong.

These numbers were previously in the status bar, in the same grey as the file
name, which is to say nobody watched them while placing geometry -- the only
moment they mean anything.

## The scene the editor opens

`assets/scenes/cozy_lair.scn` -- what `make editor` shows with no `SCENE=`.

A *shot* rather than a level: two props turning at their own rates in a small
lit room, a portal behind them, and a camera orbiting on a pivot. 36 entities in
a 12 x 12 m room -- it opens in under a second, and every component that
animates is visible at once: **Spin** on each prop *and* on the camera pivot,
**Camera** on two framings (one parked), **Shader** rim lights, a pulsing key
light and four torches flickering out of phase. It is also the thing to copy when making a clip; see
[`docs/authoring-shots.md`](authoring-shots.md).

A scene with an active camera plays *itself*: the player controller stands down
and `game <map>` runs the stripped loop, which is what makes it recordable.

### The level it replaced

`assets/scenes/start_hall.scn` is still there and still opens with
`make editor SCENE=assets/scenes/start_hall.scn`. Its design rationale,
encounter table and readability review are in
[`docs/levels/start_hall.md`](levels/start_hall.md).

A small crypt: a 3 x 3 cell hall (12 x 12 m) with an arch in its north wall for
the way out, and a shrine alcove hanging off the south wall through a second
arch. 128 entities, of which most are dressing: barrels and their hoops, a
market table with bread and a pumpkin on it, sacks, hay, crates, vases with
their lids, a leaning door, torches and hanging lamps, timber propping the
ruined west wall, rubble, a fallen pillar, and a crystal shrine under a shaft of
light with a chest and two trophies.

It is authored, not generated at runtime, and it is *small on purpose*: it opens
in a moment, it fits on screen, and every kind of thing the editor can place is
in it once, which makes it a fixture for looking at a change as well as a
starting point for a level.

`tools/author_start_hall.py` re-derives it. Grid placement is arithmetic --
cell centres, the half-thickness wall inset, the `y_offset` exceptions -- and
that arithmetic lives in `editor/src/content/GridMath.cpp`; the script mirrors it and
the cooker's validator is what proves the two still agree:

```sh
python3 tools/author_start_hall.py assets/scenes/start_hall.scn
./build/scene_cook assets/scenes/start_hall.scn --kit assets/config/kit.toml --validate-only
```

Editing it by hand in the editor is fine and expected; the script is for moving
a wall, not for owning the file.

## The catalogue

`assets/config/kit.toml` is the one catalogue: the architectural kit *and* the
prop, item and landmark meshes. The prop set under `meshes/props` is authored at
world scale rather than on the kit's 20-unit grid, so those entries carry
`import_scale = 1.0` and state their size in metres:

```toml
[[piece]]
id = "prop_barrel"
role = "prop_container"
mesh = "meshes/props/prop_barrel_p0.obj"   # a path: pack-relative, not mesh_dir
material = "Game/PropPlanks"
socket = "prop"
import_scale = 1.0                          # metres; omit for kit art
size = [1.32, 1.387, 1.32]
```

A `mesh` with no `/` still resolves inside `[kit].mesh_dir`; one with a
separator is pack-relative. `KitPiece::meshScale()` is what every consumer asks
for the piece's scale, so a metre-authored prop is not shrunk to a fifth by the
kit's import scale.

Multi-part props (barrel body + hoops, table legs + top, vase + lid) are listed
part by part, because a catalogue entry places one mesh with one material.

**Portals are not a mesh.** A level's portal is its `exit` component: the
runtime builds the membrane, the kit surround, the wisps and the light around
that entity (`createPortalProp`). The Catalog's gameplay section calls the
button `exit / portal` for that reason.

## Editing what a mouse reaches

`make editor-selftest` runs the editor through the edits that used to be
reachable only by clicking -- remove a component, remove the mesh, unparent,
delete an entity, delete one with children -- one per frame, with the entity
selected a frame *earlier* so the gizmo, the inspector section and the outliner
row have all drawn it before the edit lands.

It exists because four separate crash reports were all "I clicked x and it
died", and none of them could be reproduced from a test. The one it caught:
a scene that authors a `Camera` made the *preview* attach the renderer's camera
to a preview node, and the next rebuild destroyed that node -- taking the
engine's only camera with it, because `destroyNode` destroyed every attached
movable. Two fixes, both in the engine: `destroyNode` now detaches the camera
instead of destroying it (it belongs to the render core, not to a node), and a
world that is being *looked at* rather than *through* is attached with
`drivesCamera = false`.

It is a `make` target rather than a ctest because it needs a GL context.

## The component table

`editor/include/editor/scene/EntityComponents.h` is the single description of what a component
is. It is ImGui-free and covered by `editor_component_tests`:

```cpp
struct ComponentType {
    const char* id;    // "light" -- matches the .scn field name
    const char* label; // "Light"
    const char* hint;  // one line in the add menu
    bool (*has)(const Entity&);
    void (*add)(Entity&, const ComponentDefaults&); // null: not addable by hand
    void (*remove)(Entity&);                        // null: always present
    bool (*addable)(const ComponentDefaults&);      // null: always
};
```

The widgets live in `editor/src/ui/ComponentInspector.cpp`, keyed by the same
`id`. The split is deliberate: the half that answers "what does this entity
have" stays testable headless, and the half that draws it stays out of the
document.

Two of the entries are what turn a scene into a shot:

- **Camera** -- fov, clip planes, `priority`, `active`. The viewport draws its
  frustum, at the viewport's own aspect, because a fov is a number nobody can
  judge and the wedge it cuts through the room is the same value made visible.
  The highest-priority active camera is the one the game looks through, so a
  second framing is added by copying the first and unticking `active`.
- **Spin** -- an axis and degrees per second, turning the entity where it
  stands. Whatever is parented under it turns with it.
- **Orbit** -- a centre, a radius, an axis and a facing: the entity travels a
  ring. No pivot entity, no parent link. The viewport draws the ring, for the
  same reason it draws the frustum: "radius 5.4" is a number nobody can judge,
  and the circle it cuts next to the walls it has to stay inside is the
  decision. `facing: look at centre` is what a camera circling a subject wants;
  `free` leaves the rotation to Spin, so one entity can do both.

A chain with a Spin or an Orbit above it is the one case the cooker does *not*
bake flat -- a live parent has to stay a parent.

Two components are special, and the table says so rather than a panel doing:

- **Mesh** is only addable when the Catalog has a brush selected -- prefab ids
  come from `kit.toml`, so the table cannot invent one. The add menu greys the
  row and says why.
- **Grid Cell** cannot be added by hand at all. It is produced by placement, and
  removing it is what unpins a piece from the grid.

Removing Mesh also clears the material override and the grid cell, because
neither means anything without it.

## Editing components

Both panels go through the same two calls, and both act on the **whole
selection** as a single composite command:

```cpp
void EditorApp::addComponentToSelection(const ComponentType&);
void EditorApp::removeComponentFromSelection(const ComponentType&);
```

- Inspector: `Add Component`, and the `x` on each section header.
- Outliner: right-click any row (or group) -> `Add Component`.
- Scene > New still exists for whole entities; it now takes its defaults from
  the same table, so "New > Light" and "Add Component > Light" produce the same
  light.

Field edits are recorded on release -- one undo entry per edit, not one per
frame -- and the document is touched while dragging so the viewport follows.

## Adding a component type

Two entries, no panel changes.

1. Add the field to `game::content::Entity` and to `SceneWriter` /
   `SceneSource` / `SceneCook`, the way every existing component does it.
2. Add a `ComponentType` to the table in `editor/src/scene/EntityComponents.cpp`:

```cpp
{"sound", "Sound Emitter", "loops a sound at this point",
 [](const Entity& e) { return e.sound.has_value(); },
 [](Entity& e, const ComponentDefaults&) { e.sound = SoundAuthor{}; },
 [](Entity& e) { e.sound.reset(); }, always},
```

3. Add its widgets to `kDrawers` in `editor/src/ui/ComponentInspector.cpp`:

```cpp
void drawSound(Entity& e, InspectorContext& c) { stringField("event", e.sound->event, c); }
// ...
{"sound", drawSound},
```

The outliner, the add menus, the multi-select edit path and the undo commands
pick it up with no further change. A component with no drawer still works
everywhere else; the inspector section just says `no inspector for 'sound'`.

## Verification hooks

A screenshot run has no mouse, so two env vars drive the panels:

```sh
RAVEN_EDITOR_SELECT=crystal_0001     # an author id or index; also frames it
RAVEN_EDITOR_ADD_COMPONENT=collider  # a ComponentType id, applied to the selection
RAVEN_EDITOR_BRUSH=kit.prop_barrel   # arm the Place tool, so the ghost is drawn
RAVEN_EDITOR_WALK=1                  # start at the player's eye, at the spawn
RAVEN_EDITOR_PANEL=palette|open|help # a surface that only exists while held open
RAVEN_EDITOR_PANEL=outliner|catalog|inspector|issues|ui  # bring a docked tab forward
RAVEN_EDITOR_PALETTE_QUERY=cook      # open the palette with a query already typed
RAVEN_SCREENSHOT=/tmp/x.png RAVEN_SCREENSHOT_FRAME=240 \
  xvfb-run -a ./build/scene_editor assets/scenes/start_hall.scn
```

Tests:

| target | covers |
|---|---|
| `editor_outliner` | grouping, folding, filtering, kind tags, composed objects |
| `editor_outliner_panel` | real clicks on group, leaf and nested rows, headless ImGui |
| `editor_component` | registry coherence, add/remove round-trips, composition |
| `editor_paint_slot` | one piece per click, trails when dragging |
| `editor_viewport_grid` | the drawn grid always outreaches the camera |
| `editor_picker` | rays, projection, and the ghost's fallback point |
| `editor_pick_target` | which entity a click on a composed object means |
| `editor_ui_stage` | the 2D viewport's state -> HUD-input mapping, and scale fitting |
| `editor_entity_gizmo` | marks for the meshless half of a level, and picking them |
| `editor_command_palette` | ranking: what you half-remembered is the first row |
| `editor_scene_browser` | listing, recent scenes, autosave paths and staleness |
| `editor_settings` | the autosave clock, a hand-edited settings file, and the environment F5 launches with |
| `make editor-selftest` | not ctest: drives the real editor through the edits only a mouse could reach |
| `editor_clipboard` | fresh ids, offsets that survive the cook, composed copies |
| `editor_viewport_overlay` | the cost readout stays readable, and its budget |
| `scene_hierarchy` | parent chains, cycles, and the cook that flattens them |
| `editor_material_catalog` | classifying materials by what they need from a mesh |
| `editor_vocabulary` | the enemy ids the drop-downs are filled from |
| `scene_environment` | the palette a level names, through save, load and cook |
