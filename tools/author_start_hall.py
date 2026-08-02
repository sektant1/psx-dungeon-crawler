#!/usr/bin/env python3
"""Authors the editor's start scene: assets/scenes/start_hall.scn.

THE LEVEL, IN ONE PARAGRAPH. You wake in a shrine alcove -- the one cold blue
thing in a warm dead room. The way out is a lit arch straight ahead, visible
from where you stand, and between you and it the ceiling has come down and
something is standing in the light. Nothing here is hidden; everything here is a
decision about which side of the rubble you take.

    python3 tools/author_start_hall.py assets/scenes/start_hall.scn
    ./build/scene_cook assets/scenes/start_hall.scn --kit assets/config/kit.toml \\
        --validate-only

The scene file is the artifact -- edit it in the editor from here on. This script
is kept because the *layout* is arithmetic (cell centres, the half-thickness wall
inset, the y_offset exceptions), it mirrors editor/src/content/GridMath.cpp, and the
cooker's validator proves the two still agree: a mismatch surfaces as
cell.transform_drift. The design rationale is docs/levels/start_hall.md.
"""
import json
import sys

CELL = 4.0
INSET = 0.5
EDGE_YAW = {"north": 0.0, "east": 90.0, "south": 180.0, "west": -90.0}
EDGE_DIR = {"north": (0, -1), "east": (1, 0), "south": (0, 1), "west": (-1, 0)}
KIT_SCALE = 0.2

# y_offset in kit units for the pieces that need one, straight from kit.toml.
Y_OFF = {
    "kit.arch": 4.81, "kit.arch_fence": 4.84, "kit.arch_roof": 5.52,
    "kit.barrel": -4.0, "kit.box": -4.0, "kit.chandelier": -13.0,
    "kit.table": -5.0, "kit.wall_decor": -4.38,
}

ents = []


def add(id, prefab=None, pos=(0, 0, 0), rot=(0, 0, 0), scale=None, **extra):
    e = {"id": id}
    if prefab:
        e["prefab"] = prefab
    t = {}
    if any(abs(v) > 1e-6 for v in pos):
        t["position"] = [round(v, 4) for v in pos]
    if any(abs(v) > 1e-6 for v in rot):
        t["rotation_degrees"] = [round(v, 4) for v in rot]
    if scale:
        t["scale"] = [round(v, 4) for v in scale]
    if t:
        e["transform"] = t
    e.update(extra)
    ents.append(e)
    return e


def centre(col, row):
    return ((col + 0.5) * CELL, 0.0, (row + 0.5) * CELL)


def floor(id, col, row, prefab="kit.floor"):
    x, y, z = centre(col, row)
    add(id, prefab, (x, y, z), cell={"col": col, "row": row})


def edge_piece(id, col, row, edge, prefab, quarters=0):
    cx, _, cz = centre(col, row)
    dx, dz = EDGE_DIR[edge]
    out = CELL * 0.5 + INSET
    y = Y_OFF.get(prefab, 0.0) * KIT_SCALE
    yaw = (EDGE_YAW[edge] + quarters * 90.0) % 360.0
    cellv = {"col": col, "row": row, "edge": edge}
    if quarters:
        cellv["yaw_quarters"] = quarters
    add(id, prefab, (cx + dx * out, y, cz + dz * out), (0.0, yaw, 0.0), cell=cellv)


def prop(id, prefab, pos, yaw=0.0, scale=None, roll=(0.0, 0.0)):
    return add(id, prefab, pos, (roll[0], yaw, roll[1]), scale)


def part(id, prefab, parent):
    """The second half of a two-part prop, parented at zero offset.

    A barrel and its hoops, a table and its boards: two meshes with two
    materials at one spot. Parenting makes them ONE row in the outliner and one
    thing to drag, instead of a pair an author has to remember to move together.
    """
    return add(id, prefab, parent=parent)


# =============================================================================
# SHELL
# Hall: cols 0..2, rows 0..2 (12 x 12 m). Shrine alcove: cell (1,3).
# The north wall opens through an arch -- the exit, visible from the spawn.
# =============================================================================
n = 0
for row in range(3):
    for col in range(3):
        n += 1
        floor(f"floor_{n:04d}", col, row)
