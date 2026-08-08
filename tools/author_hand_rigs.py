#!/usr/bin/env python3
"""The hands pack -> a set of two-armed, animated first-person rigs.

WHAT THE PACK GIVES US, AND WHAT IT DOES NOT

`assets/source/hands/` holds four rigged arms -- human, glove, alien, werewolf
-- with skin tone, suit and variation textures. Every one of them is a single
RIGHT arm in a bind pose, with no animation at all. The author says so in the
pack's comment thread, and recommends the fix: copy the arm and set its X scale
to -1.

So three things are missing between that and a first-person viewmodel, and this
script is all three:

  1. a LEFT arm, mirrored, on the same skeleton as the right;
  2. engine orientation -- the pack's arm points up, a viewmodel's points
     forward, and the two differ by a 90 degree turn nobody should be doing at
     a call site;
  3. animation. There is no animator on this project, so the clips are authored
     procedurally from one table of poses, exactly as tools/author_humanoid_rig.py
     does for the actor rig. Re-running this script after changing a number IS
     the iteration loop.

It also solves the material problem. The pack gives each hand two materials so
a skin tone and a suit can be mixed, but `Renderer::attachSkinnedMesh` takes one
material for the whole mesh. Rather than teach the skinned path about submesh
materials -- a renderer change to serve one asset -- the two 256x256 textures
are atlased into one 512x256 strip and the UVs remapped per material slot. One
texture fetch, which is what the rest of this engine's art does anyway, and any
pairing is still expressible: it is a row in the variant table below.

OUTPUT

  assets/meshes/viewmodels/<rig>.glb              rigged, animated
  assets/textures/viewmodels/<rig>_albedo.png     the atlas
  assets/animations/viewmodels/<rig>/*.ozz        skeleton + clips
  assets/materials/viewmodel_hands.mat            one material per rig
  assets/config/viewmodel_hands.toml              the rig set the game reads

Usage:
  tools/author_hand_rigs.py [--rig ID ...] [--skip-cook] [--gltf2ozz PATH]

Requires the `blender` CLI. Cooking additionally requires gltf2ozz, which the
engine build produces; --skip-cook stops after the GLB if it is not built yet.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(REPO, "assets")
SOURCE = os.path.join(ASSETS, "source", "hands")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# --- the rig set -------------------------------------------------------------
#
# Which arm, wearing what. `body` is the texture the arm/hand material samples
# and `suit` the sleeve material; a rig with no suit has one material and one
# atlas cell.
#
# The pack ships 6 skin tones and 7 suits, which is 42 combinations. Publishing
# all of them would be 42 rigs nobody asked for, so this is a spread: one per
# skin tone paired with a different suit, plus every non-human arm. Adding the
# 43rd is a row here, not code -- which is the property that matters.

RIGS = [
    # id                  source     body texture                 suit texture
    ("hands_human_01", "hand", "SkinTones/1/base.png", "Suits/1/base.png"),
    ("hands_human_02", "hand", "SkinTones/2/base.png", "Suits/2/base.png"),
    ("hands_human_03", "hand", "SkinTones/3/base.png", "Suits/3/base.png"),
    ("hands_human_04", "hand", "SkinTones/4/base.png", "Suits/4/base.png"),
    ("hands_human_05", "hand", "SkinTones/5/base.png", "Suits/5/base.png"),
    ("hands_human_06", "hand", "SkinTones/6/base.png", "Suits/6/base.png"),
    ("hands_glove_01", "glove", "Gloves/1/base_2.png", "Suits/7/base.png"),
    ("hands_glove_02", "glove", "Gloves/2/base_2.png", "Suits/3/base.png"),
    ("hands_glove_03", "glove", "Gloves/3/base_2.png", "Suits/5/base.png"),
    ("hands_alien_01", "alien", "Alien/base.png", ""),
    ("hands_alien_02", "alien", "Alien/2/base.png", ""),
    ("hands_alien_03", "alien", "Alien/3/base.png", ""),
    ("hands_werewolf_01", "werewolf", "Werewolf/base.png", ""),
    ("hands_werewolf_02", "werewolf", "Werewolf/2/base.png", ""),
    ("hands_werewolf_03", "werewolf", "Werewolf/3/base.png", ""),
]

# Which material slot of each source mesh samples which atlas cell. Measured,
# not assumed -- the slot indices and their UV ranges are printed by the
# inspection in this file's history and differ per model: the human arm's
# sleeve is slot 1, the glove's is also slot 1 but its body is slot 2, and the
# alien and werewolf leave two slots empty and use only slot 2.
#
# `body` and `suit` are cell indices into the atlas; a slot missing from the
# map has no faces.
SLOT_CELLS = {
    "hand": {0: "body", 1: "suit"},
    "glove": {0: "body", 1: "suit", 2: "body"},
    "alien": {2: "body"},
    "werewolf": {2: "body"},
}

# The pack's bone names -> the engine's. The existing arms rig already spells
# these `hand.R`, `forearm.L`, `f_index.03.R`, so following it keeps one
# vocabulary for sockets across every rig the viewmodel can wear.
BONE_NAMES = {
    "Shoulder": "shoulder",
    "Arm": "forearm",
    "Hand": "hand",
    "Finger1": "thumb.01",
    "Finger1.001": "thumb.02",
    "Finger2": "f_index.01",
    "Finger2.001": "f_index.02",
    "Finger3": "f_middle.01",
    "Finger3.001": "f_middle.02",
    "Finger4": "f_ring.01",
    "Finger4.001": "f_ring.02",
    "Finger5": "f_pinky.01",
    "Finger5.001": "f_pinky.02",
}

# Metres per source unit. The pack's arm measures 6.93 units from shoulder to
# fingertip; a first-person arm is about 0.7 m, so 0.1.
UNIT_SCALE = 0.1

# How far each arm sits from the centre line, in metres.
#
# The pack models its one arm ON x=0, because it was authored to be placed by
# whatever imports it. Mirroring that without moving it puts both arms in the
# same space -- 451 vertices interpenetrating 451 vertices, which reads on
# screen as one arm with a shading fault. 0.16 m puts each hand in its own
# lower corner of the frame with the centre left clear for the weapon, which is
# what docs/references/fps_viewmodel_reference.png does.
ARM_SEPARATION = 0.16


def variant_source(rig: str) -> str:
    return next(source for ident, source, _, _ in RIGS if ident == rig)


# --- textures ----------------------------------------------------------------

def build_atlas(rig: str, body: str, suit: str) -> tuple:
    """Write the rig's atlas; return (relative path, cell count)."""
    import pngkit

    cells = [pngkit.read(os.path.join(SOURCE, body))]
    if suit:
        cells.append(pngkit.read(os.path.join(SOURCE, suit)))
    image = pngkit.atlas(cells)
    rel = "textures/viewmodels/%s_albedo.png" % rig
    out = os.path.join(ASSETS, rel)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    pngkit.write(out, image)
    return rel, len(cells)


