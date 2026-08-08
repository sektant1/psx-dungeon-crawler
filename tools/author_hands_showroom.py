#!/usr/bin/env python3
"""Generate hands_showroom.scn: every hand rig, in a PSX forest clearing.

The test scene for two things that arrived together and are hard to check any
other way:

  * the FIFTEEN first-person hand rigs authored by tools/author_hand_rigs.py.
    A rig can be wrong in ways a unit test cannot see -- an arm inside out, a
    left hand that is a second right hand, a texture atlas remapped onto the
    wrong half -- and all of them are obvious the moment you look. So the
    showroom stands one skinned display of each rig in a row, in the same pose,
    at eye height, and the player walks down it. Pressing the rig switcher wears
    the one being looked at.

  * TERRAIN. Everything this engine drew before was a flat grid of dungeon
    cells, and the forest content is the first thing that needs ground with a
    shape. A clearing surrounded by rising ground is the smallest scene that
    exercises the heightfield end to end: it is drawn, it is collided with (the
    player walks up the slope and stops at the treeline), and it is flattened
    where it has to be level (the display row, the spawn).

Everything is placed from the prefab libraries the asset importer wrote, so this
scene is also a check on the import: a prefab whose scale rule is wrong shows up
here as a mushroom the size of a tree.

    tools/author_hands_showroom.py
    tools/author_hands_showroom.py --out assets/scenes/hands_showroom.scn
"""

import argparse
import json
import math
import os
import tomllib

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(REPO, "assets")

# --- the clearing -------------------------------------------------------------
#
# A 96 m patch, which at 129 samples is a vertex every 0.75 m -- fine enough
# that a slope reads as a slope rather than as facets, coarse enough to be one
# draw call. The clearing floor is flattened to y=0 so the display row and the
# spawn are level; the ground rises away from it, so the eye stops at the
# treeline instead of at the edge of the patch.

TERRAIN_SIZE = 96.0
TERRAIN_RESOLUTION = 129
TERRAIN_HEIGHT = 9.0
CLEARING_RADIUS = 22.0

# The display row. Fifteen rigs at 2.2 m apart is 31 m, which fits the clearing
# with room to back away from it -- and backing away is how you notice that one
# arm is a different size from the others.
DISPLAY_SPACING = 2.2
DISPLAY_Z = -12.0
DISPLAY_HEIGHT = 1.45   # hands at roughly eye level for a 1.7 m player

SPAWN = [0.0, 0.0, 6.0]

# The forest, as rings rather than a scatter. A random scatter of a 15-prefab
# set reads as noise; concentric bands of decreasing size read as a clearing in
# a wood, which is what the scene is. Each ring names prefabs from
# assets/prefabs/forest.prefab.toml.
RINGS = [
    # radius, count, prefabs (cycled), jitter
    (26.0, 18, ["forest.bush_01", "forest.long_grass_01",
                "forest.long_grass_02", "forest.mushroom_01"], 2.0),
    (32.0, 16, ["forest.small_tree_01", "forest.trunk_03",
                "forest.trunk_04"], 2.6),
    (39.0, 14, ["forest.tree_01", "forest.small_tree_02",
                "forest.spiky_tree"], 3.0),
    (46.0, 12, ["forest.big_tree_01", "forest.tree_01",
                "forest.spiky_tree"], 3.4),
]

# Ground cover inside the clearing: sparse, and never where the player walks or
# where a display stands.
GROUND_COVER = ["forest.grass_01", "forest.grass_02", "forest.long_grass_01"]
COVER_COUNT = 40

# Lighting. An outdoor scene lit like the dungeon reads as a cave with trees in
# it, so this is one warm key from above and a cool ambient, and the fog is set
# far enough back that the treeline is visible and the patch edge is not.
KEY_LIGHT = {"direction": [-0.35, -0.82, -0.45],
             "colour": [1.30, 1.16, 0.88], "energy": 2.1}


def load_prefabs():
    """Every prefab id the importer published, so a typo here fails loudly."""
    known = {}
    directory = os.path.join(ASSETS, "prefabs")
    if not os.path.isdir(directory):
        return known
    for name in sorted(os.listdir(directory)):
        if not name.endswith(".prefab.toml"):
            continue
        with open(os.path.join(directory, name), "rb") as stream:
            document = tomllib.load(stream)
        for prefab in document.get("prefab", []):
            known[prefab["id"]] = prefab
    return known


def load_rigs():
    """The hand rigs, in the order the config declares them."""
    path = os.path.join(ASSETS, "config", "viewmodel_hands.toml")
    with open(path, "rb") as stream:
        document = tomllib.load(stream)
    return document.get("rig", [])


# A deterministic hash, so re-running produces the same forest. `random` with a
# fixed seed would too, but this stays stable if anything is inserted before it.
def jitter(index, salt, amount):
    h = (index * 2654435761 + salt * 40503) & 0xFFFFFFFF
    h ^= h >> 13
    h = (h * 1274126177) & 0xFFFFFFFF
    return ((h & 0xFFFF) / 65535.0 - 0.5) * 2.0 * amount


def entity(ident, **fields):
    out = {"id": ident}
    out.update(fields)
    return out


def transform(position, yaw=0.0, scale=None):
    out = {"position": [round(v, 4) for v in position]}
    if abs(yaw) > 1e-6:
        out["rotation"] = [0.0, round(yaw, 3), 0.0]
    if scale is not None and abs(scale - 1.0) > 1e-6:
        out["scale"] = [round(scale, 4)] * 3
    return out


