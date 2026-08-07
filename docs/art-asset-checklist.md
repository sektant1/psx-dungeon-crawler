# Asset pipeline checklist {#doc-art-asset-checklist}

Fast-paced dark fantasy FPS bullet-hell boomer shooter with Souls-style bosses, Tarkov extraction loops, Pathologic 2-style village progression, Stalker/Zone dungeon risk-reward, occult/Golden Dawn framing, and Dark and Darker atmosphere. Visual target: 3D pixel-art shaders + modernized Heretic (1994) enemy/gun/VFX language + Quake 3 movement feel + Boltgun-style weapon weight.

### 1. Style & Technical Foundations (Lock First)
- [ ] Final art bible: 3D pixel-art look (resolution limits, palette constraints, dithering rules, normal-map intensity, lighting model). Reference Dark and Darker dungeon lighting + Heretic silhouette language.
- [ ] Shader suite: pixelation/post-process, dither, limited color ramp, rim light for occult glow, blood/gore stylization, fog/atmospheric density for dungeon “Zone” feel.
- [ ] Movement prototype (Quake 3 baseline): accel, friction, air control, bunny-hop window, slide, weapon sway/bob linked to velocity.
- [ ] Core camera & FOV rules for first-person (weapon FOV separate from world FOV).
- [ ] Performance budgets: poly counts, texture sizes, draw calls, particle limits, audio voice limits (especially during bullet-hell moments).
- [ ] Naming & folder conventions, LOD strategy, texture atlas rules, material instance workflow.

### 2. Player & First-Person Assets
- [ ] Player body (if visible) or pure first-person arms + hands in multiple states (idle, run, climb, injured, occult ritual).
- [ ] Weapon models (modernized Heretic + Boltgun weight): primary, secondary, melee/ritual tools, occult implements. High-detail first-person meshes + lower third-person/world meshes.
- [ ] Weapon animations: draw, idle, fire, reload, empty, inspect, special occult charge/fire, hit reactions.
- [ ] Projectile & tracer VFX library (bullet-hell density safe): ballistic, magical, alchemical, soul/spirit types. Include impact, trail, residual, and “extract under fire” variants.
- [ ] Player VFX: muzzle flash, shell casings (stylized), blood, occult auras, stamina/health feedback, extraction beacon.

### 3. Village Hub (Pathologic 2 progression + Golden Dawn lodge)
- [ ] Modular village architecture set (houses, streets, church/lodge exterior + interior, market, residences). Time-of-day and progression variants (decay, growth, plague-like change, seasonal occult markers).
- [ ] Key location interiors: player home, church/lodge (ritual chamber, library, apprenticeship area), major NPC homes/shops.
- [ ] NPC models + variants: villagers, traders, quest givers, major exclusive grind NPCs, the Magus/Master, apprentice progression look.
- [ ] NPC animations: idle loops, talk, trade, work, reaction to player reputation/time, ritual gestures, fear/hostility states.
- [ ] Props & interactables: quest items, trade goods, alchemical stations, tarot/occult props, church relics, grindable exclusive system objects.
- [ ] Lighting & atmosphere sets for village day/night + progression stages (hopeful → strained → occult tension).

### 4. Dungeon / The Zone (Dark and Darker atmosphere + multi-level + extraction risk)
- [ ] Modular dungeon kit: corridors, chambers, verticality, ritual spaces, natural/organic sections, “anomaly” zones, extraction points. Support plot-hidden level count and procedural or hand-crafted layout.
- [ ] Environmental storytelling assets: corpses, failed expeditions, occult markings, alchemical residue, Zone “anomaly” visual language, public-perception vs reality contrast.
- [ ] Lighting & fog volumes, volumetric god-rays, occult light sources, darkness that forces risk.
- [ ] Destructible / interactive environment pieces (cover, barriers, ritual objects that can be used or destroyed).
- [ ] Extraction / return assets: portals, beacons, escape routes, “almost made it” visual feedback.