# --- the Blender phase -------------------------------------------------------

BLENDER_SCRIPT = r'''
import bpy, json, math, os, sys
from mathutils import Matrix, Quaternion, Vector

job = json.loads(os.environ["RAVEN_HANDS_JOB"])

SCALE = job["unit_scale"]
SEPARATION = job["separation"]
IDENTITY = Quaternion((1.0, 0.0, 0.0, 0.0))


def to_engine(v):
    """Pack space -> the space the rig is authored in.

    The pack's arm runs along Blender +Z (straight up). glTF export turns
    Blender (x, y, z) into engine (x, z, -y), so an arm left alone comes out
    pointing at the engine's +Y -- the ceiling. Turning it -90 degrees about X
    here puts it along Blender +Y, which exports as engine -Z: forward, which
    is what a viewmodel means and what every node in this engine calls its
    front.
    """
    # The lateral push is added on the right-hand side only; the mirror that
    # builds the left arm negates x, so it inherits the opposite offset for
    # free and the two can never drift apart.
    return Vector((v.x * SCALE + SEPARATION, v.z * SCALE, -v.y * SCALE))


def mirror_quat(q):
    """The same rotation seen in a mirror down x=0.

    A rotation axis is a pseudovector: reflecting negates the components
    parallel to the mirror plane and keeps the perpendicular one. Getting it
    backwards is the classic "the left arm swings the wrong way" bug.
    """
    return Quaternion((q.w, q.x, -q.y, -q.z))


def bend(degrees):
    """Curl about the bone's local X: a finger closing, an elbow folding."""
    return Quaternion(Vector((1.0, 0.0, 0.0)), math.radians(degrees))


def splay(degrees):
    """Spread about the bone's local Z: fingers opening, a wrist deviating."""
    return Quaternion(Vector((0.0, 0.0, 1.0)), math.radians(degrees))


def twist(degrees):
    """Roll about the bone's own length."""
    return Quaternion(Vector((0.0, 1.0, 0.0)), math.radians(degrees))


# --- poses ------------------------------------------------------------------
#
# A pose is a rotation per bone, parent-relative, in bone-local axes. Both
# sides of the rig are built with the same roll (local Z along Blender +Z), so
# one rotation means the same thing on either arm and a left-hand pose is the
# right-hand pose through mirror_quat().

class Pose:
    __slots__ = ("rotations", "offset")

    def __init__(self, rotations=None, offset=None):
        self.rotations = dict(rotations or {})
        self.offset = Vector(offset) if offset else Vector((0.0, 0.0, 0.0))

    def __or__(self, other):
        merged = dict(self.rotations)
        for bone, rotation in other.rotations.items():
            merged[bone] = rotation @ merged.get(bone, IDENTITY)
        return Pose(merged, self.offset + other.offset)

    def side(self, suffix):
        """Bind this pose's bare bone names to one arm."""
        if suffix == ".R":
            return Pose({b + ".R": q for b, q in self.rotations.items()},
                        self.offset)
        return Pose({b + ".L": mirror_quat(q) for b, q in self.rotations.items()},
                    Vector((-self.offset.x, self.offset.y, self.offset.z)))


def lerp(a, b, t):
    bones = set(a.rotations) | set(b.rotations)
    out = {}
    for bone in bones:
        out[bone] = a.rotations.get(bone, IDENTITY).slerp(
            b.rotations.get(bone, IDENTITY), t)
    return Pose(out, a.offset.lerp(b.offset, t))


def smoothstep(t):
    t = min(1.0, max(0.0, t))
    return t * t * (3.0 - 2.0 * t)


def sample(keys, phase, loop):
    phase = phase % 1.0 if loop else min(1.0, max(0.0, phase))
    for i in range(len(keys) - 1):
        t0, p0 = keys[i]
        t1, p1 = keys[i + 1]
        if t0 <= phase <= t1:
            return lerp(p0, p1, smoothstep((phase - t0) / max(1e-6, t1 - t0)))
    return keys[0][1] if phase < keys[0][0] else keys[-1][1]


FINGERS = ["f_index", "f_middle", "f_ring", "f_pinky"]


def curl(amount, thumb=None):
    """Every finger closed by `amount` degrees at both joints.

    One number for a whole hand, because a fist authored joint by joint is
    thirteen numbers that have to stay in proportion, and they never do.
    """
    rotations = {}
    for finger in FINGERS:
        rotations[finger + ".01"] = bend(amount)
        rotations[finger + ".02"] = bend(amount * 0.9)
    thumb_amount = amount * 0.55 if thumb is None else thumb
    rotations["thumb.01"] = bend(thumb_amount) @ splay(-thumb_amount * 0.4)
    rotations["thumb.02"] = bend(thumb_amount)
    return Pose(rotations)


def arm(shoulder=0.0, elbow=0.0, wrist=0.0, out=0.0, roll=0.0):
    """The three big joints, in the terms an animator would use."""
    return Pose({
        "shoulder": bend(shoulder) @ splay(out),
        "forearm": bend(elbow),
        "hand": bend(wrist) @ twist(roll),
    })


# The rig's home: arms drawn back and down, out of the centre of the screen,
# hands loosely open. Everything else is a departure from this.
REST = arm(shoulder=18.0, elbow=-26.0, wrist=6.0, out=14.0) | curl(16.0)
LOW = arm(shoulder=52.0, elbow=-18.0, wrist=10.0, out=10.0) | curl(24.0)
READY = arm(shoulder=6.0, elbow=-34.0, wrist=-4.0, out=9.0) | curl(30.0)
FIST = arm(shoulder=4.0, elbow=-38.0, wrist=-8.0, out=8.0) | curl(88.0, thumb=62.0)
GUARD = arm(shoulder=-14.0, elbow=-64.0, wrist=-10.0, out=16.0) | curl(84.0, thumb=58.0)
REACH = arm(shoulder=-8.0, elbow=-10.0, wrist=4.0, out=4.0) | curl(8.0)
GRASP = arm(shoulder=-6.0, elbow=-14.0, wrist=2.0, out=4.0) | curl(70.0, thumb=54.0)
PUNCH = arm(shoulder=-20.0, elbow=-6.0, wrist=0.0, out=2.0) | curl(90.0, thumb=64.0)
BREATHE = arm(shoulder=2.6, elbow=-2.0, wrist=1.4)

# Clips. `(name, seconds, loop, [(phase, right pose, left pose)])`.
#
# Two poses per key rather than one mirrored pose, because a first-person rig is
# NOT symmetric: the right hand holds the weapon and the left supports it, and
# a mirrored idle reads as a man holding two invisible objects.
CLIPS = [
    ("relax", 4.0, True, [
        (0.00, REST, REST),
        (0.50, REST | BREATHE, REST | BREATHE),
        (1.00, REST, REST),
    ]),
    ("idle", 3.2, True, [
        (0.00, READY, REST | LOW),
        (0.50, READY | BREATHE, REST | LOW | BREATHE),
        (1.00, READY, REST | LOW),
    ]),
    ("guard_idle", 3.0, True, [
        (0.00, GUARD, GUARD),
        (0.50, GUARD | BREATHE, GUARD | BREATHE),
        (1.00, GUARD, GUARD),
    ]),
    ("draw", 0.42, False, [
        (0.00, LOW, LOW),
        (0.68, READY | arm(shoulder=-8.0), REST | LOW),
        (1.00, READY, REST | LOW),
    ]),
    ("holster", 0.34, False, [
        (0.00, READY, REST | LOW),
        (1.00, LOW, LOW),
    ]),
    ("fire", 0.24, False, [
        (0.00, READY, REST | LOW),
        (0.22, READY | arm(shoulder=16.0, elbow=-12.0, wrist=-16.0),
         REST | LOW | arm(shoulder=5.0)),
        (1.00, READY, REST | LOW),
    ]),
    ("jab.R", 0.36, False, [
        (0.00, FIST, FIST),
        (0.34, PUNCH, FIST),
        (1.00, FIST, FIST),
    ]),
    ("jab.L", 0.36, False, [
        (0.00, FIST, FIST),
        (0.34, FIST, PUNCH),
        (1.00, FIST, FIST),
    ]),
    ("grab.R", 0.52, False, [
        (0.00, REST, REST),
        (0.45, REACH, REST),
        (0.70, GRASP, REST),
        (1.00, REST, REST),
    ]),
]

FPS = 30


# --- build ------------------------------------------------------------------

def read_source(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=path, use_image_search=False)
    armature = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    mesh = next(o for o in bpy.data.objects if o.type == "MESH")
    return armature, mesh


def bone_table(armature, names):
    """The pack's bones, in engine space, parent before child."""
    table = []
    for bone in armature.data.bones:
        if bone.name not in names:
            continue
        table.append({
            "name": names[bone.name],
            "head": to_engine(bone.head_local),
            "tail": to_engine(bone.tail_local),
            "parent": names.get(bone.parent.name) if bone.parent else None,
        })
    order = {row["name"]: i for i, row in enumerate(table)}
    table.sort(key=lambda row: (row["parent"] is not None,
                                order.get(row["parent"], -1)))
    return table


def build_armature(table):
    """One armature carrying both arms under a shared root."""
    data = bpy.data.armatures.new("HandsRig")
    rig = bpy.data.objects.new("HandsRig", data)
    bpy.context.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode="EDIT")

    root = data.edit_bones.new("root")
    root.head = Vector((0.0, 0.0, 0.0))
    root.tail = Vector((0.0, 0.06, 0.0))

    for suffix, sign in ((".R", 1.0), (".L", -1.0)):
        for row in table:
            bone = data.edit_bones.new(row["name"] + suffix)
            bone.head = Vector((row["head"].x * sign, row["head"].y,
                                row["head"].z))
            bone.tail = Vector((row["tail"].x * sign, row["tail"].y,
                                row["tail"].z))
            parent = row["parent"]
            bone.parent = data.edit_bones[parent + suffix] if parent else root
            # Same roll on both arms, so one authored rotation means the same
            # thing on either -- which is what makes mirror_quat() the whole of
            # the left-hand pose.
            bone.align_roll(Vector((0.0, 0.0, 1.0)))

    bpy.ops.object.mode_set(mode="OBJECT")
    return rig


def prepare_mesh(mesh, names, remap):
    """Bake the engine transform into the vertices, rename the weights, atlas
    the UVs, and mirror a left arm out of the right."""
    bpy.context.view_layer.objects.active = mesh
    bpy.ops.object.select_all(action="DESELECT")
    mesh.select_set(True)

    matrix = Matrix(((SCALE, 0, 0, SEPARATION), (0, 0, SCALE, 0),
                     (0, -SCALE, 0, 0), (0, 0, 0, 1)))
    mesh.data.transform(matrix @ mesh.matrix_world)
    mesh.matrix_world = Matrix.Identity(4)

    for group in mesh.vertex_groups:
        if group.name in names:
            group.name = names[group.name] + ".R"

    # UVs into the atlas strip: `u' = (u + cell) / cells`, per material slot.
    uv = mesh.data.uv_layers.active
    if uv and remap["cells"] > 1:
        for poly in mesh.data.polygons:
            cell = remap["slots"].get(str(poly.material_index), 0)
            for loop_index in poly.loop_indices:
                u, v = uv.data[loop_index].uv
                uv.data[loop_index].uv = ((u + cell) / remap["cells"], v)

    # The mirrored arm. Negating x reverses every triangle's winding, so the
    # faces are flipped back or the left arm renders inside out -- which looks
    # like a shading bug and is not one.
    left = mesh.copy()
    left.data = mesh.data.copy()
    bpy.context.collection.objects.link(left)
    left.data.transform(Matrix.Diagonal(Vector((-1.0, 1.0, 1.0, 1.0))))
    left.data.flip_normals()
    for group in left.vertex_groups:
        if group.name.endswith(".R"):
            group.name = group.name[:-2] + ".L"

    bpy.ops.object.select_all(action="DESELECT")
    mesh.select_set(True)
    left.select_set(True)
    bpy.context.view_layer.objects.active = mesh
    bpy.ops.object.join()
    mesh.name = "Hands"
    return mesh


def skin(mesh, rig):
    bpy.ops.object.select_all(action="DESELECT")
    mesh.select_set(True)
    rig.select_set(True)
    bpy.context.view_layer.objects.active = rig
    # ARMATURE_NAME: bind by the vertex groups already on the mesh rather than
    # computing new weights. The pack's weights are the art; recomputing them
    # from bone envelopes would repaint every knuckle.
    bpy.ops.object.parent_set(type="ARMATURE_NAME")


def apply_pose(rig, pose):
    for bone in rig.pose.bones:
        bone.rotation_mode = "QUATERNION"
        bone.rotation_quaternion = pose.rotations.get(bone.name, IDENTITY)


def bake_clips(rig):
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode="POSE")
    for name, seconds, loop, keys in CLIPS:
        action = bpy.data.actions.new(name)
        rig.animation_data_create()
        rig.animation_data.action = action
        frames = max(2, int(round(seconds * FPS)))
        for frame in range(frames + 1):
            phase = frame / float(frames)
            right = sample([(k[0], k[1]) for k in keys], phase, loop)
            left = sample([(k[0], k[2]) for k in keys], phase, loop)
            apply_pose(rig, right.side(".R") | left.side(".L"))
            for bone in rig.pose.bones:
                bone.keyframe_insert("rotation_quaternion", frame=frame + 1)
        action.use_fake_user = True
    bpy.ops.object.mode_set(mode="OBJECT")
    rig.animation_data.action = None


report = {"rigs": [], "errors": []}
for entry in job["rigs"]:
    try:
        names = job["bone_names"]
        armature, mesh = read_source(entry["fbx"])
        table = bone_table(armature, names)
        prepare_mesh(mesh, names, entry["remap"])
        bpy.data.objects.remove(armature, do_unlink=True)
        rig = build_armature(table)
        skin(mesh, rig)
        bake_clips(rig)

        bpy.ops.object.select_all(action="DESELECT")
        mesh.select_set(True)
        rig.select_set(True)
        bpy.context.view_layer.objects.active = rig
        bpy.ops.export_scene.gltf(
            filepath=entry["glb"],
            export_format="GLB",
            use_selection=True,
            export_yup=True,
            export_apply=False,
            export_animations=True,
            export_animation_mode="ACTIONS",
            export_nla_strips=False,
            export_bake_animation=True,
            export_skins=True,
            export_materials="NONE",
            export_extras=False,
        )
        report["rigs"].append({
            "id": entry["id"],
            "joints": [row["name"] + s for s in (".R", ".L") for row in table],
            "clips": [c[0] for c in CLIPS],
        })
    except Exception as exc:
        import traceback
        report["errors"].append("%s: %s\n%s"
                                % (entry["id"], exc, traceback.format_exc()))

with open(job["report"], "w") as f:
    json.dump(report, f, indent=1)
print("RAVEN_HANDS_DONE %d rigs, %d errors"
      % (len(report["rigs"]), len(report["errors"])))
'''


