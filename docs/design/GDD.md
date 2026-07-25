# Game Design Document

_Working title: **untitled PSX dungeon crawler**_
_Living document. Player-facing intent only — engine implementation lives in [GEDD.md](GEDD.md)._
_Started 2026-07-24._

---

## 1. High concept

A **single-player, first-person, fast-paced fantasy dungeon crawler** with a
**living village hub** and **procedurally generated "raid" dungeons**. You are a
delver who returns again and again to a surface village that slowly grows around
you, then descends into a hostile, mysterious dungeon to fight, loot, and
survive — one run at a time, deeper each attempt, at real risk of losing what you
carried in.

The pitch in one line: **Escape from Tarkov's loop, run solo, as a Boltgun-fast
Dark Messiah melee/magic/archery FPS, dropped into a Goblin-Slayer dungeon under
a Pathologic-2 village.**

### Reference DNA

| Pillar | Reference | What we take |
|---|---|---|
| Loop | Escape from Tarkov (solo) | Prep → raid → extract-or-die → stash & upgrade. Risk of loss. |
| Hub / progression | Gothic 2, Tarkov hideout, Pathologic 2 | A village that changes; a house you upgrade; NPCs whose lives advance. |
| Movement & feel | Dark Messiah of M&M, DOOM/Boltgun | Momentum, kicks, environmental kills, weighty fast FPS. |
| Combat gunfeel | Warhammer 40K: Boltgun | Punchy, fast, readable, arcade-lethal — applied to melee/spell/bow. |
| Enemies & bosses | Souls-likes (in FPS) | Readable tells, stamina/spacing, boss set-pieces, hard but fair. |
| Dungeon vibe | Goblin Slayer | Grimy, tactical, lethal low-fantasy; goblins are a real threat en masse. |
| Quests | Old School RuneScape | Handcrafted, quirky, memorable multi-step quests over quantity. |
| Faction side content | Gothic 2 factions | Organic reputation: help a faction → they help you → unlock passage. |
| Replayability | Vampire Survivors / Megabonk | Run variety and build churn — but stretched over a *long* campaign. |

### What it is NOT

- Not multiplayer. Not PvP. The "Tarkov loop" is the *structure*, not the sockets.
- Not a twin-stick or third-person game. First-person always.
- Not a short roguelike run (5–30 min). Runs are long, and the meta-campaign is
  measured in dozens of hours.
- Not a story-on-rails RPG. Narrative is environmental and faction-driven.

---

## 2. Design pillars

1. **Every descent is a risk.** You choose what to carry in. Dying in the dungeon
   can cost it. Tension comes from *voluntary* commitment, not artificial timers.
2. **Fast, physical, lethal combat.** Melee, casting, and archery are all
   first-class and *fast*. Fights are won by movement, spacing, and reads — not
   bullet-sponge attrition. Enemies die fast; so do you.
3. **The village remembers.** The hub is not a menu. It is a place that grows,
   whose people change, and whose factions open doors for you deep in the dungeon.
4. **Depth over floors.** Going deeper is the meta-goal. Each descent milestone
   (floors, checkpoints, bosses) is a durable achievement, gated by skill *and*
   standing.
5. **Handcrafted moments in a procedural shell.** The dungeon is generated, but
   quests, bosses, mysteries, and faction beats are authored and seeded into it.

---

## 3. Core loop

```
  ┌────────────────────── VILLAGE (hub) ──────────────────────┐
  │  talk to NPCs · take quests · trade · craft · upgrade house │
  │  manage stash · choose loadout · pick a descent target      │
  └───────────────────────────┬────────────────────────────────┘
                              │ commit loadout, descend
                              ▼
  ┌───────────────────── DUNGEON (the "raid") ─────────────────┐
  │  generated floors · fight · loot · solve mysteries          │
  │  progress quests · reach a checkpoint or deeper             │
  │  DECIDE: push deeper (more risk) or extract (bank the run)  │
  └───────────────┬──────────────────────────┬─────────────────┘
        extract   │                          │  die
                  ▼                          ▼
       keep everything looted        lose unsecured carry
                  │                          │
                  └──────────┬───────────────┘
                            ▼
                   VILLAGE advances (NPCs, factions, house)
```

### Loop beats

