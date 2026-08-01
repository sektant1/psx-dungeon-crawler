#!/usr/bin/env python3
"""Generate cozy_lair.scn: compact imported-prefab tech-demo stage."""

import argparse
import collections
import json
import math
import os
import tomllib


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", default="assets/scenes/cozy_lair.scn")
parser.add_argument("--prefab", default="kit.prop_boss_placeholder",
                    help="kit prefab id rendered as subject")
parser.add_argument("--subject-name", default="")
parser.add_argument("--subject-material", default="")
parser.add_argument("--subject-scale", type=float, default=1.0)
parser.add_argument("--subject-yaw", type=float, default=-20.0)
parser.add_argument("--subject-y", type=float, default=0.0,
                    help="additional vertical offset after automatic grounding")
parser.add_argument("--ground-clearance", type=float, default=0.02)
args = parser.parse_args()

if not args.prefab.startswith("kit."):
    parser.error("--prefab must be a kit id such as kit.prop_raccoon_head")
if args.subject_scale <= 0.0:
    parser.error("--subject-scale must be greater than zero")
if args.ground_clearance < 0.0:
    parser.error("--ground-clearance cannot be negative")


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KIT_PATH = os.path.join(ROOT, "assets/config/kit.toml")
with open(KIT_PATH, "rb") as stream:
    CATALOG = tomllib.load(stream)
KIT = CATALOG["kit"]
PIECES = {"kit." + piece["id"]: piece for piece in CATALOG["piece"]}


def prefab_geometry(prefab):
    piece = PIECES.get(prefab)
    if piece is None:
        parser.error("unknown prefab in assets/config/kit.toml: " + prefab)

    mesh = piece["mesh"]
    if "/" not in mesh:
        mesh = KIT["mesh_dir"] + "/" + mesh
    mesh_path = os.path.join(ROOT, "assets", mesh)
    minimum = [math.inf, math.inf, math.inf]
    maximum = [-math.inf, -math.inf, -math.inf]
    try:
        with open(mesh_path, "r", encoding="utf-8", errors="replace") as stream:
            for line in stream:
                if not line.startswith("v "):
                    continue
                position = [float(value) for value in line.split()[1:4]]
                for axis in range(3):
                    minimum[axis] = min(minimum[axis], position[axis])
                    maximum[axis] = max(maximum[axis], position[axis])
    except OSError as error:
        parser.error("cannot read prefab mesh %s: %s" % (mesh_path, error))
    if math.isinf(minimum[0]):
        parser.error("prefab mesh contains no OBJ positions: " + mesh_path)

    import_scale = float(piece.get("import_scale", 0.0))
    mesh_scale = import_scale if import_scale > 0.0 else float(KIT["scale"])
    return minimum, maximum, mesh_scale


entities = []


def add(**values):
    entity = collections.OrderedDict()
    entity["id"] = values.pop("id")
    for key in ("name", "parent", "prefab", "material"):
        if key in values:
            entity[key] = values.pop(key)
    transform = collections.OrderedDict()
    if "pos" in values:
        transform["position"] = [float(value) for value in values.pop("pos")]
    if "rot" in values:
        transform["rotation_degrees"] = [float(value) for value in values.pop("rot")]
    if "scale" in values:
        transform["scale"] = [float(value) for value in values.pop("scale")]
    if transform:
        entity["transform"] = transform
    entity.update(values)
    entities.append(entity)


cells = [-2.0, 2.0]
for index, (x, z) in enumerate(((x, z) for z in cells for x in cells), 1):
    add(id="floor_%04d" % index, prefab="kit.floor", pos=[x, 0.0, z])
for index, x in enumerate(cells, 1):
    add(id="wall_north_%04d" % index, prefab="kit.wall", pos=[x, 0.0, -4.0])
    add(id="wall_south_%04d" % index, prefab="kit.wall", pos=[x, 0.0, 4.0])
for index, z in enumerate(cells, 1):
    add(id="wall_west_%04d" % index, prefab="kit.wall",
        pos=[-4.0, 0.0, z], rot=[0.0, 90.0, 0.0])
    add(id="wall_east_%04d" % index, prefab="kit.wall",
        pos=[4.0, 0.0, z], rot=[0.0, 90.0, 0.0])

subject_shader = {
    "rim_colour": [1.0, 0.36, 0.18],
    "rim_strength": 0.55,
    "rim_power": 3.2,
}
default_prefab = "kit.prop_boss_placeholder"
subject_name = args.subject_name or (
    "Imported Boss Reference" if args.prefab == default_prefab else
    args.prefab.removeprefix("kit.").replace("_", " ").title()
)
yaw = math.radians(args.subject_yaw)


