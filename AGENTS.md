# AGENTS.md

The engineering map. `CLAUDE.md` says what the game is and how to work; this
says where everything lives and how the systems fit together. Read that first.

**The game: a realistic post-apocalyptic PSX first-person milsim sandbox with
zombies.** Every design note below resolves back to that.

---

# 1. LAYOUT

```
engine/          the engine. Knows nothing about this game.
  include/eng/   public headers — the whole API surface
  src/           core, render/{,rhi}, physics, animation, particles, audio,
                 ecs, controllers, camera, content, acp, script, ui, runtime
game/            this game. Player, weapons, ballistics, AI, HUD, levels.
editor/          the world editor: scene document, viewport, panels, cook.
assets/          all content. See §3.
tools/           Python authoring and import tools. See §4.
docs/            reference. `docs/design/` holds accepted design decisions.
samples/         small apps on the same engine (psx_demo, material stage).
```

Three binaries share one engine and one content tree: `game`, `scene_editor`,
`psx_demo`. Each is an `eng::Application`, so every debug target takes
`APP=game|scene_editor|psx_demo`.

**The engine/game line.** Anything that would be true of a different game
belongs in `engine/`. Anything that encodes *this* game's rules — a cartridge's
penetration, a zombie's hearing radius, what a magazine is — belongs in `game/`.
When in doubt the test is: would a fantasy RPG built on this engine want it?

---

# 2. THE SYSTEMS

## 2.1 Frame and fixed step

`eng::Application` owns the loop. Simulation runs at a fixed step; presentation
interpolates. **Physics, weapons and AI advance only in the fixed step**; camera,
viewmodel motion and HUD read the interpolated result. Mixing the two is how a
weapon's cadence becomes frame-rate dependent.

## 2.2 Player

- `eng::FpsController` — the Jolt character controller. Movement tuning is
  `[player]` in `game.toml`.
- `game::PlayerSystem` — owns the player entity, the camera rigs, the loadout,
  the hands, and the interaction focus. One place where input becomes commands.
- Camera rigs (`FirstPersonCameraRig`, `ThirdPersonCameraRig`) are engine-side
  and interchangeable; `camera_toggle` switches live.

## 2.3 Weapons and ballistics — the core of this game

Four separable things, and keeping them separate is what makes the sandbox work:

```
PlayerWeaponDef      what the GUN is    — cadence, magazine, viewmodel, socket
Cartridge            what the ROUND is  — velocity, drag, penetration, damage
WeaponController     runtime state      — cooldown, switch lock, magazines
ProjectileSystem     what flies         — bodies, drag, gravity, wind, impact
```

A weapon says which *calibre* it chambers; a cartridge says what a round of that
calibre does. They vary independently — the interesting decision in a shooter
with real ammunition is not which rifle you carry, it is what you feed it.

**Delivery** (`fire_mode`): `projectile`, `melee`, `hitscan`. Every firearm is
`projectile` — see `assets/config/firearms.toml` on why hitscan is wrong for
this game and why muzzle velocities are scaled to ~1/3 of real.

**Magazines** (`[.ammo]`): capacity, reserve, per-shot cost, separate tactical
and empty reload times, shell-by-shell reloading that firing can interrupt, and
shared reserve pools by ammo type. A weapon with no `[.ammo]` block has no
magazine at all, which is what lets the fantasy loadout coexist unchanged.

**Penetration** (`game::resolvePenetration`): a round defeats an armour class or
it does not. Armour is not a hit-point pool a big enough number always beats;
degradation is what stops that being a permanent wall. One function, so the
projectile system and anything future answer identically.

**aim ≠ muzzle.** The camera ray decides where the player aims. The projectile
*originates* at the gun's real barrel — `barrel_offset` is a point in the weapon
model's own space, so it rides the attach transform and the recoil, and a round
leaves the barrel you can see. Convergence toward the aim point is what keeps an
offset viewmodel from shooting sideways.

Files: `game/src/PlayerWeapons.{h,cpp}`, `Ammunition.{h,cpp}`,
`Projectiles.cpp`, `WeaponDelivery.cpp`.
Data: `assets/config/firearms.toml`, `ammo.toml`, `weapons.toml`.

## 2.4 Viewmodel

```
Camera → Viewmodel Rig → hands (skinned, 15 selectable rigs)
                       → weapon (model or sprite) on a named socket
```

Composable layers, deliberately not one update function:
`base + movementBob + idleBob + lookSway + recoil + actionAnimation +
landingImpulse`. Tuning is `[player_viewmodel]` in `game.toml` (the rig as a
whole) and per-weapon `[.viewmodel]` blocks (how one weapon leans out of it).

**Sockets** are named points on the hand skeleton, declared per rig in
`viewmodel_hands.toml`. A weapon names a socket, never a joint. Rigs declare
their own socket lists because they are not the same shape — the alien arm has
three fingers.

The renderer has a dedicated first-person target (`RenderCore::SceneTarget::
Viewmodel`), so the viewmodel does not inherit world depth or clipping.

Files: `FirstPersonHands.{h,cpp}`, `WeaponViewmodel.*`, `SpriteViewmodel.*`,
`HandsDefinition.*`, `ViewmodelSocket.h`.