def run_blender(rigs: list, report_path: str) -> dict:
    job = {
        "rigs": rigs,
        "report": report_path,
        "unit_scale": UNIT_SCALE,
        "separation": ARM_SEPARATION,
        "bone_names": BONE_NAMES,
    }
    env = dict(os.environ, RAVEN_HANDS_JOB=json.dumps(job))
    result = subprocess.run(
        ["blender", "-b", "--factory-startup", "--python-expr", BLENDER_SCRIPT],
        env=env, capture_output=True, text=True)
    if not os.path.isfile(report_path):
        sys.stderr.write(result.stdout[-6000:] + "\n" + result.stderr[-4000:])
        raise SystemExit("blender produced no report")
    with open(report_path) as stream:
        return json.load(stream)


# --- cooking -----------------------------------------------------------------

OZZ_CONFIG = {
    "skeleton": {
        "filename": "%s.skeleton.ozz",
        "import": {
            "enable": True,
            "raw": False,
            "types": {"skeleton": True, "marker": False, "camera": False,
                      "geometry": False, "light": False, "null": False,
                      "any": False},
        },
    },
    "animations": [{
        "clip": "*",
        "filename": "clip_*.ozz",
        "raw": False,
        "additive": False,
        "sampling_rate": 30,
        "iframe_interval": 2,
        "optimize": True,
        "optimization_settings": {"tolerance": 0.0005, "distance": 0.1,
                                  "override": []},
        "tracks": {"properties": [], "motion": {"enable": False}},
    }],
}


