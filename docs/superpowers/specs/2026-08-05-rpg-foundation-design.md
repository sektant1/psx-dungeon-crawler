# RPG foundation — design

*2026-08-05. The game layer's missing spine: stats, items, inventory, loot,
quests, dialogue, world state, and one save that covers all of it.*

## Why now

`docs/design/GEDD.md` §12 and `GDD.md` §11 both say the same thing, and the code
agrees with them: **the engine is far ahead of the game.** Combat, projectiles,
enemies, dungeon generation, viewmodels, the asset pipeline and the editor are
built. Everything the design documents call the *meta-loop* is not:

| AGENTS.md | State before this work |
|---|---|
| §14 Extraction and Inventory | nothing |
| §15 Economy | nothing |
| §16 Major NPC Progression | nothing |
| §20 Quest Design | nothing |
| §32 Save System | enemies only (`enemy/EnemySave.h`) |

The one place it shows through as a *bug* rather than an absence: the editor
offers a **Pickup** entity, `SceneCook` writes it into the `.map`, and
`MapRuntime::pickupPlacements()` reports it — and nothing reads that call. An
author can place an item, cook it, load it, and no item exists. The authoring
path was built end to end and stops one function short of the game.

So this is not "add an RPG on the side". It is finishing the half of the game
layer the engine was already shaped for.

## What this builds

A new `game/src/rpg/` module, in the shape the rest of `game/src` already uses:
a pure data/logic core that compiles with no renderer, no physics and no
registry, plus thin runtime glue that owns the parts that need a world.

```
rpg/RpgTypes.h      the shared vocabulary: slots, categories, stat fields,
                    conditions, effects, typed events
rpg/Stats.*         attributes -> derived stats, the modifier layer, XP curve
rpg/Items.*         ItemLibrary + LootLibrary (items.toml, loot.toml)
rpg/Inventory.*     containers, stacking, weight, equipment slots
rpg/WorldState.*    flags, counters, NPC standing, day, currency, event bus
rpg/Quests.*        QuestLibrary + QuestLog (objectives advanced by events)
rpg/Dialogue.*      DialogueLibrary + DialogueRunner
rpg/RpgSave.*       one versioned snapshot + codec over all of the above
rpg/PickupSystem.*  world pickups: authored placements and loot drops
rpg/RpgRuntime.*    the one object the app owns; wires the rest together
```

## Decisions

**1. Derived stats are never stored as truth.** `CharacterSheet` owns base
attributes and a *list* of modifiers, each tagged with the source that pushed
it (`equipment:iron_hauberk`, `effect:blessing`). `derive()` recomputes the
whole derived block from scratch every time. Unequipping pops by source id.
This is the single most common way an RPG save corrupts itself — a `+10 vigor`
that was added to the base attribute and subtracted twice — and it is designed
out rather than tested for.

**2. The RPG layer does not own the combat pools.** `Health`, `Stamina`,
`Mana`, `Poise` and `Resistances` already exist as combat components and
already have systems ticking them. `stats::applyTo()` writes the *maxima* onto
those components and preserves the current/max ratio. Combat stays the
authority on what a hit does; the sheet is the authority on how big the pools
are. No second health model.

**3. One event vocabulary, typed.** AGENTS §28: *"Avoid string-based event names
for critical behavior."* `EventKind` is an enum; the subject is an
`eng::StringId` (already the engine's 64-bit hashed name, `constexpr` from a
literal). A quest objective is `{kind, subject, needed}` and matching is two
integer compares. Quests, NPC progression and the tutorial all read the same
stream.

**4. Items inherit like enemies do.** `[archetype.<id>]` / `[item.<id>]` with
`inherits`, field-wise and transitive — the exact idiom `EnemyLibrary` already
established, down to the `shared_ptr<const ItemDef>` handle so a hot-reload
does not dangle the stack an inventory is holding. Adding an item is adding a
table; there is no registration call and no C++.

**5. Three carry contexts, one container type.** AGENTS §14 wants stash,
loadout and carried findings. They are the same `Container` with different
limits, plus a `foundThisRun` flag on the stack — which is all "carried
findings" mechanically *is*, and it means the death-loss rule is one predicate
rather than a third storage class. Weight is the only carry dimension turned on
(§14: "do not activate all these dimensions simultaneously").

**6. Conditions and effects are shared between quests and dialogue.** Both need
"has the player got X / is quest Y done / set this flag / give this item". One
`Condition`/`Effect` pair in `RpgTypes.h`, evaluated once in `RpgRuntime`. A
dialogue choice and a quest prerequisite are the same data.

**7. Dialogue is data, not a VM.** Nodes, lines and choices in TOML with
conditions and effects. `eng_script` (Lua) exists and is the right home for a
*scripted encounter*; a conversation tree is not one, and putting it there
would make every NPC line a hot-reloadable script file for no gain. If a line
ever needs to run arbitrary logic, an effect that publishes a typed event is
the seam — the script host already listens to those.

**8. One save, versioned, in the `EnemySave` shape.** Plain snapshot struct →
pure codec → `capture`/`restore` that need a live game. The codec is what gets
versioned and exhaustively tested with no renderer in the process. References
are by *id string*, never index, because a save outlives the content it was
made against (AGENTS §32).

## What this does not build

Named honestly, because AGENTS §36 asks for scope restraint:

- **No village hub scene.** The systems a village needs (NPC state, standing,
  quests, trade values, day counter) are here; the level is content.
- **No crafting or alchemy recipes.** `ItemCategory::Reagent` and the tag set
  exist so recipes have somewhere to land.
- **No extraction rules.** `foundThisRun` and the death hook are the two seams
  the Tarkov loop needs; the policy (what is lost, what a seal protects) is one
  function that has not been written because the expedition scene has not.
- **No NPC schedules.** §17 wants time-driven village events; `WorldState::day`
  advances and is saved, and nothing reads it yet.

## Verification

- Six unit tests over the pure layer (`ctest`): stats and the modifier layer,
  item inheritance, inventory weight/stacking/equip, loot determinism, quest
  objective advancement, dialogue conditions, save round-trip and rejection of
  a truncated or wrong-version blob.
- `assetlint` and the shipped-content tests read the new TOMLs the same way
  they read `enemies.toml`.
- On screen: `RAVEN_SCREENSHOT` with a pickup in the level, which is the only
  evidence that the authored-Pickup path actually closed.