floor("floor_0010", 1, 3)
# A vestibule one cell BEYOND the north arch. Without it the way out is a black
# hole in a lit wall -- the eye-level capture showed exactly that -- and the
# cold light had no surface to land on. With a floor and two side walls the exit
# reads as a passage with depth, which is what makes it legible as a way out
# rather than a shadow.
floor("floor_0011", 1, -1)

walls = []
for col in range(3):
    if col != 1:  # the middle bay is the way out
        walls.append((col, 0, "north", "kit.wall"))
for row in range(3):
    # The west wall is the one that failed: it is the reason for the rubble
    # spilling into the room, and it is where the timber props are.
    walls.append((0, row, "west", "kit.wall_ruin" if row == 1 else "kit.wall"))
    walls.append((2, row, "east", "kit.wall" if row != 1 else "kit.wall_window"))
walls.append((0, 2, "south", "kit.wall"))
walls.append((2, 2, "south", "kit.wall_skull"))
for edge in ("west", "east", "south"):  # the alcove's own three sides
    walls.append((1, 3, edge, "kit.wall_skull"))
for edge in ("west", "east", "north"):  # the vestibule beyond the arch
    walls.append((1, -1, edge, "kit.wall"))
for i, (col, row, edge, prefab) in enumerate(walls, 1):
    edge_piece(f"wall_{i:04d}", col, row, edge, prefab)

edge_piece("arch_0001", 1, 2, "south", "kit.arch")  # to the shrine
edge_piece("arch_0002", 1, 0, "north", "kit.arch")  # the way out

# Pillars on the corner POINTS: that is where two wall slabs meet at a point and
# leave a notch, and a pillar is what plugs it.
PILLAR_Y = 4.0 / (30.2 * KIT_SCALE)  # authored 6.04 m, the room is 4 m
for i, (x, z) in enumerate([(0.0, 0.0), (12.0, 0.0), (0.0, 12.0), (12.0, 12.0),
                            (4.0, 12.0), (8.0, 12.0), (4.0, 16.0), (8.0, 16.0),
                            (4.0, 0.0), (8.0, 0.0), (4.0, -4.0), (8.0, -4.0)], 1):
    prop(f"pillar_{i:04d}", "kit.pillar", (x, 0.0, z), scale=(1.0, PILLAR_Y, 1.0))

prop("chandelier_0001", "kit.chandelier", (6.0, 3.6, 6.0))
part("chandelier_0001_chain", "kit.chain", "chandelier_0001")

# =============================================================================
# SHRINE (alcove, z 12..16) -- refuge, landmark, and the only cold light
# The player starts here with a wall at their back and the whole hall in view:
# prospect from refuge, which is where a level should put someone who has just
# arrived and does not yet know the room.
# =============================================================================
SHRINE = (6.0, 14.9)
prop("crystal_0001", "kit.crystal_ground", (SHRINE[0], 0.0, SHRINE[1]),
     yaw=15.0, scale=(0.45, 0.5, 0.45))
prop("crystal_0002", "kit.crystal_spire_tall",
     (SHRINE[0] - 1.05, 0.0, SHRINE[1] - 0.3), yaw=20.0, scale=(0.4, 0.5, 0.4))
prop("crystal_0003", "kit.crystal_spire_mid",
     (SHRINE[0] + 1.0, 0.0, SHRINE[1] - 0.15), yaw=-35.0, scale=(0.36, 0.45, 0.36))
prop("crystal_0004", "kit.crystal_spire_low",
     (SHRINE[0] + 0.35, 0.0, SHRINE[1] + 0.75), yaw=70.0, scale=(0.3, 0.4, 0.3))
prop("shaft_0001", "kit.light_shaft", (SHRINE[0], 1.7, SHRINE[1]),
     scale=(0.22, 0.45, 0.22))
prop("chest_0001", "kit.prop_chest", (SHRINE[0] - 1.5, 0.08, SHRINE[1] - 1.9),
     yaw=25.0)