def find_gltf2ozz(explicit: str) -> str:
    if explicit:
        return explicit
    candidate = os.path.join(
        REPO, "build", "_deps", "ozz_animation_source-build", "src",
        "animation", "offline", "gltf", "gltf2ozz")
    return candidate if os.path.isfile(candidate) else ""


def cook(rig: str, glb: str, tool: str) -> bool:
    """GLB -> the runtime skeleton and clips, via gltf2ozz.

    Staged and renamed rather than written in place, the same way
    cmake/Game.cmake cooks the other two rigs: a failed conversion then leaves
    the last good clips alone instead of an empty directory, which is the
    difference between "this rig did not update" and "every hand is a capsule".
    """
    out = os.path.join(ASSETS, "animations", "viewmodels", rig)
    stage = out + ".stage"
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(stage, exist_ok=True)

    config = json.loads(json.dumps(OZZ_CONFIG))
    config["skeleton"]["filename"] = "%s.skeleton.ozz" % rig
    config_path = os.path.join(stage, "config.json")
    with open(config_path, "w") as f:
        json.dump(config, f)

    result = subprocess.run(
        [tool, "--file=" + glb, "--config_file=" + config_path,
         "--endian=little"],
        cwd=stage, capture_output=True, text=True)
    os.remove(config_path)
    if result.returncode != 0 or not os.listdir(stage):
        sys.stderr.write(result.stdout[-2000:] + result.stderr[-2000:])
        shutil.rmtree(stage, ignore_errors=True)
        return False

    shutil.rmtree(out, ignore_errors=True)
    os.rename(stage, out)
    return True


