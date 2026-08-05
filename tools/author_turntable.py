#!/usr/bin/env python3
"""Generate turntable.scn: the model showroom.

A small vaulted stone chamber whose only moving part is the subject turning on
the plinth. Everything else -- the room, the columns, the torches, the three
stage lights, the camera -- is fixed, so swapping the subject is the only edit
the shot ever needs and two subjects photographed a week apart are comparable.

    tools/author_turntable.py                              # rebuild as-is
    tools/author_turntable.py --subject kit.prop_raccoon_head
    tools/author_turntable.py --subject meshes/props/prop_chest.obj --fit 1.2

The subject is auto-fitted: the script measures the mesh, scales it to --fit
metres tall and stands it on the plinth. A prefab authored in centimetres and
one authored in metres therefore frame identically, which is the whole reason
this is generated rather than hand-placed.

Tuning lives in the STAGE block below. The generated .scn is also an ordinary
editor document: open it, drag a light, save. Re-running this overwrites that,
so move a value you liked back up here.
"""

import argparse
import collections
import json
import math
import os
import statistics
import tomllib


# --- stage --------------------------------------------------------------------
# The room is on the kit's 4 m grid, so every number here is a cell count or a
# height in metres measured off the art (see assets/config/kit.toml).

CELL = 4.0
COLS = [-4.0, 0.0, 4.0]          # x of each floor cell centre: 12 m wide
ROWS = [-4.0, 0.0, 4.0, 8.0]     # z of each floor cell centre: 16 m deep
WALL_X = 6.0                     # interior faces
WALL_Z_BACK = -6.0
WALL_Z_FRONT = 10.0

STOREY = 4.0                     # Wall_01 is 4 m tall
BORDER = 2.0                     # Wall_Border_01 on top of it
SPRING = STOREY + BORDER         # 6 m: where the columns meet the vault
VAULT_BASE = 1.104               # Arch_Roof's own base above its origin

PLINTH_STEP_TOP = 0.176          # top of the wide lower step
PLINTH_TOP = 0.59                # top of the two-tier dais, in metres

# The stage lights, and the reason this room photographs an object rather than
# just containing one. Warm key from the front left, cold fill from the right at
# a third of the strength, and a coloured kicker tucked *behind* the subject.
#
# Every range here is short. That is the whole trick: a light with a 5 m reach
# in a 16 m room lights the subject and lets the wall behind it fall away, which
# is what separates a dark silhouette from dark stone. Widen one and the shot
# immediately flattens into a lit box with something standing in it.
KEY = {"pos": [-2.20, 3.20, 2.30], "colour": [2.70, 1.66, 0.94], "range": 5.6}
FILL = {"pos": [2.15, 1.85, 1.60], "colour": [0.44, 0.72, 1.10], "range": 3.8}
KICK = {"pos": [0.0, 2.45, -2.25], "colour": [1.35, 0.56, 1.60], "range": 3.6}
# The two that keep the architecture off black. Wide and weak; without them the
# vault and the far corners read as a hole rather than as a room.
VAULT_GLOW = {"pos": [0.0, 6.2, 0.5], "colour": [0.44, 0.42, 0.52], "range": 16.0}
# Down the alcove behind the subject. Cold, and deliberately short of the back
# wall of the chamber itself: the dark rectangle of that doorway is what the
# subject's silhouette is read against.
ALCOVE_GLOW = {"pos": [0.0, 2.3, -9.0], "colour": [0.46, 0.60, 0.90], "range": 5.0}

TORCH_COLOUR = [1.55, 0.80, 0.38]
TORCH_RANGE = 5.5

# The camera is on the room's axis, and everything about the framing is derived
# from the fitted subject rather than typed in.
#
# Axial because the shot depends on the arch behind the subject reading as a
# dark field on both sides of it, and sliding the camera sideways slides that
# arch out from behind the subject -- eleven metres of parallax against five.
# The three-quarter interest comes from the subject's resting yaw and from the
# key light being off to the left instead, neither of which costs anything.
#
# Derived because --fit is a dial: pin the distance and a 3 m statue overflows
# the frame while a 0.3 m trinket becomes a speck. Solving the distance from the
# fill instead means every subject arrives framed the same way.
CAMERA_FOV = 45.0
FRAME_FILL = 0.64        # subject + plinth, as a fraction of frame height
EYE_FRACTION = 0.46      # eye height up the subject
# Where the subject-and-plinth block sits in the frame, as a fraction of its own
# height: 0.5 centres it, higher lifts it for headroom. Anything outside
# [1 - 1/(2*FRAME_FILL), 1/(2*FRAME_FILL)] saws off the plinth or the head.
FRAME_BIAS = 0.72
CAMERA_MAX_DISTANCE = 8.4  # stay off the front wall at z = 10