# Arms left at the shrine: the first hint that people came in and did not leave.
prop("sword_0001", "kit.item_sword", (4.55, 0.46, 14.2), yaw=95.0,
     scale=(0.1, 0.1, 0.1))
prop("shield_0001", "kit.item_shield", (7.45, 0.71, 14.2), yaw=-95.0,
     scale=(0.1, 0.1, 0.1))
for i, (x, z) in enumerate([(4.9, 13.1), (7.1, 13.1)], 1):
    prop(f"candle_{i:04d}", "kit.candle_cluster", (x, 0.0, z))

# =============================================================================
# CAMP (west, z 6..11) -- the story beat: somebody waited here, and ate
# Clustered rather than smeared along the wall. A camp is a *place*; props
# spread evenly around a room are wallpaper, and the room this replaced was
# wallpaper on all four sides.
# =============================================================================
prop("table_0001", "kit.prop_table", (2.3, 0.876, 8.6), yaw=90.0)
part("table_0001_top", "kit.prop_table_top", "table_0001")
prop("bread_0001", "kit.item_bread", (2.0, 1.42, 8.95), yaw=25.0)
prop("pumpkin_0001", "kit.item_pumpkin", (2.6, 1.47, 8.25), yaw=-15.0)
prop("candle_0003", "kit.candle", (2.3, 1.39, 8.0))
prop("candle_0004", "kit.candle_stub", (3.05, 0.0, 7.6))
for i, (x, z, yaw) in enumerate([(1.1, 7.4, 15.0), (1.85, 7.05, -35.0),
                                 (1.0, 9.9, 40.0)], 1):
    prop(f"sack_{i:04d}", "kit.prop_jutesack", (x, 0.0, z), yaw=yaw)
prop("crate_0001", "kit.prop_crate", (3.0, 0.0, 9.5), yaw=-15.0)
prop("crate_0002", "kit.prop_crate", (3.05, 0.244, 9.55), yaw=30.0)
prop("hay_0001", "kit.prop_haybale", (2.5, 0.0, 10.5), yaw=-65.0)  # bedding
prop("barrel_0001", "kit.prop_barrel", (1.0, 0.0, 6.3), yaw=12.0)
part("barrel_0001_bands", "kit.prop_barrel_bands", "barrel_0001")
prop("barrel_0002", "kit.prop_barrel_open", (1.9, 0.0, 10.8), yaw=-40.0)
part("barrel_0002_bands", "kit.prop_barrel_open_bands", "barrel_0002")
prop("lamp_0001", "kit.prop_lamp", (2.4, 2.7, 9.0))
# The reward that makes the detour worth taking, in plain view from the arch.
add("pickup_0001", None, (2.3, 1.45, 8.6), pickup="potion", name="Camp Potion")

# =============================================================================
# COLLAPSE (west to centre, z 4..8) -- the pinch, and the cover
# The failed wall spills into the room and narrows the crossing to about three
# metres. It is the level's one bottleneck: it slows the walk, it splits the
# approach into two lanes, and it is chest-high cover for whoever reaches it.
# =============================================================================
prop("beam_0001", "kit.prop_beam_corner", (0.9, 0.0, 5.2))
prop("beam_0002", "kit.prop_beam", (0.7, 0.0, 3.6), yaw=15.0, scale=(1.0, 2.0, 1.0))
prop("beam_0003", "kit.prop_beam", (0.75, 0.0, 6.9), yaw=-10.0, scale=(1.0, 2.0, 1.0))
prop("debris_0001", "kit.debris", (2.3, 0.0, 6.3), yaw=25.0)
prop("debris_0002", "kit.debris", (3.7, 0.0, 5.8), yaw=-40.0)
prop("debris_0003", "kit.debris", (4.5, 0.0, 6.9), yaw=70.0)
prop("pillar_fallen_0001", "kit.pillar_fallen", (3.4, 0.0, 6.4), yaw=70.0)
prop("pillar_broken_0001", "kit.pillar_broken", (4.5, 0.0, 7.6))
prop("door_0001", "kit.prop_door", (1.05, 0.0, 4.3), yaw=90.0, roll=(0.0, -8.0))
# The tight lane's reward, in the rubble: pay for it in exposure, not in health.
add("pickup_0002", None, (3.9, 0.35, 6.7), pickup="scrap", name="Rubble Scrap")