# --- generated content -------------------------------------------------------

def write_materials(rigs: list) -> None:
    out = os.path.join(ASSETS, "materials", "viewmodel_hands.mat")
    with open(out, "w") as f:
        f.write("# First-person hand rigs -- written by "
                "tools/author_hand_rigs.py.\n"
                "# One material per rig, each naming that rig's atlas.\n"
                "# Re-running the authoring tool regenerates this file.\n\n")
        for rig in rigs:
            f.write('[material."%s"]\n' % rig["material"])
            f.write('shader = "lit"\n')
            f.write('texture = "%s"\n' % os.path.basename(rig["texture"]))
            f.write('filter = "nearest"\n')
            f.write('address = "clamp"\n')
            # The viewmodel is lit by the weapon's own presentation, not by the
            # level: an arm that dims when the player walks into shadow reads
            # as a rendering fault rather than as atmosphere.
            f.write("highlight = false\n\n")


def write_config(rigs: list, default: str) -> None:
    out = os.path.join(ASSETS, "config", "viewmodel_hands.toml")
    with open(out, "w") as f:
        f.write('''# The player's first-person hands: every rig the viewmodel can wear.
#
# Written by tools/author_hand_rigs.py from assets/source/hands. Re-running the
# authoring tool regenerates this file, so a change belongs in the RIGS table
# there rather than here.
#
# This file answers two questions the game used to answer in C++:
#
#   * which rig the hands are -- `default_rig` below, or a rig id from the
#     Viewmodel panel, and the whole loadout is holding different hands with no
#     code change; and
#   * what a weapon is allowed to hang off, which is each rig's `[[socket]]`
#     list. A socket is a named point on the skeleton, and it is the vocabulary
#     weapons.toml picks from (`socket = "right_hand"`) and the editor offers
#     in a combo box. An author never types a joint name.
#
# Sockets are declared per rig because the rigs are not the same shape: the
# alien arm has three fingers, so `right_ring_tip` exists on the human hand and
# does not exist on that one. A weapon naming a socket its rig does not have is
# reported at load rather than silently seated at the wrist.
#
# See docs/fps-viewmodel.md; the placement of the rig as a whole (how low, how
# close to the eye) is [player_viewmodel] in game.toml, not here.

schema = 2
default_rig = "%s"

''' % default)
        for rig in rigs:
            f.write("[[rig]]\n")
            f.write('id = "%s"\n' % rig["id"])
            f.write('name = "%s"\n' % rig["label"])
            f.write('model = "meshes/viewmodels/%s.glb"\n' % rig["id"])
            f.write('skeleton = "animations/viewmodels/%s/%s.skeleton.ozz"\n'
                    % (rig["id"], rig["id"]))
            f.write('clip_dir = "animations/viewmodels/%s"\n' % rig["id"])
            f.write('material = "%s"\n' % rig["material"])
            f.write('idle_animation = "relax"\n')
            f.write("\n")
            for name, joint in rig["sockets"]:
                f.write("[[rig.socket]]\n")
                f.write('name = "%s"\n' % name)
                f.write('joint = "%s"\n' % joint)
                # Zero on purpose: a socket says *where on the skeleton*, and
                # the weapon's own attach_offset says *where in the hand*.
                # Splitting one distance across both makes a weapon impossible
                # to seat -- you drag one number and two things move.
                f.write("offset = [0.0, 0.0, 0.0]\n")
                f.write("rotation = [0.0, 0.0, 0.0]\n")
                f.write("scale = 1.0\n\n")