parser = argparse.ArgumentParser(
    description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument("--output", default="assets/scenes/turntable.scn")
parser.add_argument("--subject", default="kit.import_ac_vixen",
                    help="a kit prefab id (kit.foo) or an asset-relative mesh "
                         "path (meshes/props/foo.obj)")
parser.add_argument("--subject-name", default="")
parser.add_argument("--subject-material", default="",
                    help="override the material the prefab or mesh wears")
parser.add_argument("--fit", type=float, default=2.10,
                    help="scale the subject to this height in metres; 0 keeps "
                         "the asset's own size")
parser.add_argument("--scale", type=float, default=1.0,
                    help="multiplies whatever --fit worked out")
parser.add_argument("--yaw", type=float, default=-22.0,
                    help="the subject's resting yaw, which is where the spin "
                         "starts and what a single-frame screenshot sees")
parser.add_argument("--spin", type=float, default=16.0,
                    help="degrees per second; 0 parks it")
parser.add_argument("--lift", type=float, default=0.0,
                    help="extra height above the plinth, for a subject that "
                         "should hover")
parser.add_argument("--clearance", type=float, default=0.01,
                    help="gap between the subject's lowest vertex and the "
                         "plinth, so it does not z-fight the stone")
parser.add_argument("--list-subjects", action="store_true",
                    help="print every id --subject will accept and exit")
args = parser.parse_args()

if args.fit < 0.0:
    parser.error("--fit cannot be negative")
if args.scale <= 0.0:
    parser.error("--scale must be greater than zero")


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
with open(os.path.join(ROOT, "assets/config/kit.toml"), "rb") as stream:
    CATALOG = tomllib.load(stream)
KIT = CATALOG["kit"]
PIECES = {"kit." + piece["id"]: piece for piece in CATALOG["piece"]}

if args.list_subjects:
    # Grouped by the catalogue's own role, because "which of these 180 ids is a
    # thing I would want on a plinth" is the actual question being asked.
    by_role = collections.defaultdict(list)
    for identifier, piece in PIECES.items():
        by_role[piece.get("role", "other")].append(identifier)
    for role in sorted(by_role):
        print("%s:" % role)
        for identifier in sorted(by_role[role]):
            print("   ", identifier)
    print("\nAny mesh path under assets/ also works, e.g. "
          "meshes/props/prop_malenia.obj")
    raise SystemExit(0)


def mesh_bounds(relative_path):
    """Measure an .obj in its own units. None if unmeasurable.

    Returns (min, max, middle) where *middle* is the per-axis **median** vertex
    position, not the centre of the box.

    That distinction is the whole reason this function exists. The box is right
    for how tall a thing is and for where its feet are; it is badly wrong for
    where a thing *is*. A figure holding a spear out to one side has a box two
    and a half metres wide around a body forty centimetres wide, so centring the
    turntable on the box centre stands the body off the plinth and swings it
    round a point in mid-air beside itself. The median ignores the spear -- a
    few hundred vertices out on a shaft cannot move the fiftieth percentile of
    forty thousand vertices in the body -- and lands on the bulk of the mesh,
    which is what "centred" means to anyone looking at the screenshot.
    """
    path = os.path.join(ROOT, "assets", relative_path)
    if not path.lower().endswith(".obj"):
        return None
    minimum = [math.inf] * 3
    maximum = [-math.inf] * 3
    columns = ([], [], [])
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as stream:
            for line in stream:
                if not line.startswith("v "):
                    continue
                position = [float(value) for value in line.split()[1:4]]
                for axis in range(3):
                    minimum[axis] = min(minimum[axis], position[axis])
                    maximum[axis] = max(maximum[axis], position[axis])
                    columns[axis].append(position[axis])
    except OSError as error:
        parser.error("cannot read %s: %s" % (path, error))
    if math.isinf(minimum[0]):
        parser.error("no OBJ positions in " + path)
    middle = [statistics.median(column) for column in columns]
    return minimum, maximum, middle


def subject_geometry(spec):
    """(composed_min, composed_max, middle, mesh_scale). None if unmeasurable.

    Two answers, because two questions. The *composed box* folds a compound
    prefab's attachments in and decides how big the thing is -- the sword a boss
    holds is part of how tall it stands on screen, and part of what must clear
    the plinth. The *middle* is the root piece's median vertex and decides where
    the turntable's axis goes; see mesh_bounds for why that is not the box.
    """
    if not spec.startswith("kit."):
        measured = mesh_bounds(spec)
        if measured is None:
            return None
        return measured[0], measured[1], measured[2], 1.0

    piece = PIECES.get(spec)
    if piece is None:
        parser.error("unknown prefab in assets/config/kit.toml: " + spec)

    composed_min = [math.inf] * 3
    composed_max = [-math.inf] * 3
    root_middle = [0.0, 0.0, 0.0]

    def include(prefab, offset, parent_scale, stack):
        if prefab in stack:
            parser.error("attachment cycle in kit.toml: " + prefab)
        part = PIECES[prefab]
        mesh = part["mesh"]
        if "/" not in mesh:
            mesh = KIT["mesh_dir"] + "/" + mesh
        measured = mesh_bounds(mesh)
        if measured is None:
            parser.error("cannot measure prefab mesh: " + mesh)
        minimum, maximum, middle = measured
        import_scale = float(part.get("import_scale", 0.0))
        node_scale = parent_scale * (import_scale if import_scale > 0.0
                                     else float(KIT["scale"]))
        for corner in ((x, y, z) for x in (minimum[0], maximum[0])
                       for y in (minimum[1], maximum[1])
                       for z in (minimum[2], maximum[2])):
            for axis in range(3):
                value = offset[axis] + corner[axis] * node_scale
                composed_min[axis] = min(composed_min[axis], value)
                composed_max[axis] = max(composed_max[axis], value)
        if not stack:
            for axis in range(3):
                root_middle[axis] = offset[axis] + middle[axis] * node_scale
        for attachment in part.get("attachments", []):
            child = [offset[axis] + float(attachment["position"][axis]) * node_scale
                     for axis in range(3)]
            include(attachment["prefab"], child, node_scale, stack + (prefab,))

    include(spec, [0.0, 0.0, 0.0], 1.0, ())
    return composed_min, composed_max, root_middle, 1.0


entities = []


def add(**values):
    entity = collections.OrderedDict()
    entity["id"] = values.pop("id")
    for key in ("name", "parent", "prefab", "material"):
        if key in values:
            entity[key] = values.pop(key)
    if "mesh" in values:
        entity["mesh"] = values.pop("mesh")
    transform = collections.OrderedDict()
    if "pos" in values:
        transform["position"] = [round(float(v), 4) for v in values.pop("pos")]
    if "rot" in values:
        transform["rotation_degrees"] = [round(float(v), 4)
                                         for v in values.pop("rot")]
    if "scale" in values:
        transform["scale"] = [round(float(v), 5) for v in values.pop("scale")]
    if transform:
        entity["transform"] = transform
    entity.update(values)
    entities.append(entity)
    return entity


def torch(index, position, yaw):
    """A wall bracket: the prop, its flame, and the light that does the work.

    Each gets its own animation phase. Four torches on one phase gutter in
    lockstep, which reads as a switch rather than as fire.
    """
    add(id="torch_%02d" % index, name="Torch %d" % index,
        prefab="kit.prop_torch", pos=position, rot=[0.0, yaw, 0.0],
        scale=[1.35, 1.35, 1.35], cast_shadows=False,
        particles={"effect": "torch_fire", "offset": [0.0, 0.72, 0.0],
                   "scale": 0.8})
    add(id="torch_light_%02d" % index, name="Torch Light %d" % index,
        pos=[position[0], position[1] + 0.85, position[2]],
        light={"type": "point", "colour": TORCH_COLOUR, "range": TORCH_RANGE,
               "animation": {"mode": "flicker", "speed": 7.0, "amount": 0.32,
                             "phase": 1.9 * index + 0.4}})


# --- the shell ----------------------------------------------------------------

for col, x in enumerate(COLS):
    for row, z in enumerate(ROWS):
        add(id="floor_%d%d" % (col, row), prefab="kit.floor", pos=[x, 0.0, z])

# Storey one, then the cornice band on top of it. The band is what stops a 6 m
# wall from being one flat 6 m rectangle of the same texture.
for col, x in enumerate(COLS):
    # The centre bay is an open arch, not a wall, and that is the single most
    # load-bearing decision in the room: a subject photographed against brick is
    # a dark shape on a mid-grey field, and the same subject photographed
    # against an unlit opening separates instantly.
    #
    # It has to be the whole 4 m bay, though. The first version used a doorway,
    # and a 2 m doorway eleven metres back subtends *less* than a 1.3 m subject
    # five metres from the lens -- so the subject covered its own backdrop
    # exactly and the frame gained nothing. The opening must out-subtend the
    # thing standing in front of it or it is not a backdrop.
    #
    # It is a bare 4 m bay rather than a kit.arch, too. That piece is only the
    # *head* of an opening -- it springs at 0.96 m and expects wall either side
    # to be its jambs -- so dropped into an empty bay it hangs in mid-air with
    # daylight under it. The two columns moved in to flank this opening are what
    # gives it its jambs instead, and they are real 6 m pillars meeting the
    # real 6 m springing line.
    if x != 0.0:
        add(id="wall_back_%d" % col, name="Back Wall %d" % col, prefab="kit.wall",
            pos=[x, 0.0, WALL_Z_BACK])
    add(id="wall_back_band_%d" % col, prefab="kit.wall_border",
        pos=[x, STOREY, WALL_Z_BACK])
    add(id="wall_front_%d" % col, prefab="kit.wall",
        pos=[x, 0.0, WALL_Z_FRONT], rot=[0.0, 180.0, 0.0])
    add(id="wall_front_band_%d" % col, prefab="kit.wall_border",
        pos=[x, STOREY, WALL_Z_FRONT], rot=[0.0, 180.0, 0.0])

for row, z in enumerate(ROWS):
    for side, (x, yaw) in enumerate(((-WALL_X, 90.0), (WALL_X, -90.0))):
        # The two cells flanking the plinth wear the skull wall: the eye needs
        # something other than repeated brick where it spends the whole shot.
        piece = "kit.wall_skull" if z == 0.0 else "kit.wall"
        add(id="wall_side_%d%d" % (side, row), prefab=piece,
            pos=[x, 0.0, z], rot=[0.0, yaw, 0.0])
        add(id="wall_side_band_%d%d" % (side, row), prefab="kit.wall_border",
            pos=[x, STOREY, z], rot=[0.0, yaw, 0.0])

for col, x in enumerate(COLS):
    for row, z in enumerate(ROWS):
        add(id="vault_%d%d" % (col, row), prefab="kit.arch_roof",
            pos=[x, SPRING - VAULT_BASE, z], cast_shadows=False)

# The alcove through that doorway: one cell of floor, three walls and a cold
# light at the far end. It is never walked into and barely seen -- it exists so
# the doorway reads as somewhere the chamber continues rather than as a black
# decal painted on the wall.
ALCOVE_Z = -8.0
add(id="alcove_floor", prefab="kit.floor", pos=[0.0, 0.0, ALCOVE_Z])
add(id="alcove_back", prefab="kit.wall_skull", pos=[0.0, 0.0, ALCOVE_Z - 2.0])
add(id="alcove_back_band", prefab="kit.wall_border",
    pos=[0.0, STOREY, ALCOVE_Z - 2.0])
for side, (x, yaw) in enumerate(((-2.0, 90.0), (2.0, -90.0))):
    add(id="alcove_side_%d" % side, prefab="kit.wall",
        pos=[x, 0.0, ALCOVE_Z], rot=[0.0, yaw, 0.0])
    add(id="alcove_side_band_%d" % side, prefab="kit.wall_border",
        pos=[x, STOREY, ALCOVE_Z], rot=[0.0, yaw, 0.0])
add(id="alcove_roof", prefab="kit.arch_roof",
    pos=[0.0, SPRING - VAULT_BASE, ALCOVE_Z], cast_shadows=False)

# Four columns, exactly 6.02 m of Pillar.obj, standing where the vault springs.
# The front pair frames the shot from the edges; the back pair is pulled off the
# grid to stand either side of the opening, where it does three jobs at once --
# jambs for the bay, a frame for the subject, and something for the kicker to
# catch that is not the subject.
for index, (x, z) in enumerate(((-2.75, -5.0), (2.75, -5.0),
                                (-4.0, 4.0), (4.0, 4.0)), 1):
    add(id="column_%d" % index, name="Column %d" % index,
        prefab="kit.pillar", pos=[x, 0.0, z])

# --- the plinth ---------------------------------------------------------------
# One hexagon squashed twice: a wide shallow step with a narrower block on it.
# Two tiers rather than one because a single slab reads as a crate.

add(id="plinth_step", name="Plinth Step", prefab="kit.floor_hexagon",
    material="Game/Kit/Stone",
    pos=[0.0, 0.0, 0.0], scale=[0.34, 0.11, 0.30])
add(id="plinth_top", name="Plinth", prefab="kit.floor_hexagon",
    material="Game/Kit/Dungeon",
    pos=[0.0, PLINTH_STEP_TOP, 0.0], scale=[0.24, 0.26, 0.21])

# --- the subject --------------------------------------------------------------

geometry = subject_geometry(args.subject)
subject_scale = args.scale
ground = 0.0
centre_x = 0.0
centre_z = 0.0
if geometry is not None:
    minimum, maximum, middle, base_scale = geometry
    height = (maximum[1] - minimum[1]) * base_scale
    if args.fit > 0.0 and height > 1e-6:
        subject_scale = args.scale * (args.fit / height)
    else:
        subject_scale = args.scale * base_scale
    # Stand it on the plinth: the *box's* floor, so nothing pokes through the
    # stone even if the lowest thing on the model is a trailing cape.
    ground = -minimum[1] * subject_scale
    # Put its bulk over the pivot: the *median*, so a spear held out to one side
    # does not drag the axis off the body. The spin turns the pivot, and an axis
    # that misses the body swings the whole model round a point in mid-air.
    #
    # The entity's local transform is translate * rotate, so the offset has to be
    # expressed after the resting yaw or it drifts as soon as the model is
    # turned.
    offset_x = -middle[0] * subject_scale
    offset_z = -middle[2] * subject_scale
    radians = math.radians(args.yaw)
    centre_x = math.cos(radians) * offset_x + math.sin(radians) * offset_z
    centre_z = -math.sin(radians) * offset_x + math.cos(radians) * offset_z
    fitted_height = (maximum[1] - minimum[1]) * subject_scale
else:
    print("note: %s is not an .obj, so it cannot be measured -- placing it at "
          "--scale with no auto-fit" % args.subject)
    fitted_height = args.fit if args.fit > 0.0 else 2.0
subject_top = PLINTH_TOP + args.lift + args.clearance + fitted_height

pivot_y = PLINTH_TOP + args.lift + args.clearance
add(id="subject_pivot", name="Turntable",
    pos=[0.0, pivot_y, 0.0],
    spin={"axis": [0.0, 1.0, 0.0], "degrees_per_second": args.spin})

subject_name = args.subject_name or (
    args.subject.removeprefix("kit.").rsplit("/", 1)[-1]
    .removesuffix(".obj").replace("_", " ").title())
subject = {
    "id": "subject",
    "name": subject_name,
    "parent": "subject_pivot",
    "pos": [centre_x, ground, centre_z],
    "rot": [0.0, args.yaw, 0.0],
    # A faint warm fresnel. The kicker behind does the real separation; this
    # keeps an edge alive on the side the kicker cannot reach.
    "shader": {"rim_colour": [1.0, 0.62, 0.34], "rim_strength": 0.42,
               "rim_power": 3.6},
}
if args.subject.startswith("kit."):
    subject["prefab"] = args.subject
else:
    subject["mesh"] = {"path": args.subject}
# A kit prefab brings its own material. A bare mesh path does not, and a mesh
# with no material renders as the missing-texture magenta checkerboard -- which
# is the right answer in a level and the wrong one here, because the point of
# putting a thing on this plinth is to look at its shape. Neutral white lit,
# unless the caller says otherwise.
if args.subject_material:
    subject["material"] = args.subject_material
elif not args.subject.startswith("kit."):
    subject["material"] = "Game/Demo/TurntableNeutral"
    print("note: no --subject-material, so the mesh wears neutral "
          "Game/Demo/TurntableNeutral")
if abs(subject_scale - 1.0) > 1e-6:
    subject["scale"] = [subject_scale] * 3
add(**subject)

# --- dressing -----------------------------------------------------------------
# Asymmetric on purpose. A mirrored room reads as a menu; a room with the rubble
# on one side and the candles on the other reads as a place.

# Kept in the front half of the room. A torch at the back is a torch on the
# back wall, and the back wall's job here is to be dark.
torch(1, [-WALL_X + 0.62, 2.35, 1.4], 90.0)
torch(2, [WALL_X - 0.62, 2.35, 1.4], -90.0)
torch(3, [-WALL_X + 0.62, 2.35, 6.6], 90.0)
torch(4, [WALL_X - 0.62, 2.35, 6.6], -90.0)

# There is no chandelier. One hung here for a while and it was the wrong call:
# a 2.9 m unlit disc directly above the subject ate the top quarter of the frame
# and put a hard black edge across the vault. What it was really there for was
# the light, so the light stayed and the geometry went.
add(id="light_hanging", name="Hanging Flame", pos=[0.0, 4.35, -2.2],
    light={"type": "point", "colour": [1.05, 0.60, 0.30], "range": 6.5,
           "animation": {"mode": "flicker", "speed": 4.5, "amount": 0.18,
                         "phase": 2.6}})

# There are no candles either, for the same reason there is no chandelier. Two
# rounds went into moving a candle cluster somewhere it would not read as a pale
# featureless post -- floor, then up on the plinth step -- and it read as a pale
# featureless post in both. The warm flicker it was there for is a light, and a
# light is all that is left of it.
add(id="votive_light", name="Votive Flicker", pos=[-0.88, PLINTH_STEP_TOP + 0.4, 0.80],
    light={"type": "point", "colour": [1.20, 0.62, 0.24], "range": 2.4,
           "animation": {"mode": "flicker", "speed": 9.0, "amount": 0.40,
                         "phase": 5.1}})

add(id="rubble_pillar", name="Fallen Column", prefab="kit.pillar_fallen",
    pos=[4.7, 0.0, -3.2], rot=[0.0, 62.0, 0.0])
add(id="rubble_debris", prefab="kit.debris", pos=[-4.7, 0.0, -3.0],
    rot=[0.0, 145.0, 0.0])
add(id="rubble_debris_2", prefab="kit.debris", pos=[4.4, 0.0, -4.4],
    rot=[0.0, -70.0, 0.0], scale=[0.7, 0.7, 0.7])
add(id="prop_barrel", prefab="kit.prop_barrel", pos=[-4.6, 0.0, -3.6],
    rot=[0.0, 18.0, 0.0])
add(id="prop_barrel_bands", prefab="kit.prop_barrel_bands", pos=[-4.6, 0.0, -3.6],
    rot=[0.0, 18.0, 0.0])
add(id="prop_crate", prefab="kit.prop_crate", pos=[-4.95, 0.0, -4.7],
    rot=[0.0, -34.0, 0.0], scale=[1.6, 1.6, 1.6])

# The shaft through the window behind the subject: the one thing in the frame
# that says the chamber is somewhere rather than a box.
add(id="light_shaft", name="Alcove Shaft", prefab="kit.light_shaft",
    pos=[0.0, 1.05, -8.4], scale=[0.62, 1.25, 0.62], cast_shadows=False)

add(id="dust", name="Dust Motes", pos=[0.0, 1.4, 0.5],
    particles={"effect": "hall_dust", "scale": 1.0})

# --- light --------------------------------------------------------------------

add(id="light_key", name="Warm Key", pos=KEY["pos"],
    light={"type": "point", "colour": KEY["colour"], "range": KEY["range"],
           "cast_shadows": True})
add(id="light_fill", name="Cold Fill", pos=FILL["pos"],
    light={"type": "point", "colour": FILL["colour"], "range": FILL["range"]})
add(id="light_kick", name="Kicker", pos=KICK["pos"],
    light={"type": "point", "colour": KICK["colour"], "range": KICK["range"]})
add(id="light_vault", name="Vault Bounce", pos=VAULT_GLOW["pos"],
    light={"type": "point", "colour": VAULT_GLOW["colour"],
           "range": VAULT_GLOW["range"]})
add(id="light_alcove", name="Alcove Glow", pos=ALCOVE_GLOW["pos"],
    light={"type": "point", "colour": ALCOVE_GLOW["colour"],
           "range": ALCOVE_GLOW["range"],
           "animation": {"mode": "pulse", "speed": 0.22, "amount": 0.14}})

# --- camera -------------------------------------------------------------------
# Aimed rather than eyeballed: the subject's height comes out of the fit above,
# so the framing has to be derived from it or every swap needs a nudge.

subject_and_plinth = PLINTH_TOP + fitted_height
frame_height = subject_and_plinth / FRAME_FILL
distance = min(frame_height / (2.0 * math.tan(math.radians(CAMERA_FOV * 0.5))),
               CAMERA_MAX_DISTANCE)
eye = PLINTH_TOP + fitted_height * EYE_FRACTION
# Looking at (0, y, 0) from (0, eye, distance) puts y dead centre of the frame
# at the subject's own depth, so the bias above lands where it says it does.
aim = [0.0, subject_and_plinth * FRAME_BIAS, 0.0]
camera_pos = [0.0, eye, distance]

delta = [aim[axis] - camera_pos[axis] for axis in range(3)]
length = math.sqrt(sum(value * value for value in delta)) or 1.0
delta = [value / length for value in delta]
pitch = math.degrees(math.asin(max(-1.0, min(1.0, delta[1]))))
yaw = math.degrees(math.atan2(-delta[0], -delta[2]))

add(id="camera_main", name="Hero Camera", pos=camera_pos,
    rot=[pitch, yaw, 0.0],
    camera={"fov_degrees": CAMERA_FOV, "near_clip": 0.08, "far_clip": 60.0,
            "priority": 20})
# Parked alternates: tick `active` on one (and off the hero) to reframe without
# moving anything. A higher priority is how one wins.
add(id="camera_orbit", name="Orbit Camera (parked)",
    pos=[0.0, eye, distance * 0.85], rot=[pitch, 0.0, 0.0],
    camera={"fov_degrees": CAMERA_FOV, "near_clip": 0.08, "far_clip": 60.0,
            "priority": 10, "active": False},
    orbit={"centre": [0.0, aim[1], 0.0], "radius": distance * 0.85,
           "degrees_per_second": 9.0, "height": eye - aim[1], "facing": "centre"})
add(id="camera_detail", name="Detail Camera (parked)",
    pos=[0.9, eye * 0.9, distance * 0.5], rot=[-2.0, 16.0, 0.0],
    camera={"fov_degrees": 32.0, "near_clip": 0.05, "far_clip": 40.0,
            "priority": 10, "active": False})

add(id="player_start", name="Player Start", pos=[0.0, 0.0, 7.5],
    player_spawn=True)


output = os.path.abspath(args.output)
schema = os.path.relpath(
    os.path.join(ROOT, "assets/schemas/scene.schema.json"),
    os.path.dirname(output))
document = collections.OrderedDict([
    ("$schema", schema),
    ("format", "raven-scene"),
    ("version", 2),
    ("id", "scene.turntable"),
    ("palette", "turntable"),
    ("entities", entities),
])
os.makedirs(os.path.dirname(output), exist_ok=True)
with open(output, "w", encoding="utf-8") as stream:
    json.dump(document, stream, indent=2)
    stream.write("\n")
print("wrote:", output)
print("entities:", len(entities))
print("subject: %s  scale %.4f  fitted height %.2f m"
      % (args.subject, subject_scale, subject_top - PLINTH_TOP))