# =============================================================================
# EAST STAGGER (z 4..11) -- the other lane
# Deliberately NOT mirrored: the east crossing is wider and more open, so the
# choice at the pinch is "tight and covered" against "fast and exposed".
# =============================================================================
prop("box_0001", "kit.box", (10.6, 0.8, 8.7), yaw=15.0)
prop("box_0002", "kit.box", (11.3, 0.8, 9.6), yaw=-20.0)
prop("kitbarrel_0001", "kit.barrel", (9.7, 0.8, 9.2))
prop("kitchest_0001", "kit.chest", (11.0, 0.0, 10.7), yaw=200.0)
prop("vase_0001", "kit.prop_vase", (11.2, 0.0, 7.1), yaw=30.0)
part("vase_0001_lid", "kit.prop_vase_lid", "vase_0001")
prop("vase_0002", "kit.prop_vase", (10.4, 0.0, 6.3), yaw=-20.0)
part("vase_0002_lid", "kit.prop_vase_lid", "vase_0002")
prop("debris_0004", "kit.debris", (9.4, 0.0, 10.9), yaw=15.0)
prop("hay_0002", "kit.prop_haybale", (10.2, 0.0, 4.9), yaw=20.0)
for i, (x, z, yaw) in enumerate([(9.3, 5.8, -20.0), (10.7, 5.6, 40.0)], 4):
    prop(f"sack_{i:04d}", "kit.prop_jutesack", (x, 0.0, z), yaw=yaw)

# =============================================================================
# THE FIGHT FLOOR (north half, z 0.6..4.6)
# Cover first, enemies second: two clusters at chest height with lanes between
# them, so the encounter has somewhere to be fought *from* rather than across.
# =============================================================================
prop("barrel_0003", "kit.prop_barrel", (5.0, 0.0, 4.3), yaw=25.0)
part("barrel_0003_bands", "kit.prop_barrel_bands", "barrel_0003")
prop("barrel_0004", "kit.prop_barrel_open", (8.3, 0.0, 3.6), yaw=-18.0)
part("barrel_0004_bands", "kit.prop_barrel_open_bands", "barrel_0004")
prop("crate_0003", "kit.prop_crate", (5.7, 0.0, 3.9), yaw=25.0)
prop("crate_0004", "kit.prop_crate", (5.75, 0.244, 3.95), yaw=-40.0)
prop("hay_0003", "kit.prop_haybale", (8.9, 0.0, 4.6), yaw=-25.0)
prop("sack_0006", "kit.prop_jutesack", (4.6, 0.0, 0.9), yaw=20.0)
prop("sack_0007", "kit.prop_jutesack", (7.3, 0.0, 0.85), yaw=-30.0)
prop("barrel_0005", "kit.prop_barrel", (10.6, 0.0, 0.95), yaw=-8.0)
part("barrel_0005_bands", "kit.prop_barrel_bands", "barrel_0005")
prop("door_0002", "kit.door_barred", (9.6, 0.0, 0.5), yaw=180.0,
     scale=(0.2, 0.2, 0.2))
prop("crate_0005", "kit.prop_crate", (1.4, 0.0, 1.1), yaw=-15.0)
prop("vase_0003", "kit.prop_vase", (0.9, 0.0, 2.4), yaw=45.0)
part("vase_0003_lid", "kit.prop_vase_lid", "vase_0003")

# =============================================================================
# LIGHT
# Warm where you are, cold where you are going. The exit arch carries the only
# white light in the hall, so the objective separates from a room lit entirely
# in firelight -- figure against ground, with no marker and no minimap.
# =============================================================================
for i, (x, z, yaw) in enumerate([(0.55, 3.0, -90.0), (0.55, 9.6, -90.0),
                                 (11.45, 5.2, 90.0), (11.45, 10.4, 90.0)], 1):
    prop(f"torch_{i:04d}", "kit.prop_torch", (x, 1.9, z), yaw=yaw)