- **Prep (village).** Read the board / talk to NPCs, accept quests, buy/sell,
  craft ammo & consumables, upgrade the house (hideout), pick a *descent target*
  (how deep, which faction's territory, which quest).
- **Commit.** Choose a loadout. Gear you bring is at risk. This is the Tarkov
  "insurance vs. loss" decision, single-player-flavored.
- **Descend (dungeon).** Enter a procedurally generated dungeon seeded with the
  authored content relevant to your target. Fight, explore, loot, progress quest
  objectives, uncover mysteries.
- **The push/extract decision.** At any checkpoint you may extract (bank the run —
  everything carried becomes yours, quests advance) or push deeper for greater
  reward at greater risk of losing it all.
- **Consequence.** Extract → stash grows, village advances. Die → lose unsecured
  carry (secured/insured/quest items follow separate rules — see §7).
- **Advance.** The village state ticks forward: NPC storylines progress, faction
  standing updates, new quests and hideout upgrades unlock. Repeat, deeper.

---

## 4. The village (progressive hub)

The village is a **persistent, evolving place**, not a menu screen.

### 4.1 Your house (hideout)

- A player-owned building you **upgrade over the campaign** (Tarkov hideout model).
- Upgrade **stations** unlock crafting, storage, healing, ammo/potion production,
  training, and passive generation while you're in the dungeon.
- Upgrades cost dungeon loot + faction favors, gating progression behind descents.
- The house is the **safe stash**: what's stored here is never lost on death.

### 4.2 The villagers (Pathologic-2 progression)

- NPCs have **advancing lives**: their state, dialog, location, and availability
  change as the campaign progresses and as you help or neglect them.
- Some storylines are **time/progress-gated** — help someone now or lose the
  thread. Not real-time-clock pressure, but *campaign-state* pressure.
- NPCs are questgivers, vendors, crafters, trainers, and faction representatives.
  The village population is the campaign's narrative and service layer.

### 4.3 Village services

- **Vendors / trade** — sell loot, buy gear & ammo; stock evolves with your
  progress and faction standing.
- **Crafting** — turn dungeon materials into ammo, potions, gear, upgrades.
- **Training** — spend resources to raise skills / unlock abilities.
- **Quest board + NPC quests** — the entry points to authored content.

---

## 5. Combat

Fast, physical, lethal, first-person. Three coexisting damage identities the
player fluidly mixes — **there are no rigid classes**; you carry what you bring.

### 5.1 The three vectors

- **Melee (Dark Messiah).** Momentum-driven. Kicks, shoves, environmental kills
  (spikes, ledges, hazards), weapon weight and reach matter. Positioning is the
  skill.
- **Casting.** Fast, punchy spells (existing SpellSystem: fireball / beam, growing).
  Elemental identities, resource-costed, with utility and control roles, not just
  damage.
- **Archery.** Ranged precision and pacing — bow/crossbow, ammo as a managed
  resource, headshots and weak-points rewarded.

### 5.2 Feel targets

- **Gunfeel = Boltgun.** Every hit reads: strong hit-feedback, fast time-to-kill
  both ways, arcade lethality. No spongey enemies.
- **Weighty but fast.** High mobility, but attacks and impacts have heft.
- **Readable.** Enemy attacks telegraph; the player's job is to read and react.

### 5.3 Enemies & bosses (Souls-like, in FPS)

- Enemies have **tells, spacing, and stamina-like commitment** — you win by
  reading wind-ups and punishing recovery, at FPS speed.
- **Goblin-Slayer dungeon fauna:** goblins are a genuine swarm threat — numbers,
  ambushes, traps; low-fantasy grime over high-fantasy pageantry.
- **Bosses are set-pieces:** authored, multi-phase, arena-driven, hard-but-fair.
  A boss kill is a durable campaign milestone.

### 5.4 Underlying model (already built)

Data-driven damage system: Health / Resistances (percentage, clamped) / Faction /
StatusEffects (burn/DoT, CC gates) / crit rolls resolved deterministically.
Weapons defined in `weapons.toml`. This is the engine the above feel is tuned on —
see [GEDD.md](GEDD.md) §7.3 for implementation.

---

## 6. The dungeon (procedural "raid")

- **Procedurally generated** floors (BSP generator exists) — layout differs every
  descent, so the *space* is always fresh.
- **Descent by floors.** You go deeper; depth is the primary progression axis.
  Milestone floors (e.g. floor 10/20/30…) act as **checkpoints / gates**.
- **Authored content seeded in.** Quests, bosses, mysteries, faction territories,
  and set-pieces are hand-designed and injected into the generated shell so runs
  have handcrafted beats without handcrafted layouts.
- **Mysteries & exploration.** Secrets, lore, optional rooms, environmental
  puzzles — rewards for the curious and the careful.
- **Hardcore.** High lethality, meaningful resource pressure, real loss on death.
- **Vibe:** Goblin Slayer — grimy, tactical, lethal, low-fantasy.

### Checkpoints & passage

Deep floors have **faction-controlled checkpoints**. Your standing with a faction
determines whether you get **free passage** at their checkpoint (e.g. floor 30),
turning slow re-clears into fast re-entry deep into the dungeon. This is the
mechanical payoff of the faction reputation loop (§8).

---

## 7. Risk, loss & progression economy

The Tarkov loop, single-player.

- **Carry vs. stash.** Items in your **house stash are safe**. Items you **carry
  into the dungeon are at risk.**
- **Extract to bank.** Reaching an extract/checkpoint **secures** the run's loot
  into your stash.
- **Death costs the carry.** Dying loses unsecured carried gear/loot. (Design
  space for *secured slots / insurance / quest-item protection* to soften the
  hardcore edge — tuned in balancing.)
- **Persistent meta-progression** across runs: stash wealth, house upgrades,
  skills/abilities, faction standing, quest state, unlocked depth. This is what
  makes a "run" long-tail: you are always building the campaign, not just the run.

### Replayability

The Vampire-Survivors/Megabonk churn — build variety, loot rolls, procedural
layouts, run-to-run decisions — but **stretched across a long campaign** rather
than 20-minute loops. Novelty per descent; mastery across dozens of hours.

---

## 8. Quests & factions

### 8.1 Quests (OSRS model)

- **Handcrafted, memorable, multi-step** quests over infinite radiant filler.
- Quirky, characterful, occasionally puzzle-like; each quest is an authored
  experience with a beginning, obstacles, and a payoff.
- Delivered by NPCs and the village board; objectives play out in the dungeon.

### 8.2 Factions (Gothic-2 organic reputation)

The signature side-content system. Reputation is **earned organically through
play**, and it **pays back mechanically**:

```
help a faction  →  faction standing rises  →  faction helps you back
     (quests,           (reputation,               (free checkpoint
   deliveries,          unlocked NPCs,               passage, vendors,
   territory)           dialog, services)            allies, deep access)
```

- Helping one faction may cost standing with a rival — choices have weight.
- The concrete payoff: **free passage at that faction's deep-dungeon checkpoint**
  (e.g. clear their favor → walk through the floor-30 gate instead of fighting it).
- Factions gate access, services, allies, and areas — the Gothic-2 "which camp do
  you join" tension, expressed through the dungeon's depth gates.

---

## 9. Player-facing acceptance criteria (design targets)

These are the design bars the build is measured against as features land.

- **Loop is legible:** a new player understands prep → descend → push/extract →
  advance within the first session.
- **Commitment is felt:** players hesitate before descending with good gear —
  the risk is real and understood.
- **Combat reads at speed:** a player can identify and react to an enemy wind-up
  at full movement speed; kills (both ways) feel earned and punchy.
- **The village visibly changes:** returning from a descent, the player can point
  to something new (an NPC moved, a house station unlocked, a faction shifted).
- **Depth is a trophy:** reaching a new deepest floor / boss feels like a durable
  milestone worth telling someone about.
- **Factions pay off concretely:** the player experiences a checkpoint they once
  had to fight become free passage because of standing they earned.

---

## 10. Open design questions

Tracked, not yet decided:

- **Loss softening:** exact secured-slot / insurance / quest-protection rules for
  the hardcore death cost. How punishing, how forgiving?
- **Skill/ability system shape:** class-less loadout (bring-what-you-want) vs.
  unlockable skill trees vs. hybrid.
- **Descent length:** how long is a "typical" descent, and how are extracts /
  checkpoints spaced to make the push/extract decision recur meaningfully?
- **Time/campaign-state model** for Pathologic-2 NPC progression: what ticks the
  village forward — descents completed, quests done, in-game days?
- **Death & the village:** does a death advance the world (Pathologic-style) or
  only pause the run?
- **Economy sinks/sources:** what keeps stash wealth meaningful over a long
  campaign so late-game trade still matters?

---

## 11. Current build vs. vision

Where the code is today relative to this document (see [GEDD.md](GEDD.md) §
"Current state" for engine detail):

| System | Vision | Built today |
|---|---|---|
| FPS movement | Dark Messiah momentum + kicks | Basic WASD + mouse-look controller |
| Combat | melee/cast/archery, Boltgun feel | Data-driven damage model, spells (fireball/beam), melee, projectiles, wired to a training dummy |
| Dungeon | seeded procedural + authored content | BSP generator → layout → 3D world; seed-stack descend/ascend; portals |
| Village hub | living, upgradeable, factioned | Static lobby/showcase set-dressing only |
| Quests / factions | OSRS quests + Gothic-2 reputation | Not started |
| Risk/loss economy | Tarkov carry/stash/extract | Not started |
| Bosses | Souls-like set-pieces | Not started |
| Content pipeline | data-driven archetypes (TOML) | Foundations exist (weapons/particles/dungeon TOML, .map editor) |

The **engine is far ahead of the game**: the tech to build all of the above
largely exists. This GDD is the target the (thin) game layer grows toward.