def include_bounds(bounds_min, bounds_max, source_min, source_max,
                   source_scale, offset):
    for x in (source_min[0], source_max[0]):
        for y in (source_min[1], source_max[1]):
            for z in (source_min[2], source_max[2]):
                local_x = offset[0] + x * source_scale
                local_y = offset[1] + y * source_scale
                local_z = offset[2] + z * source_scale
                rotated_x = math.cos(yaw) * local_x + math.sin(yaw) * local_z
                rotated_z = -math.sin(yaw) * local_x + math.cos(yaw) * local_z
                point = (rotated_x, local_y, rotated_z)
                for axis in range(3):
                    bounds_min[axis] = min(bounds_min[axis], point[axis])
                    bounds_max[axis] = max(bounds_max[axis], point[axis])


composed_min = [math.inf, math.inf, math.inf]
composed_max = [-math.inf, -math.inf, -math.inf]


def include_prefab(prefab, offset, parent_scale, stack):
    if prefab in stack:
        parser.error("attachment cycle in assets/config/kit.toml: " + prefab)
    minimum, maximum, mesh_scale = prefab_geometry(prefab)
    node_scale = parent_scale * mesh_scale
    include_bounds(composed_min, composed_max, minimum, maximum, node_scale,
                   offset)
    for attachment in PIECES[prefab].get("attachments", []):
        position = attachment["position"]
        child_offset = [offset[axis] + float(position[axis]) * node_scale
                        for axis in range(3)]
        include_prefab(attachment["prefab"], child_offset, node_scale,
                       stack + (prefab,))


include_prefab(args.prefab, [0.0, 0.0, 0.0], args.subject_scale, ())

subject_position = [
    -(composed_min[0] + composed_max[0]) * 0.5,
    args.subject_y + args.ground_clearance - composed_min[1],
    -(composed_min[2] + composed_max[2]) * 0.5,
]
add(id="subject_pivot", name=subject_name + " Pivot",
    spin={"axis": [0.0, 1.0, 0.0], "degrees_per_second": 12.0})
subject = {
    "id": "subject",
    "name": subject_name,
    "parent": "subject_pivot",
    "prefab": args.prefab,
    "pos": subject_position,
    "rot": [0.0, args.subject_yaw, 0.0],
    "shader": subject_shader,
}
if args.subject_material:
    subject["material"] = args.subject_material
if args.subject_scale != 1.0:
    subject["scale"] = [args.subject_scale] * 3
add(**subject)

add(id="portal", name="Tech Backdrop", prefab="kit.portal_membrane",
    pos=[0.0, 0.0, -3.6], scale=[0.86, 0.86, 0.86], cast_shadows=False,
    exit={"yaw_degrees": 0.0}, portal={})

add(id="stage_key", name="Warm Key", pos=[-1.5, 2.5, 1.8],
    light={"type": "point", "colour": [1.55, 0.72, 0.38], "range": 5.5})
add(id="stage_fill", name="Cold Fill", pos=[1.7, 1.5, 1.5],
    light={"type": "point", "colour": [0.30, 0.42, 0.78], "range": 4.5})
add(id="stage_rim", name="Scarlet Rim", pos=[0.0, 2.2, -1.5],
    light={"type": "point", "colour": [1.20, 0.24, 0.14], "range": 4.2})

add(id="camera_main", name="Static Inspection Camera", pos=[0.0, 1.45, 3.25],
    rot=[-10.0, 0.0, 0.0],
    camera={"fov_degrees": 50.0, "near_clip": 0.1,
            "far_clip": 24.0, "priority": 10})
add(id="player_start", name="Player Start", pos=[0.0, 0.0, 2.7],
    player_spawn=True)

output = os.path.abspath(args.output)
schema = os.path.relpath(os.path.abspath("assets/schemas/scene.schema.json"),
                         os.path.dirname(output))
document = collections.OrderedDict([
    ("$schema", schema),
    ("format", "psx-dungeon-scene"),
    ("version", 2),
    ("id", "scene.test.cozy_lair"),
    ("palette", "cozy_lair"),
    ("entities", entities),
])
os.makedirs(os.path.dirname(output), exist_ok=True)
with open(output, "w", encoding="utf-8") as stream:
    json.dump(document, stream, indent=2)
    stream.write("\n")
print("wrote:", output)
print("entities:", len(entities))
