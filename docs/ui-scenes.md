# UI scenes {#doc-ui-scenes}

Screen-space UI authored as entities, edited in the 2D viewport, drawn by the
same solver in the editor and the game.

For the components see `eng/ecs/components/UiComponents.h`; for the runtime,
`eng/ui/UiScene.h`; for the canvas underneath it all, `eng/ui/UiCanvas.h`.

## Why

Every surface in this engine used to be laid out in C++ against `UiCanvas`. That
is fine for a HUD -- four widgets that never move -- and it is why the editor's
2D viewport described itself as "not a WYSIWYG layout editor": there was nothing
to edit, because the HUD *is* code.

It stops being fine at the first inventory screen. A screen is dozens of boxes
whose positions are a design question, and answering a design question by
editing a `.cpp` and waiting for a link is how UI work stops happening.

So a screen is a scene. Same `.scn`, same cooker, same inspector, same
instancing, same undo stack.

## The components

| Component | What it is |
|---|---|
| `UiRect` | the layout box. Every UI entity has one. |
| `UiPanel` | a filled or framed plate |
| `UiLabel` | text, with an optional data binding |
| `UiBar` | a proportional gauge, with an optional binding |
| `UiIcon` | a solid chip |
| `UiList` | repeating rows of live data |

### Anchors and offsets

`UiRect` resolves against its parent's *resolved* box:

```
min = parent.min + parent.size * anchorMin + offsetMin
max = parent.min + parent.size * anchorMax + offsetMax
```

Two cases cover everything:

- **Equal anchors** pin a fixed-size box to one spot. `anchorMin = anchorMax =
  (0,0)`, offsets in pixels from the top-left.
- **Spread anchors** stretch a box with its parent. `anchorMin = (0,0)`,
  `anchorMax = (1,1)`, offsets as inset margins -- a negative max offset is a
  margin from the far edge.

Units are **virtual pixels** -- the canvas grid, not the window's -- so a layout
authored once is pixel-exact at every window size and integer scale.

Hiding an entity prunes its whole subtree from both painting and hit-testing. A
screen is switched on and off by one flag on its root.

## Bindings

A scene cannot know the player's weight or a shop's prices, so it names a key
and the game answers it through `eng::ui::UiDataSource`:

```json
{ "bar": { "binding": "player.health_fraction" } }
{ "list": { "source": "inventory.backpack", "maxRows": 12 } }
```

An **unanswered binding falls back to the authored value**. That is what makes
the same scene previewable in an editor with no game behind it: the author sees
their placeholder text, the game shows the live number.

## Authoring

In the editor: **Add Component -> UI element**, then the 2D tab. Click to
select, drag the body to move, drag a corner or edge to resize. A whole drag is
one undo entry.

Dragging writes **offsets only, never anchors**. An anchor is a layout decision
the author made; a drag must not silently change it. Dragging a stretched box
therefore adjusts its margins, which is what "move this 4px right" means for a
box pinned to both edges.

Or by hand:

```json
{
  "id": "pack_panel",
  "ui": {
    "rect": { "anchorMin": [0,0], "anchorMax": [1,1],
              "offsetMin": [24,20], "offsetMax": [-24,-20] },
    "panel": { "style": 0, "rail": 1 }
  }
}
```

The six components are grouped under one `ui` key when authored, and cooked out
as the six independent components they are. That grouping is an editor
convenience only -- the ECS, the field tables and Lua all see normal components.

## The font

`[ui] font` in `game.toml` names a metrics file in `assets/fonts`, generated
from a TTF by `tools/gen_font_atlas.py`. The runtime never reads a TTF, so text
measurement is identical on every machine. The editor's 2D panel loads the same
face, because a layout that fits in one font and not another is exactly what
that panel exists to catch.

## Bindings this game answers

`game/src/ui/GameUiData.cpp` holds the vocabulary in one table per kind, so
"what can a screen bind to" is a list rather than a grep.

