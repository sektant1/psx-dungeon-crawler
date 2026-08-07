# A general-purpose menu authoring stack

**Date:** 2026-08-07
**Status:** design
**Supersedes nothing.** Extends the UI-as-scenes work of 2026-08-07
(`docs/ui-scenes.md`).

## The problem, in one file

`assets/scenes/ui/atoms/button.scn` is a `UiPanel` with a `UiLabel` reading
"OK". It cannot be clicked. There is no component that makes anything
clickable, no hover state, no press state, no disabled state, and no way for a
scene to say what activating it should mean.

That is the shape of the whole gap. The UI layer is a **painter** with exactly
one interactive concept bolted on — a `UiList` row — and the meaning of
activating a row lives in a hand-written C++ verb table in
`game/src/ui/UiScreens.cpp`:

```cpp
if (action == "reply")  { ... }
if (action == "seal")   { ... }
if (action == "sell")   { ... }
```

Three further limits follow from the same place:

- **`UiCanvas` has no texture primitive.** `UiIcon` is a solid coloured chip.
  No item icon, portrait, rarity frame or authored panel skin can be drawn at
  all.
- **`UiList` is a list widget, not a repeater.** It draws a line of text with an
  optional value and gauge. A trader offer — icon, name, price, condition bar,
  rarity border — is not expressible.
- **`eng::runtime::ProjectApp` draws no UI whatsoever.** Screens exist only in
  `game/src/main.cpp`. A project on this engine gets no menus.

## The goal

Make the editor a **general-purpose menu authoring tool** that any project on
this engine can use, targeting the class of screen that Escape from Tarkov and
Dark and Darker use for traders, quests and crafting: dense, image-heavy,
grid-based, clickable, drag-and-drop, several panes deep.

The success test is a workflow, not a feature list:

> Drop a PNG into a project, author a screen in the 2D editor, write a Lua
> function that answers its bindings and one that handles its actions, run the
> project, and the screen works — with **no C++ written and no engine verb
> added**.

## What stays

These are existing decisions that are right and that this design preserves.

- **A screen is a scene.** Same `.scn`, same cooker, same inspector, same
  instancing, same undo stack.
- **Anchors plus offsets.** No flexbox, no constraint solver. The one addition
  is `UiRepeat`, which generates cells — it does not introduce a solver.
- **Tones, not colours.** New components take a tone with an opt-in explicit
  colour escape hatch, exactly as `UiLabel` does.
- **No interaction state on the components.** Which widget is hovered, pressed,
  focused, scrolled or being dragged is *session* state and lives in a separate
  `eng::ui::UiInteraction` object. A screen that stored focus could not be
  reloaded without losing the player's place, and the existing header says so.
- **An unanswered binding falls back to the authored value.** This is what makes
  a screen previewable in an editor with no game behind it, and it extends
  unchanged to item-scoped bindings.
- **Virtual pixels and integer magnification.** Every addition is integer-snapped.
- **The rendered world image is frozen.** UI paints on imgui's foreground list
  after the PSX post chain. `make visual-test` must stay green.

## What gets added

Seven milestones, in dependency order. Each is independently useful and
independently reviewable.

---

## M1 — Images

Nothing else in this document can look like the reference genre without this.

### `UiCanvas::image`

`eng::rhi_texture_registry::load(path, filter, address, w, h)` already returns a
`uint64_t` token that the imgui backend accepts as an `ImTextureID` — this is
exactly how `BitmapFont` uploads its atlas
(`engine/src/render/rhi/BitmapFont.cpp:64`). An image primitive is therefore a
thin wrapper, not new rendering infrastructure.

```cpp
// engine/include/eng/ui/UiTextureCache.h
namespace eng::ui {
struct UiTexture { uint64_t token = 0; glm::ivec2 size{0, 0}; };

// Path -> token, loaded once and kept. Nearest filtering and clamped
// addressing are forced: a UI texture that filters is a UI texture that
// shimmers when the canvas magnifies it, and this canvas exists to be crisp.
class UiTextureCache {
public:
    const UiTexture& get(const std::string& path);   // {} on failure
    void clear();
};
}
```

A failed load yields a zero token. Painting a zero token draws a magenta
placeholder chip rather than nothing: a missing texture that renders as empty
space is a missing texture nobody notices until a player does.

`UiCanvas` gains:

```cpp
void image(glm::ivec2 at, glm::ivec2 size, const UiTexture& tex,
           glm::vec2 uvMin, glm::vec2 uvMax, unsigned int tint) const;
// Nine-slice: corners unscaled, edges stretched along one axis, centre both.
void imageSliced(glm::ivec2 at, glm::ivec2 size, const UiTexture& tex,
                 glm::ivec2 borderMin, glm::ivec2 borderMax,
                 unsigned int tint) const;
```