### 5. Enemies & Bosses (Heretic modernized + bullet-hell + Souls telegraphs)
- [ ] Enemy roster models: common, elite, special, and boss silhouettes. Strong readable shapes from Heretic DNA updated with modern detail density and pixel-art constraints.
- [ ] Enemy LODs, ragdoll/physics proxies, hitbox sets.
- [ ] Animation sets: locomotion, attacks (including multi-phase bullet-hell patterns), reactions, deaths, spawn/despawn, staggered/Souls-style recovery windows.
- [ ] Boss-specific assets: multi-phase models or additive geometry, unique arenas, telegraph VFX (clear wind-ups readable in first-person), phase transition effects, environmental interaction pieces.
- [ ] Projectile & attack VFX per enemy type (density-tested for bullet-hell readability).
- [ ] Status & debuff VFX (poison, curse, alchemical, spiritual).

### 6. Weapons, Items, Loot & Occult Systems
- [ ] Full weapon family + attachments/variants if any.
- [ ] Loot & item models: Tarkov-style inventory objects, dungeon-exclusive materials, alchemical ingredients, quest-only items, Magus-apprentice exclusive grindables.
- [ ] Inventory & UI 3D previews or icons that match the 3D pixel style.
- [ ] Occult/alchemical process assets: ritual circles, reagent combinations, tarot/hero’s-journey visual metaphors, transformation effects.
- [ ] World pickup & interaction feedback (glow, sound, particle).

### 7. VFX, Shaders & Atmosphere
- [ ] Core shader library finalized and documented.
- [ ] Bullet-hell safe particle systems (screen-space clarity rules).
- [ ] Occult / spiritual / alchemical VFX language (consistent across village rituals and dungeon).
- [ ] Environmental VFX: fog, dust, blood mist, Zone anomalies, church incense/light.
- [ ] Post-process stack: pixelation, color grading per location (village vs dungeon), damage vignette, extraction tension filters.
- [ ] Death / near-death / successful extraction cinematic or feedback packages.

### 8. Audio (Integrated Early)
- [ ] Weapon foley & fire (weight like Boltgun, occult variants).
- [ ] Enemy vocalizations & attack telegraphs (Souls-readable).
- [ ] Village ambient + progression layers + lodge ritual beds.
- [ ] Dungeon / Zone ambient (tension, isolation, anomaly stingers).
- [ ] UI, inventory, trade, quest, extraction success/fail stingers.
- [ ] Music stems that support bullet-hell intensity and quiet occult exploration.

### 9. UI / UX & Meta
- [ ] First-person HUD (minimal, readable under bullet hell).
- [ ] Inventory / Tarkov-style grid or containers.
- [ ] Quest log, NPC dialogue, trade screens, Magus apprenticeship progression UI.
- [ ] Map / extraction / risk indicators.
- [ ] Occult / tarot / alchemy interface language.
- [ ] Death / extract / progression summary screens.

### 10. Pipeline, Integration & Polish Stages
- [ ] Source → intermediate → engine import pipeline documented (modeling package → texture tool → engine).
- [ ] Automated validation: poly count, texture size, naming, missing LODs, broken materials.
- [ ] Animation retargeting / state machine templates for player, NPCs, enemies, bosses.
- [ ] VFX performance profiling under max bullet-hell density.
- [ ] Lighting & probe baking strategy for village progression states and dungeon levels.
- [ ] Save/load compatible asset versioning.
- [ ] Accessibility passes (color blindness, telegraph clarity, audio cues).
- [ ] Final art pass: silhouette readability, first-person gun feel, extraction tension, occult discovery moment (player “finding” the dungeon through curiosity).

### Priority Order Recommendation
1. Style bible + movement + first-person weapon feel  
2. Core dungeon modular kit + basic enemy + bullet-hell projectiles  
3. One complete boss (Souls telegraph language in FPS)  
4. Village hub shell + Magus + first discovery path  
5. Extraction loop + exclusive loot + NPC grind systems  
6. Full enemy roster, progression variants, occult systems, polish

This checklist is designed so every asset directly serves the fantasy: the curious discovery of the Zone, the hard extract that funds village life, the apprenticeship under the Magus, and the bullet-hell Souls pressure inside a Dark-and-Darker atmosphere rendered with 3D pixel-art shaders and Heretic-derived silhouettes.