## 2.5 Terrain

`eng::Terrain` — a heightfield patch: procedural or from a heightmap PNG,
bilinearly sampled, with sculpt operations (`raise`, `smooth`) and
`geometry()` producing render + collision meshes. Local space, origin at the
patch CENTRE. Authored as a `terrain` component on a scene entity.

It exists because everything before it was a flat dungeon grid, and this game is
outdoors. `heightAt`/`normalAt` share one interpolated field so a slope's
shading and the direction a character slides agree.

## 2.6 Prefabs, scenes and levels

- **Prefab libraries** — `assets/prefabs/<domain>.prefab.toml`, generated by the
  importer, one entry per placeable with its measured size and material.
- **Scene** (`.scn`) — the authored document: entities, transforms, hierarchy,
  components, layers. JSON, schema in `assets/schemas/`.
- **Map** (`.map`) — the cooked runtime form. `make cook` produces it; CI checks
  it is current.
- **Instancing** — an entity can BE another scene, expanded before anything
  downstream sees the document.

## 2.7 Assets

Implements figure 1.33 of *Game Engine Architecture*: a resource database
(`.meta` sidecars, stable guids), one exporter per row, and `raven_acp`
publishing `build/cooked/` + a manifest. Build keys are content hashes, never
timestamps. See `docs/assets-pipeline.md`.

`assets::resolve()` answers logical path → file. A loader that can read the
conditioned form asks for it by name with `assets::conditioned()`.

## 2.8 Editor

The **Asset Library** has exactly five tabs, sharing one wireframe with a
fixed-height preview block: **Worldbuilding / Props / Textures / Particles /
Shaders**. The split between the first two is by prefab `role`, so an imported
pack lands in the right tab by declaring one, with no editor change.

Retexturing lives in Textures: pick an image, clone a material with it, and the
variant persists to `assets/materials/variants.mat` — a file the importer never
regenerates.

## 2.9 Audio — a game mechanic here, not dressing

Zombies are attracted by sound, so loudness is gameplay data on a weapon, not a
mixer setting. A suppressed pistol and a rifle solving the same problem
differently is the central tension of a firefight in this game.

---

# 3. CONTENT MAP

```
assets/
  source/        vendor packs + packs.toml. INPUTS. Not loaded at runtime.
  prefabs/       <domain>.prefab.toml         (generated)
  meshes/        <domain>/*.obj, viewmodels/*.glb, actors/*.glb
  textures/      <domain>/*.png               (generated, nearest-sampled)
  materials/     <domain>.mat (generated) + hand-authored + variants.mat
  animations/    cooked ozz skeletons and clips
  config/        game.toml, firearms.toml, ammo.toml, enemies.toml, ...
  scenes/        .scn sources and cooked .map
  shaders/       GLSL. The PSX look lives here — do not redesign it.
  particles/     effect definitions and atlases
  scripts/       Lua entity and level scripts
```

**Generated files carry a header saying so.** Editing one is work that
disappears at the next import. Change `packs.toml` and re-import instead.

---

# 4. TOOLS

| Tool | What it does |
|---|---|
| `import_asset_pack.py` | vendor pack → meshes, textures, materials, prefabs |
| `clean_imported_pack.py` | remove one domain's output, for a clean re-import |
| `author_hand_rigs.py` | hands pack → 15 two-armed animated rigs, cooked |
| `author_humanoid_rig.py` | the shared actor skeleton and clip library |
| `author_hands_showroom.py` | the hand-rig test scene in a forest clearing |
| `pngkit.py` | exact 8-bit PNG read/write, no image library |
| `assetlint.py` | reference and naming checks (a ctest) |
| `visual_test.py` | the frozen-image gate |
| `build-doctor.sh` | `make doctor` |

Everything a tool generates is derived: delete the outputs, re-run, get the same
bytes. That is what makes re-importing a non-event rather than a merge.

---

# 5. INVARIANTS

Break these and something fails far from the change:

1. **The image is frozen.** No refactor changes the rendered game image.
2. **Node forward is local -Z.** Authored characters face -Z.
3. **`clearScene()` destroys skinned meshes.** Reload after every level build.
4. **Texture basenames are globally unique.** The resource group is flat; two
   `door_albedo.png` means one silently wins.
5. **Generated content is generated.** Never hand-edit it.
6. **Fixed step owns simulation.** Presentation only reads.
7. **A conditioned mesh is used only when its import settings match the call
   site's.** Geometry is baked; a pivot mismatch silently moves the level.
8. **Prefab ids and enemy ids reach saves and cooked maps.** Never bulk-rename
   without migrating every reference.

---

# 6. TESTING

`make test` runs ctest (167 tests). Most are headless and fast; a few need a
Vulkan device.

- Logic (weapons, ammo, ballistics, AI, layout) is unit-tested headless.
- Content is checked by `assetlint` and `acp_content`.
- The rendered image is checked by `visual_test`.

A red suite is not automatically yours — check `git stash` or a known-failing
list before assuming. But **do not report work complete on a red suite without
saying so and saying which failures are yours.**