for i, (x, z, yaw) in enumerate([(4.55, 14.2, -90.0), (7.45, 14.2, 90.0)], 5):
    prop(f"torch_{i:04d}", "kit.prop_torch", (x, 1.9, z), yaw=yaw)
# Sconces framing the way out, so the arch reads as a made thing, not a hole.
for i, (x, z) in enumerate([(5.0, 0.6), (7.0, 0.6)], 1):
    prop(f"sconce_{i:04d}", "kit.wall_decor", (x, 2.8, z))


def light(id, pos, colour, rng, name):
    add(id, None, pos, light={"type": "point", "colour": colour, "range": rng},
        name=name)


light("light_0001", (6.0, 2.8, 6.0), [1.0, 0.72, 0.42], 9.0, "Chandelier")
light("light_0002", (0.9, 2.0, 3.0), [1.0, 0.62, 0.30], 5.5, "West Torch")
light("light_0003", (0.9, 2.0, 9.6), [1.0, 0.62, 0.30], 5.5, "Camp Torch")
light("light_0004", (11.1, 2.0, 5.2), [1.0, 0.62, 0.30], 5.5, "East Torch")
light("light_0005", (2.4, 2.1, 9.0), [1.0, 0.78, 0.48], 4.5, "Camp Lamp")
light("light_0006", (6.0, 2.2, 0.2), [0.62, 0.82, 1.20], 6.5, "Exit Arch")
light("light_0008", (6.0, 1.6, -2.0), [0.55, 0.78, 1.25], 6.0, "Vestibule")
light("light_0007", (6.0, 1.8, 14.6), [0.45, 0.75, 1.25], 7.0, "Shrine")
add("key_light_0001", None, (6.0, 8.0, 6.0), (-55.0, 30.0, 0.0), name="Key Light",
    light={"type": "directional", "colour": [0.62, 0.66, 0.78],
           "cast_shadows": True})

# =============================================================================
# GAMEPLAY
# One encounter, placed so it is read before it is fought: the guards hold the
# lit arch, ten metres of open floor from the pinch, and the shrine alcove is
# the fallback the player already knows -- they started in it.
# =============================================================================
# Yaw 0 is north: the player wakes FACING the lit arch, with the shrine and the
# alcove wall at their back. Authored at 180 first, and the walk-mode capture
# showed the back of a skull wall -- which is the whole argument for looking at
# a level from the player's eye before calling it done.
add("player_spawn_0001", None, (6.0, 0.0, 13.4), player_spawn=True,
    name="Arrival")
add("exit_0001", None, (6.0, 0.0, -1.6), exit={"yaw_degrees": 0.0},
    name="Descent")
add("enemy_0001", None, (3.4, 0.0, 1.9), (0.0, 150.0, 0.0), enemy_spawn="hollow",
    name="Left Guard")
add("enemy_0002", None, (9.0, 0.0, 1.7), (0.0, 200.0, 0.0), enemy_spawn="hollow",
    name="Right Guard")
add("enemy_0003", None, (6.2, 0.0, 0.9), (0.0, 180.0, 0.0),
    enemy_spawn="crossbow_hollow", name="Arch Shooter")
# Fires as the player leaves the alcove: the hook for a stinger, a door, or the
# guards noticing -- the reason a designer opens this scene at all.
add("trigger_0001", None, (6.0, 1.5, 11.6),
    trigger={"size": [4.0, 3.0, 1.2], "event": "start_hall.entered"},
    name="Hall Threshold")
add("marker_0001", None, (6.0, 0.0, 6.4), marker="hall.pinch", name="Pinch")
add("marker_0002", None, (2.3, 0.0, 8.6), marker="hall.camp", name="Camp")

doc = {"$schema": "../schemas/scene.schema.json", "format": "psx-dungeon-scene",
       "version": 2, "id": "scene.start_hall",
       "entities": sorted(ents, key=lambda e: e["id"])}
out = sys.argv[1]
with open(out, "w") as f:
    json.dump(doc, f, indent=2)
    f.write("\n")
print(f"{out}: {len(ents)} entities")
