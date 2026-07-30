  ## Game Engine Non-Negotiables:

  - Try to make systems/modules just generic enough to be flexible and reusable while still being specific enough for the intended game-engine genre(fast paced
  dark fantasy fps bullet hell boomer shooter with souls/eldenring-like hard boss fights & fps equivalent combat and boss fight mechanics with a single-player
  tarkov vibes(hub is the players small hometown village that progresses like pathologic 2 npcs that also gives quests/trades/major-npc-specific-exclusive
  grindable system and a *not defined place type* dungeon that is a *plot-hidden-number* levels dungeon with the stalkers the zone meaning/public
  perception(village nps wants things that you can only obtain in the dungeon, but is really hard to get in there and out there alive, so thats how you(since i
  like this irl, the game will also have occult/esoteric/spiritual/alchemical proccess/philosophic/taro's heros journey vibes) make the coin for living; the
  village also has a church/lodge that acts like the "secret order"(golden dawn vibes) and its there how the player first hears/know about the dungeon(the
  intention is to make the player feel that he discovered the dungeon himself just because he was curious about something he heard in the village about that
  temple, and decided to go there) and gets to know his "master" magus/mage and becomes his "squire"/apprentice) but with the dark and darker visual/
  atmospheric dungeon vibes) 3d pixel art shaders with quake 3-like movement and a modernized(warhammer 40k boltgun-like) heretic(1994) enemy/bullets/guns 3d
  models/vfx).

# AGENTS.md

## 1. Purpose

This repository contains a single-player dark-fantasy first-person action game and its supporting C++ engine, editor, runtime, tools, and assets.

This document is simultaneously:

* The high-level game design document.
* The implementation contract for coding agents.
* The authority for tone, mechanics, progression, and technical priorities.
* A guardrail against turning the project into a generic shooter, roguelite, extraction clone, or Soulslike imitation.

All agents must preserve the identity described here. When requirements conflict, prioritize:

1. Responsive and expressive first-person combat.
2. Legible but demanding encounters.
3. Meaningful expedition risk.
4. Discovery and mystery.
5. A reactive village populated by consequential characters.
6. Technical simplicity, maintainability, and performance.

Do not copy copyrighted characters, maps, weapons, dialogue, enemy designs, or assets from referenced games. References describe desired qualities, not content to reproduce.

---

# 2. High Concept

A fast-paced dark-fantasy FPS combining:

* Bullet-hell projectile patterns.
* Boomer-shooter speed and aggression.
* Quake-style movement mastery.
* Deliberate, punishing boss fights adapted to first-person combat.
* Single-player extraction tension.
* A reactive hometown village that changes with time and player decisions.
* Occult initiation, alchemy, tarot symbolism, spiritual transformation, and philosophical ambiguity.
* A vast forbidden place beneath or beyond an ancient temple.
* Low-resolution 3D pixel-art rendering with oppressive dark-fantasy atmosphere.
* Chunky magical firearms, arcane weapons, enemies, projectiles, and effects inspired by the expressive readability of 1990s shooters.

The player is not initially sent on a heroic quest.

They live in a small, struggling village where ordinary people need extraordinary objects obtainable only from a place that most villagers fear, misunderstand, exploit, or mythologize. Curiosity about rumors surrounding an old temple and its associated lodge leads the player toward the forbidden place.

The player should feel that they discovered it themselves.

Through this discovery, the player encounters a magus who becomes their master. The player enters the lodge as an apprentice or squire, but the true purpose of the lodge, the master, and the place must remain uncertain.

The central fantasy is:

> Enter a place that should not be entered, survive through skill and preparation, return carrying objects that change people’s lives, and slowly realize that each expedition is also changing you.

---

# 3. Player Experience

The player should regularly feel:

* Fast.
* Vulnerable.
* Clever.
* Greedy.
* Curious.
* Lost without being directionless.
* Powerful without being safe.
* Relieved when returning home.
* Suspicious of apparently helpful characters.
* Drawn deeper despite understanding the danger.
* Unsure whether progression represents growth, corruption, initiation, or all three.

Combat should produce mechanical mastery.

The village should produce emotional attachment and obligation.

The forbidden place should produce fear, temptation, and mystery.

The lodge should produce philosophical uncertainty.

---

# 4. Non-Negotiable Design Pillars

## 4.1 Speed With Commitment

Movement is fast and expressive, but actions still have consequences.

The player may rapidly reposition through techniques such as:

* Air control.
* Strafe acceleration.
* Momentum-preserving jumps.
* Slides.
* Vaults.
* Short lateral evasions.
* Weapon-specific movement impulses.
* Controlled knockback or recoil movement.

Speed must not eliminate decision-making. Reloads, heavy attacks, rituals, healing, parries, and powerful casts create commitment windows.

The intended result is not slow Soulslike combat transplanted into first person. It is fast FPS combat in which timing, positioning, observation, and restraint matter as much as aim.

## 4.2 Lethal but Legible Combat

The player may die quickly, but should usually understand why.

Attacks must communicate through combinations of:

* Silhouettes.
* Animation.
* Audio.
* Projectile shape.
* Projectile color value.
* Arena effects.
* Enemy posture.
* Consistent timing.
* Distinct impact feedback.

Difficulty must come from execution, pattern recognition, pressure, resource management, and combined threats—not unreadable attacks or arbitrary damage.

## 4.3 Expedition Risk

Entering the forbidden place is an expedition, not a normal level selection.

The player chooses:

* Weapons.
* Ammunition.
* Consumables.
* Ritual preparations.
* Storage capacity.
* Contract objectives.
* How much valuable equipment to risk.

During an expedition, the player must repeatedly decide whether to:

* Continue deeper.
* Deviate toward a rumor or optional objective.
* Spend resources to open a shortcut.
* Attempt a dangerous extraction route.
* Abandon loot to improve survival odds.
* Risk everything for one more chamber.

Reaching an exit while carrying something important must feel as meaningful as defeating a major enemy.

## 4.4 First-Person Boss Duels

Major bosses are the project’s highest combat standard.

A boss must feel like an intelligent duel or ritual confrontation rather than a large enemy with excessive health.

Bosses should test combinations of:

* Movement execution.
* Pattern recognition.
* Aim under pressure.
* Positioning.
* Resource timing.
* Aggression control.
* Defensive technique.
* Arena awareness.
* Build understanding.
* Adaptation between phases.

Every major boss needs a distinct mechanical thesis.

Examples include:

* A duelist who controls safe movement lanes.
* A saint whose attacks form moving bullet patterns.
* A beast whose body becomes navigable arena geometry.
* An alchemist who changes the rules of damage and healing.
* A marksman who turns cover, sound, and line of sight into the encounter.
* A ritualist whose arena must be altered during combat.

Do not solve boss difficulty mainly by increasing health or damage.

## 4.5 The Village Matters

The village is the player’s home, economy, social world, quest hub, and emotional measure of progression.

It is not a static menu populated by vendors.

Villagers have:

* Daily routines.
* Material needs.
* Relationships.
* Beliefs about the forbidden place.
* Limited knowledge.
* Personal crises.
* Conflicting requests.
* Changing inventories.
* Reactions to time and player behavior.
* Potential states such as healthy, desperate, indebted, missing, transformed, hostile, or dead.

Some problems progress without the player.

The player cannot perfectly satisfy everyone.

The village should visibly change because of what the player brings back, withholds, sells, reveals, destroys, or gives away.

## 4.6 Discovery Before Exposition

The player must not begin with a complete explanation of the dungeon, lodge, cosmology, or plot.

Discovery should emerge through:

* Overheard dialogue.
* Contradictory rumors.
* Environmental evidence.
* Merchant requests.
* Village folklore.
* Hidden lodge records.
* Repeated symbols.
* Changes after expeditions.
* Unreliable testimony.
* The player’s own experimentation.

Do not use a conventional quest marker that immediately labels the temple as the main dungeon.

The player should first investigate because something seems strange or interesting.

## 4.7 Esotericism as Structure

Occult, alchemical, spiritual, tarot, and philosophical influences must affect mechanics and narrative structure. They must not exist only as decorative symbols.

Use these influences to shape:

* Character arcs.
* Initiation ranks.
* Boss transformations.
* Alchemical crafting.
* Sacrifice and exchange.
* Dungeon topology.
* Quest structures.
* Player progression.
* Interpretations of death.
* The tension between knowledge and power.

Avoid treating living spiritual traditions as interchangeable horror decoration. Prefer fictionalized systems inspired by historical symbolism, philosophy, ritual structure, and esoteric literature.

---

# 5. Canonical Terminology

The following developer terms may be used internally:

* **Village:** The player’s hometown and primary hub.
* **Expedition:** One departure from the village into the forbidden place and the attempt to return.
* **Lodge:** The secretive occult order associated with the temple.
* **Master:** The magus who introduces the player to lodge practices.
* **Major NPC:** A named village character with a personal storyline and exclusive progression system.
* **Depth:** A region or progression band inside the forbidden place.
* **Extraction:** Returning alive with carried items.
* **Initiation:** Progress through the lodge’s occult systems.
* **Burden:** Persistent consequences carried between expeditions.

“Dungeon” may be used in development, but the final fiction must not definitively classify the place as merely a dungeon.

Different characters may call it:

* The Temple.
* The Below.
* The Interior.
* The Wound.
* The House.
* The Pit.
* The Road.
* The Zone.
* The Old Place.
* A local or religious name.

No single term should completely explain its nature.

---

# 6. Protected Mysteries

These facts must remain hidden from the player until deliberately resolved by the narrative:

* The exact nature of the forbidden place.
* Whether it is physical, spiritual, constructed, alive, or several of these.
* The true purpose of the lodge.
* The master’s complete history and motivation.
* The exact number of levels or depths.
* Whether the player discovered the place freely or was guided toward it.
* Whether initiation protects the player or makes them more compatible with the place.
* Whether recovered objects originate from the past, another world, symbolic reality, or transformed matter.

Do not expose the total number of levels through:

* Achievement counts.
* Save metadata.
* Map numbering.
* File names visible to players.
* Loading screens.
* UI progress bars.
* Public debug commands.
* Dialogue that accidentally confirms the number.

Use semantic internal identifiers such as `SunkenArchive`, `AshChoir`, or opaque stable IDs instead of public-facing sequential names like `Floor_01_of_20`.

Narrative ambiguity must be intentional rather than an excuse for contradictory writing. The development team may maintain private canonical answers while allowing multiple in-world interpretations.

---

# 7. Core Game Loop

## 7.1 Village Phase

The player:

1. Wakes, recovers, or returns to the village.
2. Learns what changed during their absence.
3. Speaks with villagers and lodge members.
4. Accepts requests, rumors, debts, and obligations.
5. Trades recovered materials.
6. Repairs or modifies equipment.
7. Performs alchemical and ritual preparation.
8. Chooses a loadout.
9. Selects which risks and relationships to prioritize.
10. Travels toward the temple without a conventional mission-launch screen.

## 7.2 Expedition Phase

The player:

1. Enters through a known or discovered route.
2. Navigates authored landmarks connected through variable chamber arrangements.
3. Fights enemies and avoids environmental threats.
4. Collects requested objects, equipment, ritual materials, and knowledge.
5. Encounters optional paths, shortcuts, locked routes, and rumors.
6. Chooses whether to descend further.
7. Locates or activates an extraction opportunity.
8. Escapes, dies, becomes trapped, or reaches a narrative transition.

## 7.3 Return Phase

On a successful return:

* Carried items enter the village economy.
* Contracts and personal requests may be completed.
* NPC states advance.
* The lodge may react to discoveries.
* New rumors or routes may become available.
* The player may unlock equipment, knowledge, rites, or social consequences.

On death:

* Most carried expedition loot is lost.
* Unsecured equipment may be lost, damaged, or left recoverable.
* Persistent knowledge remains.
* Village time advances according to the death and recovery rules.
* Certain burdens, injuries, debts, or transformations may persist.
* Death must never be a consequence-free fast-travel method.

The exact loss model should be demanding without encouraging repetitive inventory administration.

---

# 8. Movement

Movement should evoke arena-FPS freedom while remaining controllable in dense projectile encounters.

Required qualities:

* Immediate acceleration feedback.
* Strong but learnable air control.
* Consistent momentum rules.
* Reliable jumping and landing.
* Minimal camera interference.
* No arbitrary slowdown from minor terrain.
* Predictable collision response.
* Support for intentional movement techniques.

Movement mechanics may include:

* Strafe acceleration.
* Momentum-preserving jumps.
* Crouch slides.
* Step climbing.
* Vaulting.
* Recoil boosts.
* Enemy-projectile boosts.
* Weapon jumps when appropriate.
* A short defensive step, dash, or phase action.

Do not require advanced movement exploits for basic completion. Skilled movement should create advantages, alternate routes, faster clears, and improved survival.

Avoid excessive head bob, motion blur, camera shake, chromatic aberration, or weapon sway that compromises aiming.

---

# 9. Combat Model

## 9.1 Combat Rhythm

Combat should alternate between:

* Aggressive resource generation.
* Defensive repositioning.
* Precision target removal.
* Crowd manipulation.
* Short commitment windows.
* High-impact punish opportunities.

Standing behind cover and slowly removing enemies should rarely be the optimal universal strategy.

Enemies should pressure the player to move through:

* Slow projectile fields.
* Area denial.
* Flanking.
* Pursuit.
* Vertical attacks.
* Summoning.
* Line-of-sight control.
* Delayed explosions.
* Arena transformations.

## 9.2 Damage Types

Use a small, understandable set of damage concepts.

Suggested families:

* **Physical:** Shot, blade, impact, fragmentation.
* **Ember:** Fire, combustion, purification, fury.
* **Rot:** Poison, disease, decomposition, corrosion.
* **Astral:** Sorcery, distortion, mind, impossible geometry.
* **Sacred:** Oaths, judgment, ward-breaking, ritual authority.
* **Void:** Absence, silence, erasure, anti-magic.

Do not create a large elemental chart unless each interaction materially changes combat behavior.

## 9.3 Defensive Mechanics

Defense should be active and build-dependent.

Possible defensive tools include:

* Movement evasion.
* Directional guard.
* Timed parry.
* Projectile deflection.
* Temporary ward.
* Weapon-specific block.
* Interrupts.
* Stagger.
* Destructible cover.
* Ritual circles.
* Emergency consumables.

Invulnerability frames, when used, must be explicit, limited, and supported by clear visual and audio feedback.

## 9.4 Stagger and Punish Windows

Important enemies may have a posture, concentration, armor, or ritual-stability system.

Correct aggression creates short punish windows.

Stagger must not become a universal stun-lock strategy. Bosses need resistance rules, phase adaptations, and diminishing control responses.

---

# 10. Weapons

Weapons should be mechanically distinct, visually chunky, and easy to read in motion.

Weapon categories may include:

* Black-powder pistols and hand cannons.
* Repeating occult firearms.
* Heavy bolt-launching weapons.
* Shotguns firing shot, bone, salt, nails, or alchemical payloads.
* Crossbows and mechanical reliquaries.
* Staves and wands used like precision firearms.
* Spellbooks functioning as weapon platforms.
* Relic weapons with ritual conditions.
* Throwable alchemical devices.
* Melee sidearms.
* Hybrid weapons that transform or change firing modes.

Every weapon requires:

* A clear combat role.
* A recognizable silhouette.
* Distinct sound.
* Distinct projectile or impact language.
* At least one mastery mechanic.
* A meaningful limitation.

Possible mastery mechanics include:

* Active reload timing.
* Recoil control.
* Heat management.
* Alternate-fire resource conversion.
* Projectile detonation.
* Charged weak-point attacks.
* Kill-based ammunition recovery.
* Rhythm-based firing.
* Movement interaction.
* Risky overcasting.

Avoid an item-level treadmill where a higher number automatically invalidates an interesting weapon.

Horizontal differences are more important than small statistical upgrades.

---

# 11. Bullet-Hell Rules

Projectile-heavy combat must remain readable in first person.

Enemy projectile rules:

* Projectiles must have understandable trajectories.
* Dangerous projectiles need strong silhouettes and audio.
* Dense patterns should move slowly enough to navigate.
* Fast projectiles require longer telegraphs or restricted firing lanes.
* Homing behavior must communicate lock strength and turning limits.
* Projectile colors must remain readable under all intended lighting.
* Effects must not obscure the projectile that caused them.
* Hitboxes should be slightly more forgiving than visuals when necessary.
* Arena hazards must use a different visual language from collectible or friendly effects.

Use projectile patterns with spatial intent:

* Walls.
* Spirals.
* Fans.
* Rings.
* Delayed mines.
* Rotating lanes.
* Expanding zones.
* Tracking volleys.
* Intersections.
* Baited placement attacks.

Do not fill the screen with particles merely to imply difficulty.

---

# 12. Enemies

Enemy design should be role-based.

Core roles include:

* Pursuer.
* Flanker.
* Artillery.
* Area-denial caster.
* Shielded anchor.
* Summoner.
* Sniper.
* Swarm.
* Support unit.
* Duelist.
* Ambusher.
* Hazard-producing creature.

An encounter should combine a limited number of roles in deliberate ways.

Normal enemies need:

* A clear silhouette.
* A readable primary threat.
* A distinct movement profile.
* At least one exploitable weakness.
* A reason to prioritize or postpone killing them.
* Reactions to damage and player pressure.

Enemies should not all become hyper-aggressive melee pursuers. Variety in space control is essential.

---

# 13. Boss Design Standard

Each major boss specification must define:

* Narrative role.
* Visual thesis.
* Mechanical thesis.
* Arena thesis.
* Phase structure.
* Attack vocabulary.
* Defensive interactions.
* Punish windows.
* Failure lessons.
* Build accommodations.
* Anti-cheese responses.
* Reward and world consequence.

Boss attacks must be learnable.

A valid difficult attack generally contains:

1. A recognizable tell.
2. A consistent rule.
3. A viable response.
4. A consequence for failure.
5. A meaningful punish opportunity or positional change.

Bosses may break previously established rules only after clearly demonstrating the new rule.

A boss must support more than one viable play style, but not every build must be equally optimal.

Checkpoint placement should support learning without removing expedition tension. Major boss approaches may unlock temporary shortcuts, ritual anchors, or repeated-entry arrangements that reduce traversal repetition while preserving resource decisions.

---

# 14. Extraction and Inventory

The player has three broad storage contexts:

## 14.1 Village Stash

Items stored in the village are safe unless a specific narrative event explicitly threatens them.

## 14.2 Expedition Loadout

Weapons, armor, ammunition, tools, and consumables deliberately taken into danger.

These may be lost, damaged, cursed, or left behind on death.

## 14.3 Carried Findings

Loot acquired during the current expedition.

These items are not truly owned until extracted.

A very small protected container or ritual seal may preserve limited objects. Its capacity must remain small enough that extraction matters.

Inventory design should emphasize decisions rather than grid-management busywork.

Weight, bulk, noise, fragility, contamination, or ritual incompatibility may influence what the player carries. Do not activate all these dimensions simultaneously without a clear gameplay purpose.

---

# 15. Economy

Village residents want objects that cannot be safely obtained elsewhere.

Examples include:

* Medicine.
* Metals.
* Fuel.
* Relics.
* Books.
* Food-like substances.
* Monster organs.
* Alchemical reagents.
* Building materials.
* Sacred objects.
* Contraband.
* Evidence.
* Personal keepsakes.
* Items whose purpose the player does not initially understand.

The economy should create conflicting values:

* Market value.
* Personal value to an NPC.
* Lodge value.
* Crafting value.
* Ritual value.
* Future speculative value.
* Moral or narrative value.

Selling an object should sometimes be a more consequential choice than obtaining it.

Avoid infinite money loops and vendors who purchase every object at a universal price.

---

# 16. Major NPC Progression

Every major NPC has a unique progression system tied to that character’s profession, worldview, needs, and relationship with the forbidden place.

Examples:

* A blacksmith develops experimental weapon frames.
* A physician researches dungeon diseases.
* A hunter studies creatures and unlocks tracking preparations.
* A widow collects evidence about a missing expedition.
* A smuggler builds clandestine extraction routes.
* A priest develops protective rites while opposing the lodge.
* An alchemist transforms materials through increasingly dangerous processes.
* A cartographer creates imperfect but useful maps.
* A veteran teaches combat disciplines.
* A collector unlocks relic appraisal while becoming less trustworthy.

These systems must not be interchangeable reputation bars.

Each one should contain:

* Character-specific tasks.
* Unique resources or evidence.
* Mechanical unlocks.
* Narrative changes.
* A visible effect on the village.
* At least one difficult choice.
* A final state that cannot coexist cleanly with every rival path.

High-level commitment to one NPC may close, transform, or complicate another NPC’s system.

The player should not be able to maximize every relationship in one playthrough without severe cost.

---

# 17. Village Time and Consequences

Time advances through expeditions, deaths, and significant story actions.

Do not make real-time waiting the primary progression mechanism.

NPC events may:

* Begin.
* Worsen.
* Resolve without the player.
* Become more expensive.
* Move to another location.
* Create shortages.
* Affect other NPCs.
* Permanently change available quests.
* Cause injury, disappearance, transformation, or death.

Consequences must be foreshadowed enough that the world feels harsh rather than arbitrary.

The player should not receive a complete schedule of future events. Information comes through observation, relationships, rumors, and incomplete warnings.

The village must remain useful even when damaged. Avoid unwinnable states caused by losing one essential vendor.

---

# 18. Lodge and Initiation

The lodge is simultaneously:

* A source of knowledge.
* A progression system.
* A social faction.
* A philosophical school.
* A potential manipulator.
* A bridge between the village and the forbidden place.

The master should be charismatic, useful, and difficult to interpret.

Initiation progression may unlock:

* Ritual preparation slots.
* New interpretations of recovered objects.
* Alchemical operations.
* Occult weapon techniques.
* Perception of hidden paths.
* Dialogue options.
* Protective seals.
* Dangerous transformations.
* Access to lodge spaces.
* New obligations.

Initiation must always have a cost.

Costs may include:

* Rare materials.
* Changed NPC relationships.
* Physical marks.
* New vulnerabilities.
* Binding promises.
* Altered perception.
* Increased attention from entities.
* Sacrificed opportunities.

The player’s initiation arc may loosely echo the tarot hero’s journey, but the game must not explicitly present itself as a checklist of tarot cards.

Symbolism should reward attentive players without requiring external occult knowledge to understand the mechanics.

---

# 19. Forbidden-Place Structure

The forbidden place combines authored level design with controlled variability.

Prefer:

* Authored landmark rooms.
* Authored combat arenas.
* Authored shortcuts and secrets.
* Authored boss spaces.
* Variable connections between compatible modules.
* Conditional room states.
* Multiple entry and extraction routes.
* Persistent discoveries.
* Region-specific encounter rules.

Avoid generating every room from interchangeable procedural tiles.

The place should have stable geography at the scale of memorable landmarks while remaining uncertain between expeditions.

The player must be able to learn it.

Possible structural features:

* Elevators that require restoration.
* Ritual gates.
* One-way descents.
* Collapsing passages.
* Shortcuts opened from the far side.
* Hidden routes revealed by NPC knowledge.
* Extraction points with conditions.
* Chambers that appear only while carrying certain objects.
* Regions altered by defeated bosses.
* Routes affected by village decisions.

Each depth requires a strong identity expressed through architecture, enemies, hazards, sound, loot, and metaphysical rules.

---

# 20. Quest Design

Quests should feel like personal requests, investigations, bargains, or obligations—not task-board chores.

Prefer objectives such as:

* Retrieve an object with ambiguous ownership.
* Find evidence about a missing person.
* Decide which villager receives a scarce medicine.
* Carry a fragile object without using certain extraction methods.
* Observe an enemy instead of killing it.
* Destroy something valuable.
* Bring back two mutually incompatible materials.
* Follow unreliable directions.
* Choose whether to reveal what was found.
* Return with an object whose effects worsen during the expedition.

Avoid repetitive objectives that only vary by quantity.

Quest markers should be minimal. Dialogue, maps, landmarks, item descriptions, and environmental clues must carry useful information.

---

# 21. Narrative Rules

Narrative should be concrete at the human level and ambiguous at the cosmic level.

Villagers speak about:

* Hunger.
* Work.
* Debt.
* Illness.
* Family.
* Fear.
* Faith.
* Jealousy.
* Missing people.
* What they need from the player.

They should not constantly speak in cryptic poetry.

Lodge members, transformed characters, dreams, inscriptions, and entities may use more symbolic language, but symbolism must retain meaning.

Avoid:

* Constant lore monologues.
* Exposition dumps.
* A chosen-one opening.
* A morality meter.
* Obviously labeled good and evil factions.
* Dialogue options that reveal their consequences in advance.
* Modern irony that undermines the atmosphere.
* Explaining every mystery.

Player identity should allow role-playing through actions and priorities more than extensive biography selection.

---

# 22. Visual Direction

The visual target is dark-fantasy 3D pixel art.

Core qualities:

* Low-resolution internal rendering.
* Nearest-neighbor presentation where appropriate.
* Chunky readable geometry.
* Deliberately limited texture resolution.
* Strong silhouettes.
* Restrained palettes by region.
* Billboard or mesh-based retro effects.
* Dramatic darkness.
* Local lights with clear gameplay purpose.
* Dense atmospheric depth without muddy combat.
* Detailed weapons contrasted against simplified distant geometry.

Possible retro effects include:

* Ordered dithering.
* Palette quantization.
* Low-resolution shadows.
* Vertex snapping.
* Texture warping.
* Limited animation frames.
* Pixel-scaled particles.
* Posterized fog.

Retro effects must never compromise aiming, projectile recognition, navigation, or accessibility.

Do not stack every retro artifact simultaneously. Visual instability should be authored and adjustable.

The atmosphere inside the forbidden place should feel claustrophobic, ancient, damp, sacred, decayed, and dangerous. Darkness must create tension without forcing the player to stare at a black screen.

---

# 23. Legacy Shader and Asset Adaptation

The repository may contain shaders, materials, effects, textures, models, and conventions from the developer’s previous OpenGL 3.3 game engine inside `assets/`.

Agents must inspect and reuse or adapt these assets before replacing them.

When adapting legacy shaders:

* Preserve the intended PSX or 3D-pixel-art appearance.
* Separate renderer-independent effect parameters from API-specific shader code.
* Port behavior rather than performing superficial syntax translation.
* Document differences caused by coordinate systems, depth ranges, texture sampling, color spaces, or shader-stage changes.
* Validate vertex snapping, affine-like warping, dithering, fog, quantization, and palette effects independently.
* Keep a reference image or test scene for visual regression checks.
* Do not permanently alter source legacy assets when a converted copy is required.
* Avoid making gameplay rendering dependent on undocumented magic constants.

Legacy assets should become part of a maintainable material and post-processing system rather than isolated special cases.

---

# 24. Audio Direction

Audio is essential for combat readability and atmosphere.

Required qualities:

* Weapons recognizable without looking.
* Enemy roles identifiable through sound.
* Boss attacks telegraphed audibly.
* Projectiles with meaningful spatial cues.
* Village ambience that changes with state and time.
* Distinct acoustic identities for each depth.
* Music that supports pressure without continuously exhausting the player.
* Strategic silence.

Dynamic music may respond to:

* Enemy pressure.
* Remaining resources.
* Boss phases.
* Extraction availability.
* Carrying significant objects.
* Entering lodge-related spaces.

Do not use constant loud drones as the only method of creating dread.

---

# 25. Accessibility

Difficulty is central, but unnecessary physical barriers are not.

Support:

* Full input rebinding.
* Mouse sensitivity controls.
* Field-of-view controls.
* Reduced camera motion.
* Reduced screen shake.
* Adjustable flashes.
* Color-independent projectile identification.
* Subtitle and dialogue-log support.
* Hold/toggle options.
* Scalable UI.
* Audio range controls.
* Separate music, dialogue, ambience, weapon, and warning volumes.
* A training environment for movement and defensive mechanics.

Difficulty options should modify understandable variables or offer targeted assists. Do not secretly make enemies inconsistent.

Possible assists include:

* Wider parry timing.
* Slower hostile projectiles.
* Reduced expedition-loss severity.
* Additional attack telegraphs.
* Aim assistance.
* Increased interaction visibility.
* Boss practice after first discovery.

---

# 26. Repository Responsibilities

Use the existing repository structure unless the project already defines a better equivalent.

## `/engine`

Renderer-independent and game-agnostic systems:

* Platform abstraction.
* Windowing.
* Input.
* Rendering.
* Audio.
* Physics and collision.
* Asset loading.
* Resource lifetime.
* Scene management.
* Serialization.
* Job system.
* Debugging.
* Profiling.
* Engine-level ECS or object infrastructure.

The engine must not contain village-specific, lodge-specific, quest-specific, or lore-specific logic.

## `/runtime`

The playable game:

* Player controller.
* Weapons.
* Projectiles.
* Damage.
* Enemies.
* Bosses.
* Encounters.
* Expeditions.
* Extraction.
* Inventory.
* Village simulation.
* NPC relationships.
* Quests.
* Lodge progression.
* Saving.
* Game-specific UI and audio behavior.

## `/editor`

Tools for:

* Level construction.
* Landmark and modular-room authoring.
* Encounter placement.
* Patrol and navigation editing.
* Boss arena scripting.
* NPC schedules.
* Quest graphs.
* Dialogue conditions.
* Loot tables.
* Extraction rules.
* Lighting and atmosphere.
* Material previews.
* Shader-effect tuning.
* Validation.

## `/assets`

Source and runtime assets, including legacy OpenGL 3.3 shaders and prior PSX-style rendering work.

Do not mix generated cache files with source assets.

---

# 27. C++ Engineering Rules

Use the repository’s selected language standard. Default to C++17 when no newer standard is already established.

Required practices:

* Use RAII for ownership.
* Do not use raw owning pointers.
* Use explicit lifetime boundaries.
* Use stable handles for cross-system references.
* Validate external data.
* Treat warnings as actionable.
* Check API return values.
* Keep platform and rendering API details behind boundaries.
* Avoid global mutable state.
* Avoid hidden singleton dependencies.
* Avoid per-frame heap allocation in hot paths.
* Keep update order explicit.
* Make thread ownership clear.
* Use fixed-step simulation where consistency requires it.
* Use render interpolation rather than coupling rendering to simulation.
* Make save formats versioned.
* Make random generation seedable.
* Keep gameplay data inspectable in development builds.

Prefer small, focused components and systems.

Do not create abstractions for hypothetical future games unless the current project has at least two real consumers requiring them.

---

# 28. Gameplay Architecture

Use a hybrid architecture rather than forcing every system into one paradigm.

Recommended boundaries:

* Data-oriented storage for high-volume objects such as projectiles, particles, decals, and simple enemies.
* Explicit stateful objects or components for bosses, quests, NPC storylines, and complex interactions.
* Data-driven definitions for weapons, enemies, loot, dialogue, and encounters.
* Code-defined behavior for mechanics that require strong invariants or performance.
* Scriptable encounter sequencing without moving fundamental combat logic into fragile scripts.

Core gameplay systems should communicate through typed events or explicit APIs.

Avoid string-based event names for critical behavior.

Suggested game-level services include:

* `CombatWorld`
* `ProjectileSystem`
* `DamageSystem`
* `EncounterDirector`
* `ExpeditionState`
* `ExtractionSystem`
* `InventoryService`
* `VillageSimulation`
* `QuestState`
* `NpcStateRegistry`
* `LodgeProgression`
* `SaveGameService`

Names may follow existing repository conventions, but responsibilities should remain separate.

---

# 29. Projectile Performance

The game may display large numbers of projectiles.

Projectile implementation should support:

* Pooling or contiguous allocation.
* Batched updates.
* Broad-phase collision filtering.
* Stable ownership handles.
* Cheap lifetime expiration.
* Separation of simulation and visual effects.
* Deterministic or seedable pattern generation.
* Debug visualization.
* Performance counters.
* Graceful visual degradation under extreme load.

Do not attach heavyweight general-purpose entities to every decorative particle.

Gameplay projectiles and visual particles must remain distinct.

Projectile simulation must not become frame-rate dependent.

---

# 30. AI and Encounters

Normal enemy behavior should use understandable state machines, utility logic, behavior trees, or a limited combination of these.

Boss behavior should be explicitly authored around:

* Phase state.
* Attack selection constraints.
* Arena state.
* Player position.
* Recent attack history.
* Cooldowns.
* Anti-repetition rules.
* Interrupt and stagger state.

Do not use unconstrained random attack selection.

Encounter composition belongs in data or editor-authored assets.

The encounter director may adjust pacing, but it must not secretly spawn threats behind the player without valid entrances, audio, or narrative justification.

---

# 31. Data and Content

Gameplay definitions should be data-driven where iteration benefits from it.

Candidate data assets include:

* Weapon definitions.
* Projectile definitions.
* Enemy archetypes.
* Damage profiles.
* Loot tables.
* Encounter definitions.
* NPC schedules.
* Quest conditions.
* Dialogue.
* Region properties.
* Material parameters.
* Alchemical recipes.
* Extraction conditions.

Every data format needs:

* A schema or strongly validated loader.
* Stable identifiers.
* Useful error messages.
* Default handling.
* Versioning when persisted.
* Editor validation where possible.

Do not silently accept malformed content.

---

# 32. Save System

The save system must distinguish between:

* Player progression.
* Village state.
* NPC state.
* Quest state.
* Lodge initiation.
* Discovered knowledge.
* Expedition state.
* Stash state.
* Equipment state.
* World-route state.
* Configuration and accessibility settings.

Use versioned saves and migrations.

Never serialize raw pointers, memory addresses, renderer handles, or transient object indices as persistent identity.

Autosaving should occur at clear state boundaries.

The game must avoid easy save corruption while also preventing uncontrolled save-scumming from erasing all expedition tension. Exact restrictions must be communicated honestly to the player.

---

# 33. Testing Requirements

At minimum, maintain automated tests for:

* Damage calculations.
* Inventory transfers.
* Item-loss rules.
* Extraction-state transitions.
* Quest conditions.
* NPC state transitions.
* Save migration.
* Seeded dungeon-layout generation.
* Projectile lifetime and collision.
* Boss phase transitions where practical.
* Data validation.

Maintain development test spaces for:

* Movement.
* Weapon behavior.
* Projectile readability.
* Enemy combinations.
* Boss attacks.
* Lighting.
* Materials.
* Extraction.
* Village schedule simulation.

A test arena is not a substitute for testing the complete expedition loop.

---

# 34. Performance Priorities

Optimize based on profiling, with special attention to:

* Projectile updates.
* Collision queries.
* Animation.
* Visibility.
* Dynamic lights.
* Shadows.
* Transparent effects.
* Enemy perception.
* Navigation.
* Save serialization.
* Village schedule simulation.
* Asset streaming.

Target smooth input and consistent frame pacing before increasing visual density.

The retro visual style does not excuse poor performance.

Debug builds must expose useful timing information without requiring an external profiler for every investigation.

---

# 35. Editor and Debugging Requirements

Development builds should provide tools for:

* Invulnerability.
* Loadout creation.
* Teleportation between landmarks.
* Starting specific encounters.
* Replaying boss phases.
* Free camera.
* AI state inspection.
* Projectile hitbox visualization.
* Navigation visualization.
* Loot-table sampling.
* NPC schedule inspection.
* Quest-state inspection.
* Time advancement.
* Extraction forcing.
* Save-state validation.
* Material and shader tuning.
* Seed capture and replay.

Debug tools must not leak protected mysteries into normal player-facing interfaces.

---

# 36. Scope Restrictions

This project is not:

* A multiplayer extraction shooter.
* A live-service game.
* A competitive PvP game.
* An open-world sandbox.
* A fully procedural roguelike.
* A generic medieval looter shooter.
* A cover-based tactical shooter.
* A conventional power-fantasy RPG.
* A direct clone of any referenced game.
* A simulation of every village resident at all times.
* A game where every mechanic needs a crafting tree.
* A game where loot rarity colors replace item identity.

Do not add multiplayer architecture, online accounts, battle passes, seasons, premium currency, or server-authoritative networking without an explicit project-level decision.

---

# 37. Vertical-Slice Order

Development should prove the game’s identity in this order:

## Milestone 1: Movement and Weapon Feel

Deliver:

* One movement test map.
* One firearm.
* One magical weapon.
* One melee or defensive tool.
* Basic damage and feedback.
* Initial low-resolution rendering.

## Milestone 2: Combat Language

Deliver:

* Several enemy roles.
* Projectile patterns.
* Encounter composition.
* Healing and resource rules.
* Death and restart.
* Performance validation under projectile load.

## Milestone 3: Boss Standard

Deliver:

* One complete multi-phase boss.
* A finished arena.
* Clear telegraphs.
* Multiple viable player responses.
* Practice and debugging tools.

## Milestone 4: Expedition Loop

Deliver:

* Loadout selection.
* Carried loot.
* Extraction.
* Death losses.
* Stash.
* One authored region with variable routes.

## Milestone 5: Living Village

Deliver:

* Several scheduled NPCs.
* Trading.
* Personal requests.
* Time progression.
* Visible consequences.
* One character-specific progression system.

## Milestone 6: Lodge and Initiation

Deliver:

* Temple discovery sequence.
* First meeting with the master.
* Initial initiation.
* One ritual preparation system.
* A meaningful cost or obligation.

## Milestone 7: Integrated Vertical Slice

Deliver one polished loop:

1. Learn about a village need.
2. Prepare.
3. Enter through the temple.
4. Navigate and fight.
5. Recover a meaningful object.
6. Face an optional escalation.
7. Extract.
8. Return.
9. Choose what to do with the object.
10. Observe changes in the village and lodge.

Do not expand content production aggressively before this loop is compelling.

---

# 38. Agent Workflow

Before modifying code, an agent must:

1. Read this document.
2. Inspect the relevant engine, runtime, editor, and asset code.
3. Search for existing implementations before introducing new systems.
4. Inspect earlier engine work and legacy assets when related.
5. Identify ownership and update-order implications.
6. Write or update tests for deterministic logic.
7. Keep changes focused on the requested feature.
8. Preserve existing APIs unless change is justified.
9. Run relevant tests and validation tools.
10. Report limitations honestly.

When implementing a feature:

* Start from the smallest playable behavior.
* Prefer a vertical path over disconnected infrastructure.
* Avoid speculative frameworks.
* Keep data inspectable.
* Add debug visualization early.
* Measure performance before claiming optimization.
* Document new content formats.
* Update this file when a decision changes the game’s identity or repository-wide architecture.

Agents must not silently invent major canon, expose protected mysteries, or resolve deliberate ambiguity.

---

# 39. Feature Acceptance Questions

Before considering a feature complete, ask:

* Does it strengthen the central expedition loop?
* Does it support fast, expressive first-person combat?
* Is the danger readable?
* Does skill meaningfully improve the outcome?
* Does failure teach the player something?
* Does the feature affect preparation, extraction, the village, or initiation?
* Is it mechanically distinct from existing systems?
* Is it data-driven only where useful?
* Can it be debugged?
* Can it be tested?
* Does it preserve performance under combat load?
* Does it fit the atmosphere without obscuring gameplay?
* Does it preserve discovery rather than over-explaining the world?

A feature that fails several of these questions should be revised or removed.

---

# 40. Final Creative Direction

The player begins as a person trying to survive in a small village.

They hear something strange.

They investigate because they are curious.

They find a threshold.

Beyond it is a place where violence, wealth, knowledge, faith, and transformation are inseparable.

They return carrying what their neighbors need.

Each return makes life possible.

Each descent makes returning less certain.

The game succeeds when the player stands at the entrance with valuable equipment, unfinished obligations, limited supplies, and a clear opportunity to turn back—but chooses to enter anyway.