| Numbers | Text | Rows |
|---|---|---|
| `inventory.weight_fraction` | `inventory.weight` | `inventory.backpack` |
| `inventory.slot_fraction` | `inventory.currency` | `inventory.stash` |
| `raid.at_risk_fraction` | `raid.phase` / `raid.depth` | `trade.stock` |
| `raid.extraction_progress` | `raid.at_risk` / `raid.seals` | `dialogue.choices` |
| `character.level_progress` | `character.level` | `quests.active` |
| | `dialogue.speaker` / `dialogue.text` | `raid.at_risk_items` |

## Actions

A `UiList` names what activating a row does. The engine carries the string; the
game maps it to a verb (`game/src/ui/UiScreens.cpp`), so another game's screens
name their own actions and share none of these.

| `action` | On the selected row |
|---|---|
| `reply` | take that dialogue choice; closes the screen when the tree ends |
| `buy` | buy one from the trader's shelf |
| `sell` | sell one from the pack to the current trader |
| `seal` | toggle a seal against the next death (see LossPolicy) |
| `use` | equip it, if it is equipment |

Authored rather than derived from `source`, because the same source means
different things on different screens: the backpack is what you *seal* on the
inventory screen and what you *sell* on the trade screen.

**Focus.** Every list declaring an action is focusable; `ui_left`/`ui_right`
move between them, and only the focused list marks a selected row -- a screen
that highlighted the same index in both columns would tell the player nothing
about where a keypress lands. Tab is deliberately not the focus key: it already
opens the pack, and a key meaning two things depending on what is on screen is
a key nobody trusts.

**Mouse.** Hovering moves the cursor and the focus, so a click on the other
column needs no keypress first; clicking activates. A pointer over an open
screen always consumes the input, even where there is nothing actionable under
it -- a click falling through a panel and firing a weapon is the worst bug this
layer can have.

Dim wins over selected when a row draws: in a shop, dim means you cannot afford
it, and that does not stop being true because the cursor moved onto it. The
plate behind the row is what shows the cursor.

## Showing a screen

`game::UiScreens` loads each cooked screen into a registry of its own -- not
merged into the level, because a screen is not part of the world and must
survive a level change. Exactly one is open at a time; a stack would only earn
its complexity when something opened over something else.

An open screen freezes the player (`playerDriven()`) and consumes input, so a
keypress that closes the pack cannot also swing a weapon. Bindings are
`inventory`, `trade`, `ui_back`, `ui_up`, `ui_down` in `game.toml`.

Talking to an NPC opens the right screen: a conversation if they have a tree, a
shop if they have stock and no tree -- somebody with goods and nothing to say is
a shopkeeper, not a mute. Closing a dialogue screen ends the conversation, or
the RPG layer would believe the player is still mid-sentence.

`RAVEN_OPEN_SCREEN=inventory|trade|dialogue` opens one at startup, for
screenshot runs that have no way to press a key;
`RAVEN_OPEN_SCREEN_TRADER=<id>` picks whose shelf.

## Cooking

The parent link survives the cook for UI entities. Ordinarily the cooker bakes
the hierarchy into world transforms and drops it -- correct for a level, and
silently wrong here: a `UiRect` resolves against its *parent's* resolved box and
has no world transform to bake in its place, so dropping the link does not
flatten the hierarchy, it deletes it, and every element lands against the screen
corner instead of inside the panel it was authored in.

## What this deliberately is not

- **No flexbox or constraint solver.** Anchors plus offsets is Unity's
  RectTransform model and covers every layout this game has.
- **No stylesheet cascade.** Tones name a role in the active palette, so a theme
  swap touches no entity.
- **`UiList` is a list widget, not a repeater.** It asks the data source for
  rows and draws them with its own styling; it does not clone a template child
  per row. A real repeater needs a per-row binding scope, and every list here is
  a line of text with an optional value and gauge. `rowHeight` and the tones are
  the seam if that stops being true.
- **No input focus on the components.** Which widget is selected is gameplay
  state; a screen that stored it could not be reloaded without losing the
  player's place.