Nine-slice is what makes an authored panel skin possible without code, and it is
the single highest-leverage primitive here — every frame, slot, button plate and
window chrome in the target genre is a nine-slice.

### `UiImage` component (id 44)

```cpp
struct UiImage {
    std::string texture;        // resolved through the asset root
    glm::vec2 uvMin{0.0f, 0.0f};
    glm::vec2 uvMax{1.0f, 1.0f};
    int fit = 0;                // 0 Stretch 1 Contain 2 Cover 3 NineSlice
    // Nine-slice borders in source pixels. Two Vec2 rather than one Vec4
    // because FieldType has no Vec4 and appending one renumbers the enum that
    // ProjectComponents derives declared-component byte layout from -- the
    // trap that already cost this repo a debugging session when Vec2 was
    // added. Two Vec2 fields cost nothing and touch no enum.
    glm::vec2 borderMin{0.0f, 0.0f};
    glm::vec2 borderMax{0.0f, 0.0f};
    int tone = 0;
    glm::vec3 colour{1.0f, 1.0f, 1.0f};
    bool useColour = false;
    float opacity = 1.0f;
    // A data key naming a texture path, for an icon that changes with the item
    // in the slot. Wins over `texture` when it resolves, exactly as
    // UiLabel::binding wins over UiLabel::text.
    std::string binding;
};
```

`uvMin`/`uvMax` give atlas support with no atlas format: an item sheet is one
PNG and each icon is a sub-rect. A dedicated sprite-atlas asset can come later
without changing this component.

**Editor:** the inspector's `String` widget gains an opt-in file-picker variant
for texture fields, and the 2D viewport shows the image live.

---

## M2 — Interaction

### `UiInteract` component (id 45)

One component makes anything clickable. It is deliberately separate from
`UiPanel`/`UiImage`, so a clickable thing is a plate *with* an interact rather
than a special kind of plate.

```cpp
struct UiInteract {
    // What activating this means. The engine never interprets it; it carries
    // the string to the application, exactly as UiList::action already does.
    std::string action;
    // An argument carried with the action: a slot id, an item id, a tab name.
    std::string param;
    bool enabled = true;
    // A data key that can grey it out -- "trade.can_afford". Non-zero is
    // enabled. Falls back to `enabled` when unanswered.
    std::string enabledBinding;
    bool focusable = true;
    // Painted state, as tones. Hover and press are drawn as an overlay wash
    // over whatever the entity already paints, so a button, an image slot and
    // a repeat cell all get the same feedback with no per-widget code.
    int hoverTone = 2;      // Focus
    int pressTone = 2;
    int disabledTone = 1;   // Muted
    bool draggable = false;   // M5
    bool dropTarget = false;  // M5
};
```

### `eng::ui::UiInteraction`

Session state, owned by whoever drives a screen, never serialised.

```cpp
class UiInteraction {
public:
    struct Event {
        enum class Kind { Activate, Drop, Change };
        Kind kind = Kind::Activate;
        entt::entity entity = entt::null;
        std::string action, param;
        int item = -1;              // repeat index, or -1
        entt::entity source = entt::null;  // Drop only
        std::string sourceParam;           // Drop only
        std::string value;                 // Change only (text field)
    };

    // Pointer, keyboard and wheel against a solved layout. Returns true when
    // the input was consumed, which is what stops a click on a panel also
    // firing a weapon.
    bool update(const entt::basic_registry<entt::entity>& registry,
                const std::vector<UiSolvedRect>& solved,
                const UiDataSource* data, const Pointer& pointer,
                const Keys& keys);

    std::vector<Event>& events();     // drained by the caller each frame
    const State& state() const;       // hovered/pressed/focused, for paint
};
```

Input is passed in as a small POD (`Pointer{position, down, clicked, wheel}`,
`Keys{up,down,left,right,accept,back,typed}`) rather than an `eng::Input&`, so
the editor can drive the same interaction model from an imgui panel with no game
input system behind it. That is what makes M6's interaction preview possible.

**Focus navigation is spatial, not indexed.** `ui_up`/`ui_down`/`ui_left`/
`ui_right` move to the nearest focusable rect in that direction, scored by
centre distance projected on the axis. A grid of inventory slots is the case
this exists for; index-order navigation through a 6×8 grid is unusable.

### `paintUiScene` learns state

An overload takes `const UiInteraction::State*`. The existing signature stays
and forwards with `nullptr`, so no caller changes and a screen painted without
an interaction model simply has no hover.

### `eng::ui::UiScreenStack`

Moves the loading/opening logic out of `game/` and into the engine, and replaces
"exactly one screen at a time" with a stack.

