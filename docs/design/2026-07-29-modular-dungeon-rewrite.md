# Phase 4 — rewrite the dungeon on the modular kit {#doc-design-2026-07-29-modular-dungeon-rewrite}

Status: partially superseded. Kit conversion landed; the proposal to drop
`.map` is superseded by `2026-07-27-prototype-content-reset-and-pipeline.md`:
JSON `.scn` is canonical authoring data and binary `.map` remains its derived,
versioned runtime container.
Date: 2026-07-29

## What this replaces

The dungeon is generated today from `dungeon.toml` character rows into six
converted tiles (`tile_floor`, `tile_wall`, `tile_ceiling`, `tile_arch`,
`tile_pillar`, `tile_wall_plaster`) by `DungeonGen` + `DungeonMap` (513 + 750
lines), and levels persist as a bespoke binary `.map`.

The replacement generates from the full 44-piece kit and persists as JSON.

## The kit, measured

`assets/models/dungeon/modular/Models`, 43 FBX (44 meshes -- `Chest` is two,
base and lid). Converted to `game/assets/meshes/kit/*.obj`. Every texture the
kit needs was already in `game/assets/textures/dungeon/`: this kit is where the
current dungeon art came from, and the game was using a sixth of it.

The grid is clean and needs no adaptation:

| Piece | Size (kit units) |
|---|---|
| `Floor_Tiles` | 20 x 20, flat at Y=0 |
| `Wall_01` | 20 wide x 20 tall x 5 thick |
| `Block` | 20 x 20 x 20, a solid cell |
| `Block_Half` | 20 x 10 x 20 |
| `Arch` | 20 wide, opening from Y 4.8 to 16.8 |
| `Pillar` | 5.65 diameter x 30 tall |

One cell is 20 units and a wall is exactly as tall as a cell is wide, every
piece centred on its origin with its base at Y=0. At an import scale of 0.2
that is a 4 m cell -- the `cell_size = 4.0` the level documents already use.

The vocabulary the generator gains over the current six tiles: half walls, wall
borders, ruined and decorated walls, expanding/narrowing walls, windowed walls,
skull walls, three door widths with two frames, three stair widths, collapsed
pillars, arch fences and roofs, hexagon and block variants, and props (barrel,
box, chest, candles, chain, chandelier, debris, spikes, table).

## Where the code goes

Dungeon *generation* stays in the game. It is policy -- room graphs, encounter
pacing, what a "vault" is -- and putting it in the engine would undo the
boundary the last three phases established.

What the engine gains is the mechanism the generator drives:

- **Kit catalog**: pieces, their footprints, their sockets (which edges accept
  a wall, a door, an arch), authored in data.
- **Modular assembly**: place a piece at a cell with a rotation, resolve its
  socket against its neighbours, emit the instance.
- **Instancing and batching**: what `Renderer::createStaticBatch` already does,
  driven from the assembly output rather than from `DungeonMap`.

So: `eng::render::KitCatalog` + `eng::scene::ModularAssembler` in the engine,
and the game keeps a much smaller generator that decides *what* to build.

## Levels as JSON source

`.scn` becomes the editable source; `.map` does not become editor authority.
Two things worth being precise about, because "the universal JSON
game engines use" is not one thing:

- **glTF is the interchange format** and it is JSON, but it describes meshes,
  materials and node hierarchies -- not colliders, spawn points, triggers or
  pickups. It is the right format for the *kit*, not for a level.
- So levels get **our own JSON schema**, which is what Unity, Unreal and Godot
  each do too (they just use YAML/binary/tscn instead of JSON).

The component registry remains the cooked wire table. A renderer-independent
content library owns `SceneDocument`, JSON load/save, stable author IDs,
validation and deterministic cooking into `MapSerializer`. Text source levels
diff, merge and hand-edit; runtime maps stay compact and disposable.

Needs a JSON library -- nlohmann/json via CPM, consistent with how every other
dependency arrives.

## Order

1. **Kit landed.** 44 OBJs converted, textures verified present. *Done.*
2. **Kit catalog + materials.** Map each piece to a material from the existing
   `game.material` set; author the catalog with footprints and sockets.
3. **Assembler in the engine.** Placement, socket resolution, batching. Tested
   headlessly -- given a cell grid, assert the instances that come out.
4. **New generator in the game.** Rooms, corridors, doors, stairs, verticality
   (the kit has three stair widths and half-height blocks; the current dungeon
   is one storey).
5. **Cut over.** Game and demo scenes build from the new path; delete
   `DungeonGen`/`DungeonMap` and the six old tiles.
6. **JSON scene sources.** Schema, scene IR, cooker and editor write `.scn`;
   `mapgen` and the cooker emit derived `.map` files.

Steps 3 and 4 are the bulk. Step 5 is where the game changes visually, and
should be one commit that can be reverted whole.

## Risk

The current dungeon works and looks finished. A rewrite that stalls halfway
leaves the game worse than it started, so each step above ends with the game
running on the *old* path until step 5 flips it in one move.
