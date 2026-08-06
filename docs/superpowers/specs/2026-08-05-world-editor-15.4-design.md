# The Game World Editor — measured against Gregory §15.4

*Spec and plan, 2026-08-05.*

The brief was to refactor the scene editor to follow *Game Engine Architecture*
3e §15.4, "The Game World Editor". §15.4.1 lists ten features a world editor is
expected to have. This document audits the editor against that list, states what
is being built and why, and records the calls that could have gone the other way.

## The audit

| §        | Feature                          | Before                                                                     | Verdict |
|----------|----------------------------------|----------------------------------------------------------------------------|---------|
| 15.4.1.1 | Chunk creation and management    | `.scn` per chunk, `SceneBrowser`, `SceneTemplates`, cook to `.map`           | present |
| 15.4.1.2 | Visualization                    | one 3D perspective view; **no orthographic projection at all**              | **gap** |
| 15.4.1.3 | Navigation                       | orbit / fly / walk, frame-selection                                        | partial |
| 15.4.1.4 | Selection                        | multi-select, ray pick, gizmo-mark pick, outliner tree, hide, lock         | partial |
| 15.4.1.5 | Layers                           | **nothing**                                                                | **gap** |
| 15.4.1.6 | Property grid                    | `ed::ui::PropertyGrid`, per-component drawers, single-object editing        | partial |
| 15.4.1.7 | Placement and alignment aids     | translate/rotate/scale gizmos, snap-to-grid, work plane                    | partial |
| 15.4.1.8 | Special object types             | lights, particles, audio, triggers, cameras, colliders all have gizmo marks | present |
| 15.4.1.9 | Saving and loading chunks        | JSON `.scn` + schema + round-trip tests                                     | present |
| 15.4.1.10| Rapid iteration                  | live `PreviewBridge`, F5 cook-and-play, autosave                            | present |
| 15.4.2   | Integrated asset management      | Asset Browser, Resource DB tab, `raven_acp`                                 | present |

Six of the eleven rows already hold. This is not a rewrite; it is filling the
five holes the chapter names, in the editor's existing idioms.

## What is being built

### 1. Layers (§15.4.1.5) — the largest gap

The chapter's argument for layers is not tidiness, it is division of labour:
*"all of the lights might be stored in one layer, all of the background geometry
in another and all AI characters in a third... the lighting, background and NPC
teams can all work simultaneously on the same world chunk."*

**Split of ownership.** A layer's *identity* — its id, display name and colour —
and an entity's *membership* are document data: they are decisions two people
share, and a torch that is "lighting" is lighting for everyone. A layer's
*visibility* and *lock* are session data, in `EditorState`, next to the existing
per-entity `hidden` / `locked` lists. That follows the precedent already set in
`EditorState.h`: hiding a ceiling to see the room is how one person works today,
and it must not land in a file the other two people merge.

**Effective visibility** is the union: an entity is hidden if it is itself
hidden, its layer is hidden, or a solo is active and its layer is not soloed.
One function, `ed::layers::hiddenBy`, answers that for the viewport filter, the
picker, the marquee and the outliner, so those four cannot disagree.

**Per-layer save/load** is the chapter's parallel-work claim made real:
*Export layer…* writes a `.scn` holding only that layer's entities (and the
ancestors they hang from), *Import layer…* merges one back, renaming on id
collision. That is the granularity §15.4.1.9 asks about, without splitting the
on-disk format into a file per layer — which would break every existing scene.

**Cook drops layers.** They are an authoring organisation, and the runtime map
is flat. `SceneValidate` reports an entity naming a layer that does not exist.

### 2. Orthographic elevations (§15.4.1.2)

*"virtually all game world editors provide a three-dimensional perspective view
of the world and/or a two-dimensional orthographic projection."*

`RenderCore::View` gains one field, `orthoHeight`; zero keeps the perspective
path exactly as it is, and only the Editor target ever sets it. The game's
rendered image is untouched, which the frozen-image rule requires.

`EditorCamera` gains four view presets — Persp, Top, Front, Side — where the
three elevations lock yaw/pitch to an axis, switch the projection, and turn the
scroll wheel from a dolly into a zoom. Picking builds parallel rays instead of
a pinhole fan; `ed::screenRay` grows an ortho sibling rather than a flag, so a
caller cannot forget which it is in.