```cpp
class UiScreenStack {
public:
    bool load(const std::string& id, const std::string& mapPath,
              const eng::ecs::ComponentRegistry& types);
    void push(const std::string& id);
    void pop();
    void clear();
    const std::string& top() const;
    bool anyOpen() const;
    bool pausesGame() const;

    bool handleInput(const Pointer&, const Keys&);
    void draw(UiCanvas&, glm::vec2 displayPixels, const UiDataSource*);
    std::vector<UiInteraction::Event> takeEvents();
};
```

Every screen in the stack **paints**; only the top one takes input. That is the
definition of a modal, and it is why a stack is now worth its complexity where
the original note correctly said it was not: a purchase-confirm over a trader
screen is exactly something opening over something else.

`game::UiScreens` is rewritten as a thin adapter over this, keeping the game's
existing three screens and its verb table working. The verbs stay in `game/`
where they belong; the engine gains none.

---

## M3 — The repeater

The structural limit `docs/ui-scenes.md` names as the one left.

### `UiRepeat` component (id 46)

```cpp
struct UiRepeat {
    std::string source;     // "trade.stock"
    int maxItems = 64;
    int direction = 0;      // 0 Vertical 1 Horizontal 2 Grid
    int columns = 4;        // Grid only
    glm::vec2 cell{48.0f, 48.0f};
    glm::vec2 gap{2.0f, 2.0f};
    bool scrollable = true;
};
```

**The entity's first child is the template.** The solver, on meeting a
`UiRepeat`, asks the data source how many items the source has, then emits the
template subtree's solved rects once per item, each offset to its cell and
tagged with the item index. The template child itself is not emitted separately.

`UiSolvedRect` gains `int item = -1`. One entity therefore appears many times in
the solved list with different bounds and different item indices, which is the
whole trick: no entities are created, nothing is cloned, and paint and hit-test
walk the same flat list they already walk.

### Item-scoped bindings

`UiDataSource` gains three methods, all defaulted so no existing source breaks:

```cpp
virtual int  itemCount(std::string_view source) const { return 0; }
virtual bool itemText(std::string_view source, int index,
                      std::string_view key, std::string& out) const { return false; }
virtual bool itemNumber(std::string_view source, int index,
                        std::string_view key, float& out) const { return false; }
```

Inside a repeat, a binding resolves against the item first. A label bound to
`name` inside a repeat over `trade.stock` asks
`itemText("trade.stock", i, "name", out)`. A binding that starts with `/`
escapes the scope and resolves globally, so a repeat cell can still show the
player's gold.

This is the per-row binding scope, and it is what makes an offer card authorable
once and drawn sixty times.

### Clipping and scroll

A scrollable repeat clips its children to its own rect and offsets cells by a
scroll amount held in `UiInteraction` (session state, not a component). The
wheel over it scrolls; focus navigation past the visible edge scrolls to follow.
`UiCanvas` already has private `pushClip`/`popClip`; they become usable by the
scene painter.

`UiList` is **kept**, not replaced. A list of plain text lines should not need a
template child, and every existing screen uses it.

---

## M4 — Lua, and screens in `ProjectApp`

This is the milestone that makes the stack project-agnostic rather than
this-game-specific. **The engine ships zero verbs.**

### The `ui` module

```lua
-- screens
ui.open("trader")            -- push a screen declared in project.toml
ui.close()                   -- pop
ui.close_all()
ui.top()                     -- id, or nil

-- answering bindings
ui.number("player.gold",      function() return gold end)
ui.text  ("trader.name",      function() return trader.name end)
ui.rows  ("trade.stock",      function() return stock_rows() end)  -- UiList
ui.items ("trade.stock",      function() return offers end)        -- UiRepeat

-- handling actions
ui.on("buy", function(ev)  buy(ev.param, ev.item)  end)
ui.on_drop(function(ev)    move(ev.source_param, ev.param)  end)

-- the escape hatch, for what bindings cannot express
ui.set_visible("trader", "quest_tab", false)
```

`ui.items` returns a Lua array of tables; a binding key inside the repeat is a
key in that table. `{ name = "Iron Sword", price = 120, icon = "icons/sword.png",
condition = 0.8 }` answers `name`, `price`, `icon` and `condition` with no
schema declared anywhere.

`eng::ui::ScriptUiData : UiDataSource` bridges these callbacks. Unanswered keys
fall back to authored values, as everywhere else.

### `project.toml`

```toml
[ui]
font = "antiquity.toml"
authored_size = [320, 240]

[[ui.screen]]
id = "trader"
scene = "scenes/ui/trader.scn"
action = "open_trader"      # optional input binding that toggles it
pauses = true
```

`ProjectApp` loads these, owns a `UiScreenStack` and a `ScriptUiData`, draws the
stack each frame, and routes input to it before the player controller — so an
open screen freezes the player without the project writing that rule.

---

## M5 — Drag and drop, and a text field

### Drag and drop