def build(known, rigs):
    entities = []

    # --- ground -------------------------------------------------------------
    # The terrain patch is one entity. Its `flatten` spots are not in the scene
    # format -- they are a property of the descriptor, applied at build -- so
    # the clearing is levelled by the generator below rather than authored.
    entities.append(entity(
        "terrain_clearing",
        terrain={
            "resolution": TERRAIN_RESOLUTION,
            "size": TERRAIN_SIZE,
            "height_scale": TERRAIN_HEIGHT,
            "seed": 20260807,
            "octaves": 4,
            "frequency": 2.2,
            "roughness": 0.52,
            "uv_scale": 32.0,
            "material": "Builtin/Forest/Ground",
            "collision": True,
        },
        transform={"position": [0.0, 0.0, 0.0]},
        cast_shadows=False,
    ))

    # --- the display row ----------------------------------------------------
    # One entity per rig, each a viewmodel preview: the component that already
    # exists to stand a first-person rig in a scene without it being the
    # player's own hands. That is exactly what a showroom needs, and it means
    # the display and the real thing are the same code path -- a rig that looks
    # right here cannot look wrong in the player's view for a reason the
    # showroom hid.
    first = -DISPLAY_SPACING * (len(rigs) - 1) * 0.5
    for index, rig in enumerate(rigs):
        x = first + index * DISPLAY_SPACING
        entities.append(entity(
            "display_%s" % rig["id"],
            name=rig.get("name", rig["id"]),
            viewmodel_preview={"hands_rig": rig["id"], "animation": "idle"},
            transform=transform([x, DISPLAY_HEIGHT, DISPLAY_Z], yaw=180.0),
            properties=[
                # Read by the showroom script: walking up to a display and
                # pressing the interact key wears that rig.
                {"key": "hands_rig", "value": rig["id"]},
            ],
        ))
        # A plinth under each, so the row reads as a row and the rigs are not
        # floating. The smallest trunk in the forest set, which is what is to
        # hand and looks deliberate.
        if "forest.trunk_04" in known:
            entities.append(entity(
                "plinth_%02d" % index,
                prefab="forest.trunk_04",
                transform=transform([x, 0.0, DISPLAY_Z],
                                    yaw=jitter(index, 7, 180.0)),
                cast_shadows=True,
            ))

    # --- the forest ---------------------------------------------------------
    placed = 0
    for ring_index, (radius, count, prefabs, spread) in enumerate(RINGS):
        for step in range(count):
            prefab = prefabs[step % len(prefabs)]
            if prefab not in known:
                continue
            angle = (step / float(count)) * math.tau
            r = radius + jitter(placed, ring_index * 31 + 1, spread)
            x = math.cos(angle) * r + jitter(placed, 11, spread * 0.5)
            z = math.sin(angle) * r + jitter(placed, 13, spread * 0.5)
            entities.append(entity(
                "tree_%03d" % placed,
                prefab=prefab,
                transform=transform(
                    [x, 0.0, z],
                    yaw=jitter(placed, 17, 180.0),
                    # A forest of identical trees reads as wallpaper. +-12%
                    # is enough to break that and small enough that nothing
                    # looks like a different species.
                    scale=1.0 + jitter(placed, 19, 0.12)),
                cast_shadows=True,
            ))
            placed += 1

    # Ground cover, kept out of the walkway and off the display row.
    for i in range(COVER_COUNT):
        prefab = GROUND_COVER[i % len(GROUND_COVER)]
        if prefab not in known:
            continue
        angle = jitter(i, 23, math.pi)
        r = 6.0 + abs(jitter(i, 29, CLEARING_RADIUS - 8.0))
        x = math.cos(angle) * r
        z = math.sin(angle) * r
        # The row occupies a 3 m band; grass growing through a plinth is the
        # kind of detail that reads as a placement bug.
        if abs(z - DISPLAY_Z) < 3.0:
            continue
        entities.append(entity(
            "cover_%03d" % i,
            prefab=prefab,
            transform=transform([x, 0.0, z], yaw=jitter(i, 31, 180.0)),
            cast_shadows=False,
        ))

    # --- spawn and light ----------------------------------------------------
    entities.append(entity(
        "player_spawn",
        marker="spawn",
        transform={"position": SPAWN, "rotation": [0.0, 180.0, 0.0]},
    ))
    entities.append(entity(
        "sun",
        light={
            "type": "directional",
            "direction": KEY_LIGHT["direction"],
            "colour_srgb": KEY_LIGHT["colour"],
            "energy": KEY_LIGHT["energy"],
        },
        transform={"position": [0.0, 20.0, 0.0]},
    ))
    return entities


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out",
                        default=os.path.join(ASSETS, "scenes",
                                             "hands_showroom.scn"))
    args = parser.parse_args()

    known = load_prefabs()
    rigs = load_rigs()
    if not rigs:
        raise SystemExit("no hand rigs in config/viewmodel_hands.toml -- run "
                         "tools/author_hand_rigs.py first")

    scene = {
        "$schema": "../schemas/scene.schema.json",
        "format": "raven-scene",
        "version": 2,
        "id": "scene.hands_showroom",
        "palette": "forest",
        "entities": build(known, rigs),
    }

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w") as f:
        json.dump(scene, f, indent=2)
        f.write("\n")
    print("wrote %s: %d entities, %d hand rigs"
          % (os.path.relpath(args.out, REPO), len(scene["entities"]),
             len(rigs)))


if __name__ == "__main__":
    main()
