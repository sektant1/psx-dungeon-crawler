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

`buildOutliner` (`game/editor/OutlinerTree.h`) groups the document:

- entities with a prefab group by **prefab id** -- forty `kit.wall` pieces that
  differ only by id become one row with a count;
- entities without one group by **kind** (`light`, `marker`, `enemy`, ...);
- a group with one member draws as a leaf, so the tree does not make you expand
  a row to find a single spawn;
- gameplay groups sort above geometry, each alphabetically, and members sort by
  id -- placing a wall never reshuffles the panel under the cursor.

Clicking a group row selects **everything in it**. That is the point of the
grouping: "give all fifty-four walls a collider" is then one click, one menu
item, and one undo entry.

`show geometry` folds away kit pieces that carry no gameplay component. A wall
with a trigger on it stays visible -- it is the entity you are hunting for, and
it happens to have a mesh.

The tree is rebuilt when the document revision or the panel's options change,
not per frame: it walks and sorts every entity, and the panel is open while the
gizmo is being dragged.

## Placing: strokes and slots

The Place tool paints. It drops a piece **every frame the button is held**, so
dragging across a room fills it, and a whole drag closes as one undo entry.

What keeps that from stacking a pile in one spot is the slot
(`game/editor/PaintSlot.h`): a stroke remembers the slots it has filled and
skips them. A grid piece's slot is its cell, edge and work plane. A free prop
has no cell, so its slot is its **position, quantized** -- to the grid step when
snapping is on, to `kFreePaintSpacing` (0.5 m) when it is off.

That quantization is the fix for props landing in a pile: the slot used to be a
counter, which is unique every frame, so the frame rate decided how many props a
click produced. Separate clicks are separate strokes, so clicking twice in one
spot still gives two props.

## The component table

`game/editor/EntityComponents.h` is the single description of what a component
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

The widgets live in `game/editor/ComponentInspector.cpp`, keyed by the same
`id`. The split is deliberate: the half that answers "what does this entity
have" stays testable headless, and the half that draws it stays out of the
document.

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
2. Add a `ComponentType` to the table in `game/editor/EntityComponents.cpp`:

```cpp
{"sound", "Sound Emitter", "loops a sound at this point",
 [](const Entity& e) { return e.sound.has_value(); },
 [](Entity& e, const ComponentDefaults&) { e.sound = SoundAuthor{}; },
 [](Entity& e) { e.sound.reset(); }, always},
```

3. Add its widgets to `kDrawers` in `game/editor/ComponentInspector.cpp`:

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
PSX_EDITOR_SELECT=arena_gate       # an author id, or an index into the document
PSX_EDITOR_ADD_COMPONENT=collider  # a ComponentType id, applied to the selection
PSX_SCREENSHOT=/tmp/x.png PSX_SCREENSHOT_FRAME=240 \
  xvfb-run -a ./build/scene_editor game/assets/scenes/tech_demo.scn
```

Tests: `editor_outliner` (grouping, folding, filtering, kind tags) and
`editor_component` (registry coherence, add/remove round-trips, composition).