`UiInteract::draggable` and `::dropTarget`. `UiInteraction` tracks a drag
payload `{entity, param, item}` from press-and-move on a draggable, paints the
dragged cell following the cursor, and on release over a drop target emits a
`Drop` event carrying both sides' `param` and item index.

The engine carries no meaning: it does not know what an item is, whether the
move is legal, or what a slot holds. It reports "this param was dropped on that
param" and the script decides. That is the same seam `action` already uses and
it is the whole of the Tarkov grid.

### `UiTextField` component (id 47)

Deliberately minimal — single line, caret, backspace/delete, home/end, enter to
commit, escape to revert. **No selection, no clipboard, no IME.** A quantity box
and a search box are what the genre needs; a text editor is not.

```cpp
struct UiTextField {
    std::string placeholder;
    std::string binding;   // initial value
    int maxLength = 64;
    int filter = 0;        // 0 Any 1 Digits 2 Decimal
    int tone = 0;
};
```

The live value lives in `UiInteraction` (session state) and is published on
commit as a `Change` event.

---

## M6 — The 2D editor

Today the 2D viewport does three things: select, move, resize
(`editor/src/ui/UiSceneEditor.cpp:162`). Everything else goes through the
generic inspector. In priority order:

1. **Widget palette.** The atom library (`assets/scenes/ui/atoms/atoms.json`,
   twelve atoms today) becomes a click-to-place strip in the 2D tab, extended
   with the new components. Placing an atom is the existing instancing path.
2. **Anchor presets.** Dragging deliberately never writes anchors, which is
   correct and leaves no way to *set* them but by typing four floats. A
   nine-box popover (Unity's) writes the common cases; a modifier-drag on the
   anchor gizmo writes the rest.
3. **Mock data.** A `<screen>.mock.json` beside the scene supplies numbers, text
   and item rows. **Without this a repeater previews as one empty cell**, which
   tells the author nothing — this is not polish, it is what makes the repeater
   authorable at all. The editor's data source reads it; the game never does.
4. **Interaction preview.** A play toggle in the 2D tab drives `UiInteraction`
   from the imgui panel's own mouse, so hover, press, disabled, focus, scroll
   and drag are all visible while authoring. Actions are surfaced as a log line
   rather than executed — the editor has no game to run them against.
5. **Align and distribute** for multi-selection, and **rulers, snapping and
   guides**.
6. **Responsive check.** A control to preview at several virtual sizes, since
   anchors exist precisely for that and today it is untestable without resizing
   the window.

---

## M7 — Reference screens and documentation

A trader screen, a quest log and a crafting screen, authored in a sample
project, driven entirely by Lua and TOML, exercising images, nine-slice chrome,
a repeater grid, drag and drop, tabs, a modal confirm and a quantity field.

They are the acceptance test: if any of them needs a line of C++, the design
failed.

Documentation updates: `docs/ui-scenes.md` (components, bindings, actions,
repeat scope), a new `docs/ui-interaction.md` (the interaction model, the event
kinds, focus navigation), `docs/projects.md` (`[ui]` and `[[ui.screen]]`),
`docs/scripting.md` (the `ui` module), and a tutorial: **how to build a trader
screen from nothing.**

---

## Component id allocation

44 `UiImage`, 45 `UiInteract`, 46 `UiRepeat`, 47 `UiTextField` — continuing the
38–43 block, one feature, contiguous.

## Testing

Every milestone lands with tests in the layer that already has them.

- **`engine/tests/UiSceneTests.cpp`** — repeat expansion (count, cell geometry,
  grid wrap, clipping), item-scoped binding resolution and the `/` escape,
  fallback when a source answers nothing, nine-slice rect arithmetic.
- **New `engine/tests/UiInteractionTests.cpp`** — hover/press/release ordering,
  spatial focus navigation over a grid, disabled gating, event emission and
  ordering, drag payload lifecycle, drop event fields, text field editing and
  commit/revert.
- **`editor/tests/UiSceneEditorTests.cpp`** — anchor presets preserve the
  resolved box where they should, align/distribute arithmetic, mock data
  loading, handles target the template rather than a repeat instance.
- **Screenshot runs** via `RAVEN_OPEN_SCREEN`, extended to the new reference
  screens — the repo's own rule is that a change which compiles is not a change
  that works, and every one of these milestones is visual.

## Risks

- **Draw call volume.** A 6×10 grid of nine-sliced cells is 540 quads on the
  imgui foreground list. Acceptable — imgui batches by texture, and one atlas
  keeps it to a handful of draws. Measured before optimised, per the repo rule.
- **Editor handles inside a repeat.** One author id maps to many boxes. Handles
  are offered only on item 0 and edits write the template, so dragging a cell
  edits the card rather than an instance.
- **Scope.** Seven milestones is a lot. They are ordered so that stopping after
  any one leaves the engine strictly better and nothing half-built: M1 alone
  gives icons, M2 alone gives buttons, M3 alone gives grids.