**The quad-pane layout is deliberately not built.** The chapter says four panes
are common, and they are — but `RenderCore` has exactly one offscreen editor
target, and four would mean four scene passes per frame and a real restructuring
of the render core. One viewport whose projection switches on a key gives the
authoring value (blockout against a true plan view, alignment checked from the
front) at none of that cost. It is listed as left-open at the end.

### 3. Selection (§15.4.1.4)

*"Objects might be selected via a rubber-band box in the orthographic view or by
ray cast style picking in the 3D view"* and *"the editor might allow the user to
cycle through all of the objects that the ray is currently intersecting rather
than always selecting the nearest one."*

Three additions, all in one pure module (`ed::selection`):

- **Marquee.** Drag on empty space, get every entity whose projected bounds fall
  inside the rectangle. Works in both projections because it consumes a
  view-projection matrix, not a camera.
- **Ray cycling.** `raycastDocument` currently returns the nearest hit. A second
  entry point returns all of them, sorted, and a click at the same screen point
  as the last one advances through the list. That is the chapter's answer to a
  densely populated world, and it is better than the hide-and-retry loop.
- **Named selection sets**, saved in the session sidecar.

### 4. Property grid (§15.4.1.6)

Two features from the chapter, both missing:

- **Multi-object editing.** *"If a particular attribute has the same value across
  all objects in the selection, the value is shown as-is... If the attribute's
  value differs from object to object, the property grid typically shows no
  value at all."* Implemented for the transform first, because that is the
  chapter's own worked example of what survives a heterogeneous selection, and
  as a field-level diff-and-fan-out for every component drawer: whatever field
  an author actually changed is applied to every selected entity carrying that
  component, as one undo entry. Fields they did not touch are left alone, which
  is what makes editing a mixed selection safe.
- **Free-form properties.** *"Some editors also allow additional 'free-form'
  properties to be defined by the user on a per-instance basis... incredibly
  useful for prototyping new gameplay features."* A per-entity list of typed
  key/value pairs, cooked into the same `ScriptProp` store the scripting layer
  already reads, so a prototype property is reachable from Lua on the frame it
  is authored rather than being a dead end in a file.

### 5. Navigation (§15.4.1.3) and alignment (§15.4.1.7)

The chapter's convenience list, taken literally: *"the ability to save various
relevant camera locations and then jump between them, various camera movement
speed modes... a Web-browser-like navigation history"*, and *"snap to grid, snap
to terrain, align to object and many more"*.

Bookmarks and history live in the session sidecar. Alignment is a pure module
(`ed::align`) — align min/centre/max per axis, distribute, drop to the surface
below, match rotation or scale to the primary — so every one of them is a
headless test rather than something judged by eye.

## Left open, on purpose

- **Quad-pane viewport** — needs `RenderCore` to grow N editor views. Reason above.
- **Splines and nav meshes** (§15.4.1.8) — the chapter lists them as special
  object types, but each is a new runtime component with its own cook and
  gameplay meaning, not an editor feature. They belong to whatever system first
  needs one.
- **Snap to terrain** — this engine has no terrain; drop-to-surface below is
  the same gesture against the geometry that does exist.

## Plan

1. Layers: `Layers.{h,cpp}` (pure) → document fields → reader/writer/schema →
   validate → session state → panel → outliner filter → export/import.
2. Ortho: `RenderCore::View::orthoHeight` → `Renderer` API → `EditorCamera`
   presets → `Picker` ortho rays → viewport wiring → ImGuizmo/overlay.
3. Selection: `Selection.{h,cpp}` (marquee + cycling) → `DocumentRaycast`
   all-hits → viewport wiring → saved sets.
4. Property grid: free-form property type → format → cook → inspector drawer;
   multi-edit summary + fan-out commit.
5. Navigation and alignment: `Align.{h,cpp}` → menus, palette, keybinds;
   bookmarks/history in `EditorState` + sidecar.
6. A ctest binary per pure module; build incrementally; screenshot the editor.
7. `docs/world-editor.md`, and update `docs/editor-ui-architecture.md`.