def write_sidecar(rig: dict) -> None:
    """Mark the rig's .glb as gltf2ozz's, not the static mesh exporter's.

    Without this the asset pipeline classifies a skinned .glb into the Mesh row
    by its extension, and the Mesh Exporter refuses it -- "animations require
    skeletal/deforming model import" -- which fails the build for a file that is
    not its business. The shipped arms_rig.glb has carried the same sidecar
    since before these rigs existed; this is that, generated.
    """
    guid = hashlib.sha1(rig["id"].encode()).hexdigest()[:16]
    with open(rig["glb"] + ".meta", "w") as f:
        f.write(
            "# Resource database record. Written by tools/author_hand_rigs.py.\n"
            "#\n"
            "# This .glb feeds TWO rows of the asset pipeline -- Skel. Hierarchy\n"
            "# and Animation Clips -- both of which gltf2ozz owns. It is NOT a\n"
            "# static mesh: baking a skinned rig to an .rmesh would silently\n"
            "# publish its bind pose.\n"
            "schema = 1\n"
            'guid = "%s"\n'
            'type = "mesh"\n'
            'name = "%s"\n'
            'tags = ["viewmodel", "skinned", "hands"]\n'
            "\n"
            "[import]\n"
            "skip = true\n"
            'skip_reason = "skinned rig: gltf2ozz owns it"\n'
            % (guid, rig["label"]))


