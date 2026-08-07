# The Game World Editor {#doc-world-editor}

The scene editor measured against *Game Engine Architecture* 3e §15.4, "The Game
World Editor". §15.4.1 lists the features a world editor is expected to have;
this document says where each one lives in this repository, what was decided,
and what is deliberately absent.

Companion documents: [Editor UI Architecture](editor-ui-architecture.md) for
panel topology and styling, [Scene Editor Entities](scene-editor-entities.md)
for the component table.

## Feature map

| §        | Feature                       | Where |
|----------|-------------------------------|-------|
| 15.4.1.1 | Chunk creation and management | `.scn` per chunk, `SceneBrowser`, `SceneTemplates`, `SceneCook` |
| 15.4.1.2 | Visualization                 | perspective + three orthographic elevations, `EditorCamera::Projection` |
| 15.4.1.3 | Navigation                    | `ed::nav` — bookmarks, back/forward history, speed steps |
| 15.4.1.4 | Selection                     | `ed::selection` — marquee, click cycling, named sets |
| 15.4.1.5 | Layers                        | `ed::layers`, `game::content::Layer`, the Layers panel |
| 15.4.1.6 | Property grid                 | `ed::ui::PropertyGrid`, `ed::multiedit`, free-form properties |
| 15.4.1.7 | Placement and alignment aids  | `ed::align`, plus the existing grid snap and work plane |
| 15.4.1.8 | Special object types          | `EntityGizmos` — lights, particles, audio, triggers, cameras, colliders |
| 15.4.1.9 | Saving and loading chunks     | `SceneSource` / `SceneWriter`, `scene.schema.json`, per-layer export |
| 15.4.1.10| Rapid iteration               | `PreviewBridge`, F5 cook-and-play, autosave |
| 15.4.2   | Integrated asset management   | Asset Browser, `ResourceDbPanel`, `raven_acp` |

## 15.4.1.5 Layers

The chapter's argument for layers is not tidiness, it is division of labour:
*"all of the lights might be stored in one layer, all of the background geometry
in another and all AI characters in a third... the lighting, background and NPC
teams can all work simultaneously on the same world chunk."*

**The split of ownership is the design.** A layer's identity — its id, display
name and colour — and an entity's membership are **document** data, in the
`.scn`: they are decisions two people share, and a torch that belongs to
"lighting" belongs to it for everyone. A layer's visibility, lock and solo are
**session** data, in `ed::layers::LayerSession`, beside the per-entity
`hidden`/`locked` lists in `EditorState`. Hiding a ceiling to see a room is how
one person works for ten minutes, and it must not land in a file two other
people merge.

The default layer is the empty id. It is implicit, never written, and is where
every entity authored before layers existed lives — so no scene needed
migrating and unchanged files still round-trip byte for byte.

**Effective visibility is the union.** `EditorState::isHidden` returns true if
the entity is hidden on its own account *or* its layer is off *or* a solo is
active elsewhere. The viewport filter, the picker, the marquee and the outliner
all ask that one function, so they cannot disagree about whether something is
reachable. `isHiddenAlone` is the entity's own flag, which is what the
outliner's per-row eye reads and writes.

**Per-layer save and load** is the parallel-work claim made real without
splitting the on-disk format into a file per layer:

- *Export layer…* writes a `.scn` holding only that layer's entities, plus the
  ancestors they hang from as bare transform stubs — without those, a lighting
  pass opened on its own would put every torch at the world origin.
- *Import into layer…* merges one back. Colliding ids are **renamed, never
  overwritten**: the merge is somebody's afternoon of work arriving, and
  silently replacing an entity that happens to share a name is the one outcome
  nobody can undo by hand. Ancestor stubs already present are reused rather than
  duplicated. Parent links are rewritten to follow the renames.

Layers are dropped at cook — the runtime map is flat. `SceneValidate` reports
`layer.undeclared` for an entity naming a layer nothing defines, because a
botched merge can otherwise park a room in a layer the panel cannot reach.

## 15.4.1.2 Visualization

`RenderCore::View` gained one field, `orthoHeight`. Zero keeps the perspective
path exactly as it was, and only the editor's offscreen target ever sets it —
the game's rendered image cannot be affected by a field nothing in the game
writes, which the frozen-image rule requires.

`EditorCamera::Projection` is `Perspective`, `Top`, `Front` or `Side`. The three
elevations lock yaw and pitch to an axis, switch the projection, and turn the
wheel from a dolly into a zoom (moving an orthographic eye forward changes
nothing on screen). Entering one frames the selection, or the level, padded.

Three things must agree about the projection or clicks land in the wrong place,
and all three read the same `activeOrthoHeight()`:

- `Renderer::setEditorCameraOrtho`, pushed every frame beside the pose;
- `cameraMatrices()` in `EditorApp`, which builds the matrix the gizmo marks
  project through;
- `EditorApp::mouseRay()`, which calls `ed::orthoViewportRay` — **rays are
  parallel in an elevation**. That is a separate function rather than a flag,
  because a caller who forgot the flag would get a pinhole fan in a projection
  with no pinhole, and clicks would miss by more the further they were from the
  centre of the viewport.

`ImGuizmo::SetOrthographic` follows too, or the handles do not line up with the
axis they drag.

**The four-pane layout is deliberately absent.** The chapter notes it is common,
and it is — but `RenderCore` has exactly one offscreen editor target, and four
panes would mean four scene passes a frame and a real restructuring of the
render core. One viewport whose projection switches on a toolbar click gives the
authoring value at none of that cost.

## 15.4.1.4 Selection

Three additions, all pure and headless-tested in `ed::selection`:

- **Marquee.** Drag on empty space; everything whose projected bounds meet the
  rectangle is selected. It consumes a view-projection matrix, so it works in
  both projections. Alt demands the footprint be fully enclosed — for boxing one
  room without catching the corridor behind it. Bounds are projected as their
  eight corners and reduced to a screen box, which over-estimates a rotated
  box's footprint: the right error to make, since a band that misses something
  the author clearly dragged across is worse than one that catches a neighbour.
- **Click cycling.** `raycastDocumentAll` returns every hit, nearest first, ties
  to the smaller box — the same order the single-answer `raycastDocument` picks
  by, so element zero is what a plain click would have selected.
  `selection::PickCycle` walks the list on repeated clicks at the same spot, and
  restarts when the click moves or the stack under it changes. The status line
  says "2 of 4 under the cursor", which is what makes the gesture discoverable.
  The chapter's alternative — hide what is in front and try again — is a loop.
- **Named selections**, saved in the session and pruned on restore of entities
  the document no longer has.

## 15.4.1.6 Property grid

Two features from the chapter, both new.

**Multi-object editing.** The inspector draws one entity — the primary —
because a hundred hand-written component drawers cannot each learn to be
N-valued. What makes that equivalent to the chapter's amalgam grid is
`ed::multiedit`: when an edit closes, `finishInspectorEdit` diffs the entity
against its pre-edit state and applies **only the fields that actually moved**
to every other selected entity that can take them, as one undo entry.

Field-level, never component-level. Copying a whole component would mean
dragging one light's range also stamped the primary's colour on the other
thirty-nine. What fans out: each transform axis independently, `material` (only
onto entities with a prefab to override), `castShadows`, `layer`, and the light
fields. What deliberately does not: `LightAnimAuthor::phase`, the per-instance
offset that stops a wall of torches guttering in lockstep.

**Mixed values.** `multiedit::agreementOf` reports which transform axes the
selection shares; a row they disagree on draws `--` instead of the primary's
number, and editing it writes to all of them. The transform is where this is
shown because it is the chapter's own worked example of what survives a
heterogeneous selection.

**Free-form properties.** A per-entity list of typed key/value pairs —
`Entity::properties`, the `properties` component in the inspector. They cook
into `eng::ecs::Properties`, and `ScriptHost` seeds `self.props` from them
before the script's own props, which override on a key clash. So tagging a crate
`flammable = true` in the editor is readable from whatever script it carries, on
the next playtest, with no C++ written. That reachability is the point: the
chapter calls these out for prototyping, and a property nothing can read is a
line in a file, not a prototype.

## 15.4.1.3 Navigation and 15.4.1.7 Alignment

`ed::nav` holds ten bookmark slots (`Alt+1`…`Alt+0` to jump, `Ctrl+Alt+<n>` to
set — `Ctrl+<n>` is already panel focus), a browser-style back/forward stack over
camera jumps (`Alt+Left` / `Alt+Right`), and the coarse/fine speed steps.

The history records **jumps only** — framing a selection, following a bookmark,
entering isolation — never ordinary flying, or Back would mean "undo the last
twitch". Identical consecutive poses are dropped, or Back would do nothing
several times before doing something.

`ed::align` is the Arrange menu: align min/centre/max per axis, distribute
evenly, and drop to surface. All of it works on **world bounds**, not origins —
"line these crates up against that wall" is a statement about where their sides
are, and a crate's origin is wherever the artist left it. Align to centre uses
the midpoint of the whole spread, so the operation has a fixed point and does
not depend on which entity happens to be primary. `applyMoves` converts the
world-space results back into authored local transforms, which is what keeps a
parented entity where it is put.

"Snap to terrain" from the chapter's list becomes drop-to-surface: this engine
has no terrain, so the same gesture casts down against the geometry that exists,
skipping entities with nothing underneath rather than dropping them to y=0.

## What is deliberately not here

- **Four-pane viewport** — reason above.
- **Splines and nav meshes** (§15.4.1.8). The chapter lists them as special
  object types, but each is a new runtime component with its own cook and
  gameplay meaning, not an editor feature. They belong to whatever system first
  needs one.
- **Free-form properties on multi-selections.** They fan out per entity like any
  other component, but there is no merged N-valued view of a key that only some
  of the selection carries.

## Tests

Every pure module has a headless ctest binary. None of them needs a window,
which is the point of them being separate files rather than more of
`EditorApp.cpp`.

```sh
ctest --test-dir build -R "editor_layer|editor_selection|editor_navigation|editor_align|editor_multiedit"
```

| Binary | Covers |
|---|---|
| `editor_layer_tests` | membership, session visibility, solo, extract/merge |
| `editor_selection_tests` | marquee fit modes, cycling, named sets |
| `editor_navigation_tests` | pose round trip, bookmarks, history, elevations |
| `editor_align_tests` | align/distribute/drop against offset pivots |
| `editor_multiedit_tests` | which fields fan out, and which must not |
| `schema_sync_tests` | the writer and `scene.schema.json` still agree |

## Verification hooks

A screenshot run has no mouse, so the surfaces behind a click are reachable from
the environment:

```sh
RAVEN_EDITOR_PANEL=layers  RAVEN_SCREENSHOT=/tmp/x.png RAVEN_SCREENSHOT_FRAME=140 ./scene_editor scene.scn
RAVEN_EDITOR_VIEW=top      RAVEN_SCREENSHOT=/tmp/x.png RAVEN_SCREENSHOT_FRAME=140 ./scene_editor scene.scn
```

`RAVEN_EDITOR_VIEW` takes `top`, `front` or `side`, and is applied after the
first viewport pass — framing an elevation needs both the open document and a
measured viewport.
