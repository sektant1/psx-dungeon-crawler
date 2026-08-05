# Isolation: editing one entity on an empty stage

*2026-08-04*

## The problem

The editor has exactly one way to look at a scene: the whole level, at once.
Composing a single object inside it — a chandelier and its four candles, a
viewmodel rig and the weapon on its socket — means doing fine work at
centimetre scale while three hundred walls, forty lights and a ceiling are in
the way. The ceiling cut exists precisely because that is unworkable, and it is
a blunt instrument: it hides by *height*, not by what you are editing.

Everything needed to do better is already here and unused for this:

- **Entities nest.** `Entity::parent`, `childrenOf`, `worldTransform`,
  `wouldCycle`, drag-to-reparent in the outliner, and an outliner group kind
  (`composed`) whose whole reason for existing is "one authored root and
  everything parented beneath it".
- **A mode can take the viewport.** `mMaterialMode` already hides the level,
  swaps the environment, saves and restores the camera, and disables the tools
  that make no sense in it.
- **The grid is a ground plane anywhere.** `updateGridLines` draws it at
  `gridState.level`, centred on the camera, coarsening with height. Put that
  plane under an entity and you have a superflat sandbox for free.

So this is not a new subsystem. It is a third mode, a visibility filter, and a
scoped outliner.

## The design

### One mode enum, not a second boolean

`mMaterialMode` becomes `ViewportMode { Level, Isolate, Material }`. Two
independent booleans deciding what the viewport shows is how they end up both
true; the ~20 sites that ask "is this the material stage?" are asking a question
with three answers now, and the compiler should make them say which.

This is the "the editor feels unplanned" complaint answered at the one place it
is structural rather than cosmetic.

### Isolation is a view, not a document

The isolated entity is **not** extracted, copied, or opened as a separate file.
The document is untouched; what changes is which entities are drawn, which rows
the outliner lists, and where the camera and grid plane are. Consequences that
matter:

- Undo/redo keep working, across entering and leaving.
- Save and cook still write the whole level, and cannot be surprised.
- An edit made while isolated is the same edit made in the level.

The alternative — a sub-document, Godot's separate `.tscn` per prefab — is a
real feature and a much larger one: it needs an instancing model, override
semantics, and a second serialiser. It is not required to make composing an
object bearable, which is the actual complaint.

### World coordinates stay true

The subtree is shown where it is, not moved to the origin. The sandbox feeling
comes from hiding everything else, dropping the grid plane to the root's own
height, and framing the camera on it.

Moving the subtree to the origin would read as more "prefab-like" and would be a
lie: gizmos, snapping, picking and the transform fields all work in world space,
and every one of them would need a compensating offset. One coordinate system,
told honestly, is worth more than the resemblance.

### Visibility goes through the one existing decision

`PreviewBridge::Impl::cutAway` already carries a comment warning that three
places deciding what is drawn is how one of them ends up disagreeing. Isolation
joins it there, ahead of the hidden set and the ceiling cut:

```cpp
bool cutAway(const AuthorId& id) const {
    if (isolating && isolated.count(id) == 0) return true;   // new
    if (hidden.count(id) != 0) return true;
    ...ceiling cut...
}
```

Picking follows automatically: `entityVisible` is the same function, so an
entity hidden by isolation cannot be clicked through.

### Entering and leaving

**Double-click an outliner row.** That gesture currently calls `focus` (frame
the camera), which is also bound to `F` and is on the context menu — so the
gesture is free, and taking it costs nothing.

On enter: save the camera, the grid plane and the ceiling cut; collect the root
and its descendants; put the grid at the root's world Y; frame the camera on the
subtree's bounds; select the root.

On leave (Esc, the breadcrumb, or the command palette): restore all three.

**A deleted root leaves automatically.** Undo can remove the entity being
isolated, and a mode pinned to an id that no longer exists is a viewport showing
nothing with no way out. Checked every sync.

### New entities land inside

While isolated, anything placed is parented to the isolated root, with its
transform converted to the parent's frame through the existing
`localFromWorld`. Without this the mode is read-only in practice: you would
isolate an object to add a part to it and the part would appear as a sibling in
the level.

This is the workflow the whole feature exists for — *isolate the viewmodel
entity, place the weapon, it is a child* — so it is not an extra.

### The outliner scopes to the subtree

`OutlinerOptions` gains a `root`. When set, `buildOutliner` walks only that
entity and its descendants. The panel is then the object's own tree, which is
what makes the mode feel like an editor for one thing rather than a filter over
a level.

## Files

| Change | Where |
|---|---|
| The mode enum, isolation state | `editor/include/editor/app/EditorState.h` |
| Enter/leave, framing, auto-parent, breadcrumb wiring | `editor/src/app/EditorApp.{cpp,h}` |
| Visibility filter | `editor/src/viewport/PreviewBridge.{cpp,h}`, header |
| Scoped tree | `editor/include/editor/scene/OutlinerTree.h`, `editor/src/scene/OutlinerTree.cpp` |
| Double-click action | `editor/include/editor/ui/OutlinerPanel.h`, `editor/src/ui/OutlinerPanel.cpp` |
| Breadcrumb | `editor/include/editor/viewport/ViewportOverlay.h`, `editor/src/viewport/ViewportOverlay.cpp` |
| Tests | `editor/tests/` — subtree collection, scoped outliner, auto-parent transform |
| Docs | `docs/scene-editor-entities.md` |

## As built

Shipped as designed, plus two things the design did not anticipate.

**Kit attachments are not sub-entities at all.** The design assumed "sub-entity"
meant `Entity::parent`. It does for imported models -- the dwight model in
cozy_lair isolates to twelve editable children. But a *compound kit piece*
(`kit.prop_boss_placeholder` and its sword) declares its parts in kit.toml and
the cooker emits them at build time, so they are not in the document at all.
Isolating that boss reported **"0 parts"** with a sword plainly visible in its
hand, which is exactly the workflow complaint that started this work.

That produced a second feature, **Unpack attachments**: write the parts out as
real child entities and set `unpacked_attachments` so the cooker stops
generating its own. `scene_unpack_tests` pins the property that makes it safe --
the built registry is identical either way.

**Guarding only the root double-drew every nested level.** A part may itself be
a compound piece; its own attachments are authored by the recursion, so it needs
the flag too. Caught by the generality half of the test (a synthetic kit with
several attachments per parent, three levels deep) rather than by the shipped
boss, which has exactly one attachment at one level and would have passed
either way.

**The grid is an overlay, not debug lines.** `Renderer::setDebugLines` is a
no-op in the RHI backend -- it warns once and discards -- so the editor's grid
has been invisible in this build all along. The sandbox grid is drawn as
viewport overlay strokes instead, the way frustums and wire boxes already are.
Implementing a line pass in the RHI renderer would fix the level grid too and is
worth doing; it is renderer work with its own risk and does not belong bolted
onto this.

Verified: isolation on a 12-child imported model and on the boss placeholder
(screenshots -- banner, scoped hierarchy, level hidden, framed camera, gizmos of
hidden entities gone); unpack turning the boss's sword into a selectable child
while still drawing it once; `scene_unpack_tests` proving drawn-set equality on
both the shipped piece and a synthetic three-level tree.

## Non-goals

- **No separate prefab files.** Isolation is a view of the open document. An
  instancing model with overrides is a different, larger feature.
- **No submesh editing.** A `.glb` is one asset; its parts are not addressable
  by the scene format. "Two models that must move together" is two entities, or
  a weapon on a socket — both of which this makes editable.
- **No change to the cook.** Isolation leaves no trace in the `.scn` or `.map`.