def sockets_for(joints: list) -> list:
    """The named attach points, from the joints this rig actually has."""
    wanted = [
        ("right_hand", "hand.R"),
        ("left_hand", "hand.L"),
        ("right_forearm", "forearm.R"),
        ("left_forearm", "forearm.L"),
        ("right_index_tip", "f_index.02.R"),
        ("right_middle_tip", "f_middle.02.R"),
        ("right_ring_tip", "f_ring.02.R"),
        ("right_thumb_tip", "thumb.02.R"),
    ]
    present = set(joints)
    return [(name, joint) for name, joint in wanted if joint in present]


def label_for(rig: str) -> str:
    return rig.replace("hands_", "").replace("_", " ").title() + " Hands"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rig", action="append", default=[])
    parser.add_argument("--skip-cook", action="store_true")
    parser.add_argument("--gltf2ozz", default="")
    args = parser.parse_args()

    wanted = [r for r in RIGS if not args.rig or r[0] in args.rig]
    if not wanted:
        raise SystemExit("no such rig: " + ", ".join(args.rig))

    mesh_dir = os.path.join(ASSETS, "meshes", "viewmodels")
    os.makedirs(mesh_dir, exist_ok=True)

    jobs = []
    published = []
    for rig, source, body, suit in wanted:
        texture, cells = build_atlas(rig, body, suit)
        slots = {str(slot): (0 if cell == "body" else 1)
                 for slot, cell in SLOT_CELLS[source].items()}
        glb = os.path.join(mesh_dir, rig + ".glb")
        jobs.append({"id": rig, "fbx": os.path.join(SOURCE, source + ".fbx"),
                     "glb": glb,
                     "remap": {"cells": cells, "slots": slots}})
        published.append({"id": rig, "label": label_for(rig),
                          "texture": texture,
                          "material": "Builtin/Viewmodel/%s"
                                      % "".join(p.capitalize()
                                                for p in rig.split("_")[1:]),
                          "glb": glb})

    report_path = os.path.join(REPO, "build", "hand_rigs_report.json")
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    report = run_blender(jobs, report_path)
    for error in report["errors"]:
        print("  ! " + error)
    built = {row["id"]: row for row in report["rigs"]}
    if not built:
        raise SystemExit("no rig was authored")

    tool = "" if args.skip_cook else find_gltf2ozz(args.gltf2ozz)
    if not args.skip_cook and not tool:
        print("gltf2ozz not built; skipping the cook "
              "(build it, then re-run without --skip-cook)")

    final = []
    for rig in published:
        if rig["id"] not in built:
            continue
        rig["sockets"] = sockets_for(built[rig["id"]]["joints"])
        if tool and not cook(rig["id"], rig["glb"], tool):
            print("  ! cook failed for " + rig["id"])
            continue
        write_sidecar(rig)
        final.append(rig)
        print("%-20s %2d sockets, %2d clips" %
              (rig["id"], len(rig["sockets"]), len(built[rig["id"]]["clips"])))

    if final:
        write_materials(final)
        write_config(final, final[0]["id"])
    print("\n%d hand rigs authored" % len(final))
    return 0 if len(final) == len(published) else 1


if __name__ == "__main__":
    sys.exit(main())
