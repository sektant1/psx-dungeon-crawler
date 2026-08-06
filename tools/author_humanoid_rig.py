#!/usr/bin/env python3
"""Humanoid.blend -> a rigged, animated GLB every actor can wear.

`prop_humanoid_mannequin.obj` is the mesh the kit already ships: 203 vertices,
1.8 m tall, A-pose, facing +Z, colour baked into the vertices. It has no
armature and no animation, which is why the player's third-person avatar, every
enemy and every NPC drew as a capsule.

This script is the missing authoring step. It builds a 22-bone humanoid rig on
that mesh, skins it, authors the actor clip library procedurally, and exports
one GLB. `gltf2ozz` then cooks that GLB into the runtime skeleton + clips the
engine's eng::animation already knows how to load -- the same path the
first-person hands take, so there is no second animation stack.

Why procedural clips rather than authored ones: there is no animator on this
project and no motion capture in the repository. Hand-written curves are
reproducible, diffable, and tunable from one table -- and re-running this script
after changing a number is the whole iteration loop.

Usage:
  tools/author_humanoid_rig.py [--out DIR] [--preview DIR]

  --out       where humanoid_rig.glb is written
              (default: assets/source/models/actors)
  --preview   also render a contact sheet of representative poses there, so a
              change to the clip table can be checked without launching the game

Requires the `blender` CLI on PATH. Everything below `main()` runs inside
Blender; the top half only re-executes it.
"""

from __future__ import annotations

import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE_BLEND = os.path.join(
    REPO, "assets/source/models/base_player_mesh/Humanoid.blend")
DEFAULT_OUT = os.path.join(REPO, "assets/source/models/actors")
MESH_NAME = "HumanoidBase_NotOverlapping"


def _relaunch() -> int:
    args = sys.argv[1:]
    command = ["blender", "-b", SOURCE_BLEND, "--python", os.path.abspath(__file__)]
    if args:
        command += ["--"] + args
    return subprocess.call(command)


try:
    import bpy  # type: ignore
    from mathutils import Matrix, Quaternion, Vector  # type: ignore
except ImportError:  # Running as a plain script: re-enter through Blender.
    if __name__ == "__main__":
        sys.exit(_relaunch())
    raise

import math

# --- conventions ------------------------------------------------------------
#
# Blender is Z-up with -Y forward; the engine is Y-up, and the glTF exporter
# performs that conversion (see tools/blend_to_obj.py, which bakes the same
# matrix by hand). So everything below is authored in Blender space and comes
# out the far end in the engine's.
#
# The engine's forward is a NODE's local -Z. Two independent places say so: the
# camera's view matrix is `inverse(cameraWorld)`, the GL convention where a
# camera looks down -Z; and FpsController::forward() is
# (-sin yaw, 0, -cos yaw), which is local -Z rotated by yaw. So a model whose
# front faces +Z, dropped onto a node oriented by a character's facing yaw,
# renders back-to-front -- which is exactly what the first build did.
#
# The mannequin is modelled facing +Z, so both the mesh and the skeleton are
# turned 180 degrees about up on the way out. Doing it here rather than
# compensating in the game keeps ONE convention: the editor's gizmos, the scene
# format's rotations and the runtime all agree about which way an actor points.
YAW_FLIP = True

# The three character axes, named rather than indexed, because "rotate about Y"
# is a coin flip and "yaw left" is not. These are in Blender space and are
# unaffected by the flip -- they describe the character relative to itself, and
# the flip only changes which engine direction that corresponds to.
AXIS_UP = Vector((0.0, 0.0, 1.0))
AXIS_FORWARD = Vector((0.0, 1.0, 0.0)) if YAW_FLIP else Vector((0.0, -1.0, 0.0))
AXIS_RIGHT = Vector((1.0, 0.0, 0.0)) if YAW_FLIP else Vector((-1.0, 0.0, 0.0))


def pitch(degrees: float) -> Quaternion:
    """Positive tips the bone's far end forward (a nod, a leg swinging out)."""
    return Quaternion(AXIS_RIGHT, math.radians(-degrees))


def yaw(degrees: float) -> Quaternion:
    """Positive turns toward the character's left."""
    return Quaternion(AXIS_UP, math.radians(degrees))


def roll(degrees: float) -> Quaternion:
    """Positive leans toward the character's right."""
    return Quaternion(AXIS_FORWARD, math.radians(degrees))


IDENTITY = Quaternion((1.0, 0.0, 0.0, 0.0))


def mirror_quat(q: Quaternion) -> Quaternion:
    """The same rotation seen in a mirror down the character's centre line.

    A rotation axis is a pseudovector, so reflecting through the plane x=0
    negates the components *parallel* to that plane and keeps the perpendicular
    one. Getting this backwards is the classic "the left arm swings the wrong
    way on the second step" bug.
    """
    return Quaternion((q.w, q.x, -q.y, -q.z))


# --- the skeleton -----------------------------------------------------------
#
# Head/tail positions in ENGINE space (x = character's left, y = up,
# z = forward), measured off the mannequin's own vertices: the leg column sits
# at x=0.13, the hip loop at y=0.91, the shoulder loop at y=1.39-1.50, the arm
# runs from (0.20,1.45) out to the hand at (0.81,1.01). Bones that miss those
# landmarks produce the tell-tale low-poly failure where a knee bend pinches the
# thigh instead of the shin.
#
# Only the left side is written; the right is mirrored, so the two can never
# drift apart.
BONES_CENTRE = [
    # name,     head,               tail,               parent,     connected
    ("root", (0.0, 0.0, 0.0), (0.0, 0.30, 0.0), None, False),
    ("hips", (0.0, 0.95, 0.0), (0.0, 1.05, 0.0), "root", False),
    ("spine", (0.0, 1.05, 0.0), (0.0, 1.19, 0.0), "hips", True),
    ("chest", (0.0, 1.19, 0.0), (0.0, 1.44, 0.0), "spine", True),
    ("neck", (0.0, 1.44, 0.0), (0.0, 1.58, 0.0), "chest", True),
    ("head", (0.0, 1.58, 0.0), (0.0, 1.80, 0.0), "neck", True),
]

BONES_SIDE = [
    ("shoulder", (0.04, 1.42, 0.0), (0.20, 1.45, 0.0), "chest", False),
    ("upperarm", (0.20, 1.45, 0.0), (0.52, 1.22, 0.0), "shoulder", True),
    ("forearm", (0.52, 1.22, 0.0), (0.71, 1.08, 0.0), "upperarm", True),
    ("hand", (0.71, 1.08, 0.0), (0.81, 1.01, 0.0), "forearm", True),
    ("thigh", (0.13, 0.92, 0.0), (0.13, 0.50, 0.005), "hips", False),
    ("shin", (0.13, 0.50, 0.005), (0.14, 0.09, -0.005), "thigh", True),
    ("foot", (0.14, 0.09, -0.005), (0.17, 0.025, 0.09), "shin", True),
    ("toe", (0.17, 0.025, 0.09), (0.175, 0.02, 0.19), "foot", True),
]

# Everything the upper-body mask covers, for the runtime's action layer. Written
# here because this is where the skeleton's shape is decided; the game reads the
# same list out of actors.toml.
UPPER_BODY_ROOTS = ["spine"]


def to_blender(engine_xyz) -> Vector:
    """Bone-table space -> Blender.

    The table is authored as if the character faced engine +Z with its left at
    +X, because that is how the mannequin is modelled and how its vertices
    measure. YAW_FLIP then turns the result 180 degrees about up (Blender Z),
    which is a negation of x and y, so the exported model faces engine -Z as
    the engine's node convention requires. The mesh gets the same turn in
    prepare_mesh; doing one without the other would put the skeleton inside a
    body facing the other way.
    """
    x, y, z = engine_xyz
    blender = Vector((x, -z, y))
    return Vector((-blender.x, -blender.y, blender.z)) if YAW_FLIP else blender


def build_skeleton() -> list:
    """The bone table, both sides, in parent-before-child order."""
    table = []
    for name, head, tail, parent, connected in BONES_CENTRE:
        table.append((name, to_blender(head), to_blender(tail), parent, connected))
    for suffix, sign in ((".L", 1.0), (".R", -1.0)):
        for name, head, tail, parent, connected in BONES_SIDE:
            mirrored_head = (head[0] * sign, head[1], head[2])
            mirrored_tail = (tail[0] * sign, tail[1], tail[2])
            parent_name = parent if parent in ("chest", "hips") else parent + suffix
            table.append((name + suffix, to_blender(mirrored_head),
                          to_blender(mirrored_tail), parent_name, connected))
    return table


BONE_TABLE = build_skeleton()
BONE_NAMES = [row[0] for row in BONE_TABLE]


# --- poses ------------------------------------------------------------------
#
# A pose is a rotation per bone plus an optional whole-body offset. Rotations
# are PARENT-RELATIVE deltas from the rest pose, expressed in character axes:
# bending the elbow after the shoulder has moved is one number either way, and
# the spine carrying the head along comes out for free.


class Pose:
    __slots__ = ("rotations", "offset")

    def __init__(self, rotations=None, offset=None):
        self.rotations = dict(rotations or {})
        self.offset = Vector(offset) if offset is not None else Vector((0.0, 0.0, 0.0))

    def __or__(self, other: "Pose") -> "Pose":
        """Layer `other` on top of self (rotations compose, offsets add).

        The layered rotation goes on the LEFT, so it acts about the character's
        own axes *after* the base has been applied. That is what makes "swing
        the arm forward 90 degrees" mean the same thing whether the arm starts
        in the mesh's A-pose or hanging at the side -- compose the other way and
        every arm rotation is measured against the modelled A-pose and reads as
        a twist instead of a swing.
        """
        merged = dict(self.rotations)
        for bone, rotation in other.rotations.items():
            merged[bone] = rotation @ merged.get(bone, IDENTITY)
        return Pose(merged, self.offset + other.offset)

    def mirrored(self) -> "Pose":
        flipped = {}
        for bone, rotation in self.rotations.items():
            if bone.endswith(".L"):
                name = bone[:-2] + ".R"
            elif bone.endswith(".R"):
                name = bone[:-2] + ".L"
            else:
                name = bone
            flipped[name] = mirror_quat(rotation)
        return Pose(flipped,
                    Vector((-self.offset.x, self.offset.y, self.offset.z)))


def lerp_pose(a: Pose, b: Pose, t: float) -> Pose:
    bones = set(a.rotations) | set(b.rotations)
    blended = {}
    for bone in bones:
        qa = a.rotations.get(bone, IDENTITY)
        qb = b.rotations.get(bone, IDENTITY)
        blended[bone] = qa.slerp(qb, t)
    return Pose(blended, a.offset.lerp(b.offset, t))


def smoothstep(t: float) -> float:
    t = min(1.0, max(0.0, t))
    return t * t * (3.0 - 2.0 * t)


def sample_keys(keys, phase: float, loop: bool) -> Pose:
    """Pose-to-pose interpolation over keys given as (phase, Pose).

    Eased rather than linear: a linear ramp between two extreme poses reads as a
    machine, and the ease is what makes a two-key flinch look like it has weight.
    """
    if loop:
        phase = phase % 1.0
    else:
        phase = min(1.0, max(0.0, phase))
    for index in range(len(keys) - 1):
        t0, p0 = keys[index]
        t1, p1 = keys[index + 1]
        if t0 <= phase <= t1:
            span = max(1e-6, t1 - t0)
            return lerp_pose(p0, p1, smoothstep((phase - t0) / span))
    if phase < keys[0][0]:
        return keys[0][1]
    return keys[-1][1]


# --- the standing base ------------------------------------------------------
#
# The mannequin's rest is an A-pose with the arms 36 degrees below horizontal --
# that is how the mesh was modelled, and the skin weights depend on it. Every
# clip therefore starts from a base that carries the arms down to the sides;
# authoring "arms down" once here is what keeps it out of twenty clip tables.
BASE = Pose({
    "upperarm.L": roll(-38) @ pitch(-4),
    "upperarm.R": roll(38) @ pitch(-4),
    "forearm.L": pitch(12) @ yaw(-6),
    "forearm.R": pitch(12) @ yaw(6),
    "hand.L": pitch(6),
    "hand.R": pitch(6),
    "shoulder.L": roll(1),
    "shoulder.R": roll(-1),
    "thigh.L": pitch(1),
    "thigh.R": pitch(1),
    "shin.L": pitch(-3),
    "shin.R": pitch(-3),
    "foot.L": pitch(2),
    "foot.R": pitch(2),
    "spine": pitch(2),
    "chest": pitch(-2),
})

# The fighting stance: weight down and back, torso bladed, guard up. Enemies and
# the player share it -- what differs is which clip the state machine selects,
# not what a stance means.
GUARD = BASE | Pose({
    "hips": pitch(6),
    "spine": pitch(5),
    "chest": pitch(2) @ yaw(14),
    "neck": yaw(-8),
    "head": yaw(-6) @ pitch(4),
    "thigh.L": pitch(20) @ yaw(-8),
    "shin.L": pitch(-34),
    "foot.L": pitch(14),
    "thigh.R": pitch(-8) @ yaw(-14),
    "shin.R": pitch(-22),
    "foot.R": pitch(16),
    "upperarm.L": pitch(34) @ roll(10),
    "forearm.L": pitch(72),
    "hand.L": pitch(-10),
    "upperarm.R": pitch(22) @ roll(-6),
    "forearm.R": pitch(64),
    "hand.R": pitch(-8),
}, offset=(0.0, 0.0, -0.055))


# --- the clip library -------------------------------------------------------
#
# One table. Each clip is a duration, a loop flag and a list of (phase, Pose)
# keys; half of the cyclic ones are written for the left leg and mirrored, which
# is why a walk cannot end up limping.
#
# Rotations read RIGHT TO LEFT: `pitch(30) @ roll(10)` rolls first, then
# pitches about the character's fixed axes.


def P(rotations=None, offset=None) -> Pose:
    return BASE | Pose(rotations, offset)


def half_cycle(keys):
    """A cycle written for one leg, completed by mirroring the other half."""
    full = list(keys)
    for phase, pose in keys:
        if phase >= 0.5:
            raise ValueError("half-cycle keys must lie in [0, 0.5)")
        full.append((phase + 0.5, pose.mirrored()))
    full.sort(key=lambda item: item[0])
    full.append((1.0, keys[0][1]))
    return full


# --- standing ---------------------------------------------------------------

IDLE = [
    (0.00, P({"hips": roll(2) @ yaw(-1.5), "spine": pitch(1), "chest": pitch(1.5),
              "neck": yaw(2), "head": yaw(2.5) @ pitch(1),
              "upperarm.L": pitch(2) @ roll(1), "upperarm.R": pitch(-1) @ roll(-2),
              "thigh.L": pitch(1), "shin.L": pitch(-3), "shin.R": pitch(-1)},
             offset=(0.006, 0.0, -0.004))),
    (0.25, P({"hips": roll(1) @ yaw(-0.5), "spine": pitch(-1.5), "chest": pitch(-3),
              "neck": pitch(-1), "head": yaw(1),
              "shoulder.L": roll(2.5), "shoulder.R": roll(-2.5),
              "upperarm.L": pitch(-1) @ roll(-2), "upperarm.R": pitch(2) @ roll(2),
              "shin.L": pitch(-2), "shin.R": pitch(-2)},
             offset=(0.002, 0.0, 0.008))),
    (0.50, P({"hips": roll(-2) @ yaw(1.5), "spine": pitch(1), "chest": pitch(1.5),
              "neck": yaw(-2), "head": yaw(-2.5) @ pitch(1),
              "upperarm.L": pitch(-1) @ roll(-1), "upperarm.R": pitch(2) @ roll(2),
              "thigh.R": pitch(1), "shin.R": pitch(-3), "shin.L": pitch(-1)},
             offset=(-0.006, 0.0, -0.004))),
    (0.75, P({"hips": roll(-1) @ yaw(0.5), "spine": pitch(-1.5), "chest": pitch(-3),
              "neck": pitch(-1), "head": yaw(-1),
              "shoulder.L": roll(2.5), "shoulder.R": roll(-2.5),
              "upperarm.L": pitch(2) @ roll(2), "upperarm.R": pitch(-1) @ roll(-2),
              "shin.L": pitch(-2), "shin.R": pitch(-2)},
             offset=(-0.002, 0.0, 0.008))),
    (1.00, None),  # patched below to key 0
]
IDLE[-1] = (1.00, IDLE[0][1])

IDLE_COMBAT = [
    (0.00, GUARD | Pose({"chest": pitch(1.5), "upperarm.L": pitch(3),
                         "upperarm.R": pitch(-2), "head": pitch(-1)},
                        offset=(0.0, 0.0, 0.008))),
    (0.30, GUARD | Pose({"hips": roll(1.5), "chest": pitch(-2) @ yaw(2),
                         "upperarm.L": pitch(-3), "forearm.L": pitch(4),
                         "upperarm.R": pitch(3), "head": yaw(2)},
                        offset=(0.008, 0.0, -0.012))),
    (0.62, GUARD | Pose({"chest": pitch(2), "upperarm.L": pitch(2),
                         "upperarm.R": pitch(-3), "head": pitch(1)},
                        offset=(0.0, 0.0, 0.006))),
    (1.00, None),
]
IDLE_COMBAT[-1] = (1.00, IDLE_COMBAT[0][1])

DORMANT_BASE = P({
    "hips": pitch(6), "spine": pitch(16), "chest": pitch(12),
    "neck": pitch(18), "head": pitch(10),
    "shoulder.L": roll(-6), "shoulder.R": roll(6),
    "upperarm.L": pitch(14) @ roll(4), "upperarm.R": pitch(14) @ roll(-4),
    "forearm.L": pitch(22), "forearm.R": pitch(22),
    "thigh.L": pitch(10), "thigh.R": pitch(10),
    "shin.L": pitch(-18), "shin.R": pitch(-18),
    "foot.L": pitch(9), "foot.R": pitch(9),
}, offset=(0.0, 0.0, -0.075))

DORMANT = [
    (0.00, DORMANT_BASE),
    (0.42, DORMANT_BASE | Pose({"chest": pitch(-3), "neck": pitch(-2),
                                "head": pitch(-2), "hips": roll(1.5)},
                               offset=(0.0, 0.0, 0.014))),
    (1.00, DORMANT_BASE),
]

TALK = [
    (0.00, P({"chest": yaw(3), "head": yaw(-3) @ pitch(2),
              "upperarm.R": pitch(18) @ roll(-8), "forearm.R": pitch(38)})),
    (0.28, P({"chest": yaw(-2) @ pitch(2), "head": yaw(4) @ pitch(-3),
              "upperarm.R": pitch(52) @ roll(-16), "forearm.R": pitch(76),
              "hand.R": pitch(-22),
              "upperarm.L": pitch(14) @ roll(6), "forearm.L": pitch(30)},
             offset=(0.0, 0.0, 0.01))),
    (0.52, P({"chest": yaw(4), "head": yaw(-5),
              "upperarm.R": pitch(30) @ roll(-10), "forearm.R": pitch(48),
              "upperarm.L": pitch(34) @ roll(14), "forearm.L": pitch(62),
              "hand.L": pitch(-18)})),
    (0.78, P({"chest": yaw(-1), "head": pitch(3),
              "upperarm.R": pitch(20) @ roll(-6), "forearm.R": pitch(40),
              "upperarm.L": pitch(8), "forearm.L": pitch(24)},
             offset=(0.0, 0.0, -0.008))),
    (1.00, None),
]
TALK[-1] = (1.00, TALK[0][1])


# --- walking ----------------------------------------------------------------

WALK_F = half_cycle([
    # Contact: left heel strikes forward, right toe pushes off behind.
    (0.000, P({"hips": yaw(-7) @ roll(-1), "spine": pitch(4) @ yaw(3),
               "chest": yaw(7), "neck": yaw(-4), "head": yaw(-2),
               "thigh.L": pitch(26), "shin.L": pitch(-6), "foot.L": pitch(-13),
               "thigh.R": pitch(-24), "shin.R": pitch(-16), "foot.R": pitch(26),
               "toe.R": pitch(24),
               "shoulder.L": roll(-2), "shoulder.R": roll(2),
               "upperarm.L": pitch(-21) @ roll(3), "forearm.L": pitch(14),
               "upperarm.R": pitch(23) @ roll(-3), "forearm.R": pitch(31)},
              offset=(0.0, 0.0, -0.022))),
    # Absorb: the knee takes the weight and the pelvis drops.
    (0.125, P({"hips": yaw(-4) @ roll(-3), "spine": pitch(5) @ yaw(2),
               "chest": yaw(4), "neck": yaw(-3),
               "thigh.L": pitch(17), "shin.L": pitch(-21), "foot.L": pitch(-1),
               "thigh.R": pitch(-17), "shin.R": pitch(-27), "foot.R": pitch(30),
               "toe.R": pitch(32),
               "upperarm.L": pitch(-14) @ roll(2), "forearm.L": pitch(12),
               "upperarm.R": pitch(16) @ roll(-2), "forearm.R": pitch(26)},
              offset=(0.0, 0.0, -0.048))),
    # Passing: support leg straight, swing leg's knee comes through.
    (0.250, P({"hips": roll(-2.5), "spine": pitch(3),
               "thigh.L": pitch(-3), "shin.L": pitch(-6), "foot.L": pitch(5),
               "thigh.R": pitch(7), "shin.R": pitch(-54), "foot.R": pitch(4),
               "upperarm.L": pitch(-3), "forearm.L": pitch(12),
               "upperarm.R": pitch(3), "forearm.R": pitch(20)},
              offset=(0.0, 0.0, 0.006))),
    # Push-off: the body rises over the straight support leg.
    (0.375, P({"hips": yaw(4) @ roll(-1), "spine": pitch(4) @ yaw(-2),
               "chest": yaw(-4), "neck": yaw(2),
               "thigh.L": pitch(-15), "shin.L": pitch(-6), "foot.L": pitch(12),
               "thigh.R": pitch(19), "shin.R": pitch(-31), "foot.R": pitch(-7),
               "upperarm.L": pitch(11) @ roll(-1), "forearm.L": pitch(16),
               "upperarm.R": pitch(-12) @ roll(1), "forearm.R": pitch(24)},
              offset=(0.0, 0.0, 0.014))),
])

WALK_B = half_cycle([
    (0.000, P({"hips": yaw(6) @ roll(-1), "spine": pitch(-4) @ yaw(-2),
               "chest": yaw(-5) @ pitch(-2), "neck": yaw(3),
               "thigh.L": pitch(-21), "shin.L": pitch(-24), "foot.L": pitch(20),
               "toe.L": pitch(14),
               "thigh.R": pitch(15), "shin.R": pitch(-11), "foot.R": pitch(-2),
               "upperarm.L": pitch(15) @ roll(2), "forearm.L": pitch(20),
               "upperarm.R": pitch(-16) @ roll(-2), "forearm.R": pitch(26)},
              offset=(0.0, 0.0, -0.018))),
    (0.125, P({"hips": yaw(3) @ roll(-3), "spine": pitch(-3),
               "chest": yaw(-3) @ pitch(-2),
               "thigh.L": pitch(-9), "shin.L": pitch(-34), "foot.L": pitch(24),
               "toe.L": pitch(20),
               "thigh.R": pitch(9), "shin.R": pitch(-20), "foot.R": pitch(4),
               "upperarm.L": pitch(9), "forearm.L": pitch(18),
               "upperarm.R": pitch(-10), "forearm.R": pitch(24)},
              offset=(0.0, 0.0, -0.042))),
    (0.250, P({"hips": roll(-2), "spine": pitch(-3),
               "thigh.L": pitch(5), "shin.L": pitch(-48), "foot.L": pitch(8),
               "thigh.R": pitch(-2), "shin.R": pitch(-7), "foot.R": pitch(4),
               "upperarm.L": pitch(2), "forearm.L": pitch(16),
               "upperarm.R": pitch(-2), "forearm.R": pitch(20)},
              offset=(0.0, 0.0, 0.004))),
    (0.375, P({"hips": yaw(-3) @ roll(-1), "spine": pitch(-4) @ yaw(2),
               "chest": yaw(3) @ pitch(-2),
               "thigh.L": pitch(16), "shin.L": pitch(-22), "foot.L": pitch(-4),
               "thigh.R": pitch(-14), "shin.R": pitch(-16), "foot.R": pitch(14),
               "upperarm.L": pitch(-9), "forearm.L": pitch(16),
               "upperarm.R": pitch(10), "forearm.R": pitch(22)},
              offset=(0.0, 0.0, 0.010))),
])

# Side-step, authored leading with the left leg. `walk_r` is this mirrored, so
# the two strafes cannot disagree about how much the torso leans.
WALK_L = [
    (0.00, P({"hips": roll(-3), "spine": roll(-3), "chest": roll(-2) @ yaw(-4),
              "neck": yaw(4), "head": yaw(3),
              "thigh.L": roll(17) @ pitch(2), "shin.L": pitch(-9),
              "foot.L": roll(-12),
              "thigh.R": roll(-3), "shin.R": pitch(-6), "foot.R": roll(3),
              "upperarm.L": roll(-9) @ pitch(3), "upperarm.R": roll(6),
              "forearm.L": pitch(14), "forearm.R": pitch(18)},
             offset=(0.02, 0.0, -0.014))),
    (0.25, P({"hips": roll(-5), "spine": roll(-2), "chest": roll(-1) @ yaw(-2),
              "neck": yaw(2),
              "thigh.L": roll(9) @ pitch(1), "shin.L": pitch(-14),
              "foot.L": roll(-6),
              "thigh.R": roll(11), "shin.R": pitch(-30), "foot.R": roll(-6),
              "upperarm.L": roll(-5), "upperarm.R": roll(10),
              "forearm.L": pitch(12), "forearm.R": pitch(22)},
             offset=(0.006, 0.0, -0.036))),
    (0.50, P({"hips": roll(-2), "spine": roll(-2), "chest": yaw(-3),
              "neck": yaw(3),
              "thigh.L": roll(3), "shin.L": pitch(-6),
              "thigh.R": roll(6), "shin.R": pitch(-12), "foot.R": roll(-3),
              "upperarm.L": roll(-3), "upperarm.R": roll(4),
              "forearm.L": pitch(12), "forearm.R": pitch(16)},
             offset=(0.0, 0.0, 0.008))),
    (0.75, P({"hips": roll(-4), "spine": roll(-4), "chest": roll(-3) @ yaw(-5),
              "neck": yaw(5), "head": yaw(3),
              "thigh.L": roll(23) @ pitch(3), "shin.L": pitch(-25),
              "foot.L": roll(-14),
              "thigh.R": roll(1), "shin.R": pitch(-5),
              "upperarm.L": roll(-12) @ pitch(4), "upperarm.R": roll(8),
              "forearm.L": pitch(16), "forearm.R": pitch(20)},
             offset=(0.014, 0.0, -0.012))),
    (1.00, None),
]
WALK_L[-1] = (1.00, WALK_L[0][1])


# --- running ----------------------------------------------------------------

RUN_F = half_cycle([
    # Contact, already leaning into the stride.
    (0.000, P({"hips": yaw(-10) @ pitch(3), "spine": pitch(11) @ yaw(5),
               "chest": pitch(3) @ yaw(11), "neck": pitch(-8) @ yaw(-6),
               "head": pitch(-4),
               "thigh.L": pitch(32), "shin.L": pitch(-24), "foot.L": pitch(-6),
               "thigh.R": pitch(-36), "shin.R": pitch(-42), "foot.R": pitch(22),
               "toe.R": pitch(24),
               "shoulder.L": roll(-4), "shoulder.R": roll(4),
               "upperarm.L": pitch(-44) @ roll(6), "forearm.L": pitch(88),
               "upperarm.R": pitch(46) @ roll(-6), "forearm.R": pitch(96),
               "hand.L": pitch(-14), "hand.R": pitch(-14)},
              offset=(0.0, 0.0, -0.012))),
    # Deepest absorb: this is the frame the whole run reads its weight from.
    (0.120, P({"hips": yaw(-6) @ pitch(4) @ roll(-4),
               "spine": pitch(13) @ yaw(3), "chest": pitch(4) @ yaw(7),
               "neck": pitch(-10),
               "thigh.L": pitch(16), "shin.L": pitch(-44), "foot.L": pitch(2),
               "thigh.R": pitch(-22), "shin.R": pitch(-72), "foot.R": pitch(24),
               "toe.R": pitch(30),
               "upperarm.L": pitch(-30) @ roll(4), "forearm.L": pitch(80),
               "upperarm.R": pitch(32) @ roll(-4), "forearm.R": pitch(86)},
              offset=(0.0, 0.0, -0.072))),
    # Push-off: support leg extends, swing knee drives through high.
    (0.250, P({"hips": pitch(3) @ roll(-3), "spine": pitch(11),
               "chest": pitch(3), "neck": pitch(-8),
               "thigh.L": pitch(-22), "shin.L": pitch(-14), "foot.L": pitch(28),
               "toe.L": pitch(26),
               "thigh.R": pitch(20), "shin.R": pitch(-96), "foot.R": pitch(6),
               "upperarm.L": pitch(-6), "forearm.L": pitch(84),
               "upperarm.R": pitch(6), "forearm.R": pitch(90)},
              offset=(0.0, 0.0, 0.012))),
    # Flight: both feet off the ground, trailing leg tucked.
    (0.375, P({"hips": yaw(7) @ pitch(3), "spine": pitch(11) @ yaw(-4),
               "chest": pitch(3) @ yaw(-9), "neck": pitch(-8) @ yaw(5),
               "thigh.L": pitch(-30), "shin.L": pitch(-58), "foot.L": pitch(20),
               "thigh.R": pitch(40), "shin.R": pitch(-56), "foot.R": pitch(-8),
               "upperarm.L": pitch(30) @ roll(-4), "forearm.L": pitch(92),
               "upperarm.R": pitch(-30) @ roll(4), "forearm.R": pitch(88)},
              offset=(0.0, 0.0, 0.062))),
])

RUN_B = half_cycle([
    (0.000, P({"hips": yaw(8) @ pitch(-4), "spine": pitch(-9) @ yaw(-4),
               "chest": pitch(-3) @ yaw(-8), "neck": pitch(9),
               "thigh.L": pitch(-30), "shin.L": pitch(-34), "foot.L": pitch(26),
               "toe.L": pitch(20),
               "thigh.R": pitch(24), "shin.R": pitch(-18), "foot.R": pitch(-4),
               "upperarm.L": pitch(28) @ roll(4), "forearm.L": pitch(78),
               "upperarm.R": pitch(-28) @ roll(-4), "forearm.R": pitch(84)},
              offset=(0.0, 0.0, -0.014))),
    (0.120, P({"hips": yaw(5) @ pitch(-4) @ roll(-4), "spine": pitch(-8),
               "chest": pitch(-3) @ yaw(-5), "neck": pitch(9),
               "thigh.L": pitch(-14), "shin.L": pitch(-56), "foot.L": pitch(30),
               "toe.L": pitch(26),
               "thigh.R": pitch(14), "shin.R": pitch(-34), "foot.R": pitch(6),
               "upperarm.L": pitch(18), "forearm.L": pitch(72),
               "upperarm.R": pitch(-18), "forearm.R": pitch(78)},
              offset=(0.0, 0.0, -0.062))),
    (0.250, P({"hips": pitch(-4) @ roll(-3), "spine": pitch(-8),
               "chest": pitch(-3), "neck": pitch(8),
               "thigh.L": pitch(10), "shin.L": pitch(-86), "foot.L": pitch(10),
               "thigh.R": pitch(-14), "shin.R": pitch(-16), "foot.R": pitch(24),
               "toe.R": pitch(22),
               "upperarm.L": pitch(2), "forearm.L": pitch(76),
               "upperarm.R": pitch(-2), "forearm.R": pitch(80)},
              offset=(0.0, 0.0, 0.014))),
    (0.375, P({"hips": yaw(-6) @ pitch(-4), "spine": pitch(-9) @ yaw(4),
               "chest": pitch(-3) @ yaw(7), "neck": pitch(9),
               "thigh.L": pitch(30), "shin.L": pitch(-46), "foot.L": pitch(-6),
               "thigh.R": pitch(-26), "shin.R": pitch(-50), "foot.R": pitch(22),
               "upperarm.L": pitch(-24), "forearm.L": pitch(80),
               "upperarm.R": pitch(24), "forearm.R": pitch(84)},
              offset=(0.0, 0.0, 0.056))),
])


# --- air --------------------------------------------------------------------

JUMP = [
    (0.00, P({"hips": pitch(10), "spine": pitch(14), "chest": pitch(6),
              "neck": pitch(-12),
              "thigh.L": pitch(34), "thigh.R": pitch(34),
              "shin.L": pitch(-62), "shin.R": pitch(-62),
              "foot.L": pitch(26), "foot.R": pitch(26),
              "upperarm.L": pitch(-42) @ roll(6), "upperarm.R": pitch(-42) @ roll(-6),
              "forearm.L": pitch(26), "forearm.R": pitch(26)},
             offset=(0.0, 0.0, -0.135))),
    (0.34, P({"hips": pitch(-2), "spine": pitch(-5), "chest": pitch(-3),
              "neck": pitch(4),
              "thigh.L": pitch(-6), "thigh.R": pitch(-6),
              "shin.L": pitch(-3), "shin.R": pitch(-3),
              "foot.L": pitch(30), "foot.R": pitch(30),
              "toe.L": pitch(20), "toe.R": pitch(20),
              "upperarm.L": pitch(84) @ roll(-10),
              "upperarm.R": pitch(84) @ roll(10),
              "forearm.L": pitch(16), "forearm.R": pitch(16)},
             offset=(0.0, 0.0, 0.05))),
    (1.00, P({"hips": pitch(2), "spine": pitch(4), "chest": pitch(2),
              "thigh.L": pitch(26), "shin.L": pitch(-54), "foot.L": pitch(10),
              "thigh.R": pitch(-10), "shin.R": pitch(-34), "foot.R": pitch(16),
              "upperarm.L": pitch(46) @ roll(-16),
              "upperarm.R": pitch(38) @ roll(14),
              "forearm.L": pitch(40), "forearm.R": pitch(34)},
             offset=(0.0, 0.0, 0.012))),
]

FALL = [
    (0.00, P({"spine": pitch(-5), "chest": pitch(-2), "neck": pitch(6),
              "thigh.L": pitch(12), "shin.L": pitch(-38), "foot.L": pitch(16),
              "thigh.R": pitch(-14), "shin.R": pitch(-26), "foot.R": pitch(18),
              "upperarm.L": pitch(58) @ roll(-22),
              "upperarm.R": pitch(52) @ roll(20),
              "forearm.L": pitch(48), "forearm.R": pitch(42)})),
    (0.50, P({"spine": pitch(-7) @ yaw(2), "chest": pitch(-3) @ yaw(-3),
              "neck": pitch(7),
              "thigh.L": pitch(8), "shin.L": pitch(-30), "foot.L": pitch(18),
              "thigh.R": pitch(-10), "shin.R": pitch(-34), "foot.R": pitch(14),
              "upperarm.L": pitch(48) @ roll(-28),
              "upperarm.R": pitch(62) @ roll(14),
              "forearm.L": pitch(40), "forearm.R": pitch(52)},
             offset=(0.0, 0.0, 0.012))),
    (1.00, None),
]
FALL[-1] = (1.00, FALL[0][1])

LAND = [
    (0.00, P({"hips": pitch(12), "spine": pitch(18), "chest": pitch(8),
              "neck": pitch(-14),
              "thigh.L": pitch(38), "thigh.R": pitch(36),
              "shin.L": pitch(-68), "shin.R": pitch(-66),
              "foot.L": pitch(28), "foot.R": pitch(27),
              "upperarm.L": pitch(38) @ roll(-14),
              "upperarm.R": pitch(34) @ roll(12),
              "forearm.L": pitch(52), "forearm.R": pitch(48)},
             offset=(0.0, -0.02, -0.16))),
    (0.42, P({"hips": pitch(4), "spine": pitch(7), "chest": pitch(3),
              "neck": pitch(-5),
              "thigh.L": pitch(15), "thigh.R": pitch(14),
              "shin.L": pitch(-28), "shin.R": pitch(-27),
              "foot.L": pitch(12), "foot.R": pitch(11),
              "upperarm.L": pitch(14), "upperarm.R": pitch(12),
              "forearm.L": pitch(24), "forearm.R": pitch(22)},
             offset=(0.0, 0.0, -0.05))),
    (1.00, P()),
]


# --- fighting ---------------------------------------------------------------
#
# Timing is authored to the phases the combat system already runs -- windup,
# active, recovery -- so `AttackDef`'s numbers and the pose the model is in are
# the same statement. The strike key is where `activeFiredThisStep` lands.

ATTACK_1 = [
    (0.00, P({"chest": yaw(-4), "upperarm.R": pitch(34) @ roll(-6),
              "forearm.R": pitch(58), "upperarm.L": pitch(10),
              "forearm.L": pitch(30)})),
    (0.32, P({"hips": yaw(-10), "spine": pitch(-8) @ yaw(-12),
              "chest": pitch(-6) @ yaw(-18), "neck": yaw(10),
              "head": yaw(8) @ pitch(-4),
              "upperarm.R": pitch(152) @ roll(-26), "forearm.R": pitch(84),
              "hand.R": pitch(-24),
              "upperarm.L": pitch(30) @ roll(18), "forearm.L": pitch(66),
              "thigh.R": pitch(-8), "shin.R": pitch(-14),
              "thigh.L": pitch(6), "shin.L": pitch(-8)},
             offset=(0.0, 0.05, 0.014))),
    (0.46, P({"hips": yaw(12) @ pitch(8), "spine": pitch(16) @ yaw(10),
              "chest": pitch(10) @ yaw(16), "neck": pitch(-10) @ yaw(-8),
              "head": pitch(6),
              "upperarm.R": pitch(52) @ roll(8), "forearm.R": pitch(8),
              "hand.R": pitch(-18),
              "upperarm.L": pitch(-24) @ roll(-8), "forearm.L": pitch(46),
              "thigh.L": pitch(20), "shin.L": pitch(-26), "foot.L": pitch(6),
              "thigh.R": pitch(-12), "shin.R": pitch(-18), "foot.R": pitch(16)},
             offset=(0.0, -0.11, -0.055))),
    (0.68, P({"hips": yaw(4) @ pitch(3), "spine": pitch(7) @ yaw(3),
              "chest": pitch(4) @ yaw(6),
              "upperarm.R": pitch(40) @ roll(-2), "forearm.R": pitch(40),
              "upperarm.L": pitch(-4), "forearm.L": pitch(34),
              "thigh.L": pitch(9), "shin.L": pitch(-14)},
             offset=(0.0, -0.04, -0.02))),
    (1.00, P()),
]

ATTACK_2 = [
    (0.00, P({"chest": yaw(4), "upperarm.L": pitch(30) @ roll(8),
              "forearm.L": pitch(54), "upperarm.R": pitch(8),
              "forearm.R": pitch(28)})),
    (0.30, P({"hips": yaw(12), "spine": yaw(14) @ pitch(-4),
              "chest": yaw(22) @ pitch(-2), "neck": yaw(-12),
              "head": yaw(-10),
              "upperarm.L": pitch(74) @ roll(-32), "forearm.L": pitch(72),
              "hand.L": pitch(-18),
              "upperarm.R": pitch(24) @ roll(-14), "forearm.R": pitch(52),
              "thigh.L": pitch(-6), "shin.L": pitch(-12)},
             offset=(0.0, 0.04, 0.008))),
    (0.44, P({"hips": yaw(-16) @ pitch(5), "spine": yaw(-14) @ pitch(9),
              "chest": yaw(-24) @ pitch(6), "neck": yaw(14),
              "head": yaw(10) @ pitch(4),
              "upperarm.L": pitch(84) @ roll(46), "forearm.L": pitch(14),
              "hand.L": pitch(-12),
              "upperarm.R": pitch(-18) @ roll(14), "forearm.R": pitch(38),
              "thigh.R": pitch(16), "shin.R": pitch(-22), "foot.R": pitch(4),
              "thigh.L": pitch(-10), "shin.L": pitch(-16), "foot.L": pitch(14)},
             offset=(0.0, -0.10, -0.045))),
    (0.66, P({"hips": yaw(-6), "spine": yaw(-5) @ pitch(4),
              "chest": yaw(-9) @ pitch(3),
              "upperarm.L": pitch(46) @ roll(14), "forearm.L": pitch(40),
              "upperarm.R": pitch(-2), "forearm.R": pitch(30),
              "thigh.R": pitch(7), "shin.R": pitch(-12)},
             offset=(0.0, -0.03, -0.016))),
    (1.00, P()),
]

ATTACK_HEAVY = [
    (0.00, P({"chest": yaw(-4), "upperarm.L": pitch(22) @ roll(10),
              "upperarm.R": pitch(22) @ roll(-10),
              "forearm.L": pitch(48), "forearm.R": pitch(48)})),
    (0.40, P({"hips": yaw(-14) @ pitch(-6), "spine": pitch(-16) @ yaw(-14),
              "chest": pitch(-12) @ yaw(-20), "neck": pitch(12) @ yaw(12),
              "head": pitch(6),
              "upperarm.L": pitch(158) @ roll(16),
              "upperarm.R": pitch(162) @ roll(-20),
              "forearm.L": pitch(72), "forearm.R": pitch(78),
              "hand.L": pitch(-26), "hand.R": pitch(-26),
              "thigh.R": pitch(-16), "shin.R": pitch(-26), "foot.R": pitch(10),
              "thigh.L": pitch(12), "shin.L": pitch(-10)},
             offset=(0.0, 0.09, 0.026))),
    (0.55, P({"hips": yaw(10) @ pitch(14), "spine": pitch(28) @ yaw(8),
              "chest": pitch(18) @ yaw(12), "neck": pitch(-22),
              "head": pitch(-8),
              "upperarm.L": pitch(30) @ roll(-4), "upperarm.R": pitch(34) @ roll(4),
              "forearm.L": pitch(6), "forearm.R": pitch(6),
              "hand.L": pitch(-20), "hand.R": pitch(-20),
              "thigh.L": pitch(36), "shin.L": pitch(-44), "foot.L": pitch(12),
              "thigh.R": pitch(-22), "shin.R": pitch(-30), "foot.R": pitch(26),
              "toe.R": pitch(24)},
             offset=(0.0, -0.19, -0.12))),
    (0.78, P({"hips": pitch(8), "spine": pitch(16), "chest": pitch(10),
              "neck": pitch(-12),
              "upperarm.L": pitch(24), "upperarm.R": pitch(26),
              "forearm.L": pitch(26), "forearm.R": pitch(26),
              "thigh.L": pitch(22), "shin.L": pitch(-30),
              "thigh.R": pitch(-12), "shin.R": pitch(-20), "foot.R": pitch(16)},
             offset=(0.0, -0.09, -0.07))),
    (1.00, P()),
]

CAST = [
    (0.00, P({"upperarm.L": pitch(16) @ roll(6), "upperarm.R": pitch(16) @ roll(-6),
              "forearm.L": pitch(40), "forearm.R": pitch(40)})),
    (0.36, P({"hips": pitch(-4), "spine": pitch(-10), "chest": pitch(-6),
              "neck": pitch(8), "head": pitch(4),
              "upperarm.L": pitch(46) @ roll(26), "upperarm.R": pitch(46) @ roll(-26),
              "forearm.L": pitch(96) @ yaw(-20), "forearm.R": pitch(96) @ yaw(20),
              "hand.L": pitch(-24), "hand.R": pitch(-24),
              "thigh.L": pitch(8), "thigh.R": pitch(8),
              "shin.L": pitch(-16), "shin.R": pitch(-16)},
             offset=(0.0, 0.03, -0.045))),
    (0.56, P({"hips": pitch(6), "spine": pitch(10), "chest": pitch(6),
              "neck": pitch(-6),
              "upperarm.L": pitch(92) @ roll(4), "upperarm.R": pitch(92) @ roll(-4),
              "forearm.L": pitch(8), "forearm.R": pitch(8),
              "hand.L": pitch(-30), "hand.R": pitch(-30),
              "thigh.L": pitch(14), "shin.L": pitch(-20),
              "thigh.R": pitch(-8), "shin.R": pitch(-14), "foot.R": pitch(12)},
             offset=(0.0, -0.07, -0.03))),
    (0.76, P({"spine": pitch(5), "chest": pitch(3),
              "upperarm.L": pitch(74), "upperarm.R": pitch(74),
              "forearm.L": pitch(18), "forearm.R": pitch(18),
              "hand.L": pitch(-18), "hand.R": pitch(-18)},
             offset=(0.0, -0.03, -0.01))),
    (1.00, P()),
]


# --- taking it --------------------------------------------------------------

HIT = [
    (0.00, P()),
    (0.30, P({"hips": pitch(-6), "spine": pitch(-17), "chest": pitch(-11) @ roll(7),
              "neck": pitch(-14) @ yaw(-8), "head": pitch(-12) @ yaw(-9),
              "shoulder.L": roll(-5), "shoulder.R": roll(5),
              "upperarm.L": pitch(-26) @ roll(-12),
              "upperarm.R": pitch(-22) @ roll(10),
              "forearm.L": pitch(44), "forearm.R": pitch(40),
              "thigh.L": pitch(-8), "shin.L": pitch(-14),
              "thigh.R": pitch(-5), "shin.R": pitch(-12)},
             offset=(0.0, 0.055, -0.024))),
    (1.00, P()),
]

STAGGER = [
    (0.00, P()),
    (0.20, P({"hips": pitch(-10) @ yaw(-6), "spine": pitch(-27) @ yaw(-10),
              "chest": pitch(-18) @ roll(10), "neck": pitch(-22) @ yaw(-12),
              "head": pitch(-20) @ yaw(-14),
              "upperarm.L": pitch(-52) @ roll(-26),
              "upperarm.R": pitch(-44) @ roll(22),
              "forearm.L": pitch(66), "forearm.R": pitch(58),
              "thigh.R": pitch(-26), "shin.R": pitch(-22), "foot.R": pitch(18),
              "thigh.L": pitch(4), "shin.L": pitch(-20)},
             offset=(0.0, 0.115, -0.05))),
    (0.48, P({"hips": pitch(-3) @ yaw(-2), "spine": pitch(-9),
              "chest": pitch(-6) @ roll(4), "neck": pitch(-6),
              "head": pitch(-5),
              "upperarm.L": pitch(-16) @ roll(-8),
              "upperarm.R": pitch(-12) @ roll(6),
              "forearm.L": pitch(40), "forearm.R": pitch(36),
              "thigh.R": pitch(-14), "shin.R": pitch(-24), "foot.R": pitch(10),
              "thigh.L": pitch(10), "shin.L": pitch(-26)},
             offset=(0.0, 0.055, -0.062))),
    (1.00, P()),
]

# Death rotates `root`, whose head sits on the ground between the feet, so the
# body pivots over its own heels instead of sinking through the floor. That is
# also why there is no big vertical offset here: the arc supplies it.
DEATH = [
    (0.00, P()),
    (0.16, P({"hips": pitch(-6), "spine": pitch(-20), "chest": pitch(-12),
              "neck": pitch(-16), "head": pitch(-14),
              "upperarm.L": pitch(-30) @ roll(-14),
              "upperarm.R": pitch(-26) @ roll(12),
              "forearm.L": pitch(52), "forearm.R": pitch(46),
              "thigh.L": pitch(16), "shin.L": pitch(-34),
              "thigh.R": pitch(12), "shin.R": pitch(-30)},
             offset=(0.0, 0.03, -0.09))),
    (0.44, P({"root": pitch(-34),
              "hips": pitch(10), "spine": pitch(-6), "chest": pitch(-4),
              "neck": pitch(-10), "head": pitch(-16),
              "upperarm.L": pitch(-46) @ roll(-30),
              "upperarm.R": pitch(-40) @ roll(26),
              "forearm.L": pitch(60), "forearm.R": pitch(54),
              "thigh.L": pitch(34), "shin.L": pitch(-62),
              "thigh.R": pitch(26), "shin.R": pitch(-54),
              "foot.L": pitch(14), "foot.R": pitch(12)},
             offset=(0.0, 0.02, -0.05))),
    (0.74, P({"root": pitch(-84),
              "hips": pitch(16) @ roll(6), "spine": pitch(-4) @ yaw(4),
              "chest": pitch(-6), "neck": pitch(-6), "head": pitch(-18) @ yaw(6),
              "upperarm.L": pitch(-64) @ roll(-44),
              "upperarm.R": pitch(-58) @ roll(38),
              "forearm.L": pitch(34), "forearm.R": pitch(28),
              "thigh.L": pitch(28) @ roll(8), "shin.L": pitch(-44),
              "thigh.R": pitch(20) @ roll(-6), "shin.R": pitch(-36),
              "foot.L": pitch(6), "foot.R": pitch(4)})),
    (0.88, P({"root": pitch(-95),
              "hips": pitch(12) @ roll(7), "spine": pitch(-2) @ yaw(5),
              "chest": pitch(-4), "neck": pitch(-2), "head": pitch(-20) @ yaw(8),
              "upperarm.L": pitch(-72) @ roll(-50),
              "upperarm.R": pitch(-66) @ roll(44),
              "forearm.L": pitch(24), "forearm.R": pitch(18),
              "thigh.L": pitch(18) @ roll(10), "shin.L": pitch(-30),
              "thigh.R": pitch(12) @ roll(-8), "shin.R": pitch(-24)})),
    (1.00, P({"root": pitch(-92),
              "hips": pitch(13) @ roll(7), "spine": pitch(-3) @ yaw(5),
              "chest": pitch(-5), "neck": pitch(-3), "head": pitch(-19) @ yaw(8),
              "upperarm.L": pitch(-70) @ roll(-48),
              "upperarm.R": pitch(-64) @ roll(42),
              "forearm.L": pitch(26), "forearm.R": pitch(20),
              "thigh.L": pitch(20) @ roll(9), "shin.L": pitch(-32),
              "thigh.R": pitch(14) @ roll(-7), "shin.R": pitch(-26)})),
]


# name, duration (seconds), loops, keys
CLIPS = [
    ("idle", 4.20, True, IDLE),
    ("idle_combat", 2.30, True, IDLE_COMBAT),
    ("dormant", 3.80, True, DORMANT),
    ("talk", 3.20, True, TALK),
    ("walk_f", 1.00, True, WALK_F),
    ("walk_b", 1.08, True, WALK_B),
    ("walk_l", 0.92, True, WALK_L),
    ("walk_r", 0.92, True, [(phase, pose.mirrored()) for phase, pose in WALK_L]),
    ("run_f", 0.68, True, RUN_F),
    ("run_b", 0.76, True, RUN_B),
    ("jump", 0.44, False, JUMP),
    ("fall", 0.80, True, FALL),
    ("land", 0.52, False, LAND),
    ("attack_1", 0.66, False, ATTACK_1),
    ("attack_2", 0.62, False, ATTACK_2),
    ("attack_heavy", 1.05, False, ATTACK_HEAVY),
    ("cast", 0.82, False, CAST),
    ("hit", 0.34, False, HIT),
    ("stagger", 0.78, False, STAGGER),
    ("death", 1.40, False, DEATH),
]


# --- building it in Blender -------------------------------------------------

FPS = 30


def log(message: str) -> None:
    print("[humanoid-rig] " + message)


def prepare_mesh():
    """Isolate the subject, freeze its transform, bake material colour down."""
    subject = bpy.data.objects.get(MESH_NAME)
    if subject is None:
        candidates = [o for o in bpy.data.objects if o.type == "MESH"]
        if not candidates:
            raise SystemExit("no mesh in " + SOURCE_BLEND)
        subject = max(candidates, key=lambda o: len(o.data.vertices))
        log("mesh '%s' not found, using '%s'" % (MESH_NAME, subject.name))

    for other in list(bpy.data.objects):
        if other is not subject:
            bpy.data.objects.remove(other, do_unlink=True)

    bpy.context.view_layer.objects.active = subject
    subject.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    if YAW_FLIP:
        # Turned to face the engine's forward, and BAKED -- an unapplied
        # rotation would be dropped by the exporter's rest-position armature
        # export and the model would silently face the old way again.
        subject.matrix_world = Matrix.Rotation(math.pi, 4, "Z") @ subject.matrix_world
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.ops.object.shade_flat()

    # The mannequin's colour lives in its materials, exactly as it did for the
    # OBJ bake (tools/blend_to_obj.py). Writing it to COLOR_0 keeps the drawn
    # result identical to the prop the kit already ships, with no texture and no
    # atlas -- and lets a tinted enemy material multiply over it.
    mesh = subject.data
    for attribute in list(mesh.color_attributes):
        mesh.color_attributes.remove(attribute)
    colours = mesh.color_attributes.new(name="Col", type="BYTE_COLOR",
                                        domain="CORNER")
    slot_colour = []
    for slot in mesh.materials:
        base = (0.6038, 0.6038, 0.6038, 1.0)
        if slot is not None:
            if slot.use_nodes:
                for node in slot.node_tree.nodes:
                    if node.type == "BSDF_PRINCIPLED":
                        base = tuple(node.inputs["Base Color"].default_value)
                        break
            else:
                base = tuple(slot.diffuse_color)
        slot_colour.append(base)
    if not slot_colour:
        slot_colour = [(0.6038, 0.6038, 0.6038, 1.0)]
    for polygon in mesh.polygons:
        base = slot_colour[min(polygon.material_index, len(slot_colour) - 1)]
        for loop_index in polygon.loop_indices:
            colours.data[loop_index].color = base
    mesh.color_attributes.active_color = colours
    if hasattr(mesh.color_attributes, "render_color_index"):
        mesh.color_attributes.render_color_index = 0

    # One material, so the GLB has a single primitive: the engine's skinned
    # attach names one material anyway, and a split here would draw half a
    # humanoid in each of two colours.
    mesh.materials.clear()
    material = bpy.data.materials.new("HumanoidBase")
    material.use_nodes = True
    mesh.materials.append(material)

    log("mesh '%s': %d verts, %d polys" %
        (subject.name, len(mesh.vertices), len(mesh.polygons)))
    return subject


def build_armature(subject):
    armature_data = bpy.data.armatures.new("HumanoidRig")
    armature = bpy.data.objects.new("HumanoidRig", armature_data)
    bpy.context.collection.objects.link(armature)
    bpy.context.view_layer.objects.active = armature
    bpy.ops.object.mode_set(mode="EDIT")

    created = {}
    for name, head, tail, parent, connected in BONE_TABLE:
        bone = armature_data.edit_bones.new(name)
        bone.head = head
        bone.tail = tail
        bone.roll = 0.0
        bone.use_deform = name != "root"
        if parent is not None:
            bone.parent = created[parent]
            bone.use_connect = connected
        created[name] = bone
    bpy.ops.object.mode_set(mode="OBJECT")
    log("armature: %d bones" % len(armature_data.bones))
    return armature


def skin(subject, armature):
    """Bone-heat weights, then a check that no vertex was left behind.

    Heat weighting is what makes a knee bend read as a knee on a 203-vertex
    mesh; a distance falloff pulls the other thigh in at the crotch. It can also
    fail outright on some topology, which is silent -- the model just draws in
    its rest pose forever -- so the result is verified rather than assumed.
    """
    bpy.ops.object.select_all(action="DESELECT")
    subject.select_set(True)
    armature.select_set(True)
    bpy.context.view_layer.objects.active = armature
    try:
        bpy.ops.object.parent_set(type="ARMATURE_AUTO")
    except RuntimeError as error:
        log("automatic weights failed (%s); falling back to envelopes" % error)
        bpy.ops.object.parent_set(type="ARMATURE_ENVELOPE")

    deform = {name for name, _, _, _, _ in BONE_TABLE if name != "root"}
    unweighted = 0
    for vertex in subject.data.vertices:
        total = sum(group.weight for group in vertex.groups
                    if subject.vertex_groups[group.group].name in deform)
        if total <= 1e-4:
            unweighted += 1
    if unweighted:
        log("WARNING: %d vertices carry no bone weight" % unweighted)
    else:
        log("skinning: every vertex weighted")


def pose_bone_basis(bone, delta: Quaternion) -> Quaternion:
    """A character-space delta, expressed in the bone's own rest basis.

    R_basis = M^-1 . D . M, where M is the bone's rest rotation in armature
    space. Doing it this way means bone roll never enters the clip tables: a
    number in a pose means the same thing on every bone.
    """
    rest = bone.matrix_local.to_quaternion()
    return rest.inverted() @ delta @ rest


def apply_pose(armature, pose: Pose) -> None:
    for pose_bone in armature.pose.bones:
        pose_bone.rotation_mode = "QUATERNION"
        delta = pose.rotations.get(pose_bone.name, IDENTITY)
        pose_bone.rotation_quaternion = pose_bone_basis(pose_bone.bone, delta)
        pose_bone.location = Vector((0.0, 0.0, 0.0))
    root = armature.pose.bones.get("root")
    if root is not None and pose.offset.length > 1e-9:
        rest = root.bone.matrix_local.to_quaternion()
        root.location = rest.inverted() @ pose.offset


def action_fcurves(action):
    """Every curve in an action, across both action APIs.

    Blender 4.4 moved an action's curves behind layers/strips/channelbags and
    dropped `Action.fcurves`. Reaching through both shapes is three lines; the
    alternative is a script that only runs on the Blender the author happened to
    have installed.
    """
    if hasattr(action, "fcurves"):
        return list(action.fcurves)
    curves = []
    for layer in getattr(action, "layers", []):
        for strip in getattr(layer, "strips", []):
            for bag in getattr(strip, "channelbags", []):
                curves.extend(bag.fcurves)
    return curves


def bake_clips(armature) -> None:
    bpy.context.scene.render.fps = FPS
    armature.animation_data_create()
    for name, duration, loop, keys in CLIPS:
        action = bpy.data.actions.new(name)
        action.use_fake_user = True
        armature.animation_data.action = action
        frames = max(2, int(round(duration * FPS)))
        for step in range(frames + 1):
            phase = step / frames
            apply_pose(armature, sample_keys(keys, phase, loop))
            for pose_bone in armature.pose.bones:
                pose_bone.keyframe_insert("rotation_quaternion", frame=step + 1)
                if pose_bone.name == "root":
                    pose_bone.keyframe_insert("location", frame=step + 1)
        # Densely sampled curves already carry their easing; Bezier handles on
        # top of that overshoot, which on a leg reads as the foot punching
        # through the floor between keys.
        for curve in action_fcurves(action):
            for point in curve.keyframe_points:
                point.interpolation = "LINEAR"
        log("clip %-13s %5.2fs %3d frames %s" %
            (name, duration, frames + 1, "loop" if loop else "once"))
    armature.animation_data.action = None


def export_glb(path: str) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    wanted = {
        "filepath": path,
        "export_format": "GLB",
        "use_selection": False,
        "export_apply": False,
        "export_yup": True,
        "export_skins": True,
        "export_animations": True,
        "export_animation_mode": "ACTIONS",
        "export_bake_animation": False,
        "export_optimize_animation_size": False,
        "export_optimize_animation_keep_anim_armature": True,
        "export_force_sampling": True,
        "export_frame_range": False,
        "export_rest_position_armature": True,
        "export_leaf_bone": False,
        "export_def_bones": False,
        "export_hierarchy_flatten_bones": False,
        "export_influence_nb": 4,
        "export_all_influences": False,
        "export_vertex_color": "ACTIVE",
        "export_materials": "EXPORT",
        "export_cameras": False,
        "export_lights": False,
        "export_extras": False,
    }
    # Blender renames exporter properties between releases, and an unknown
    # keyword is a hard error rather than a warning. Ask the operator what it
    # actually takes.
    accepted = set(bpy.ops.export_scene.gltf.get_rna_type().properties.keys())
    kwargs = {key: value for key, value in wanted.items() if key in accepted}
    dropped = sorted(set(wanted) - set(kwargs))
    if dropped:
        log("exporter does not take: " + ", ".join(dropped))
    bpy.ops.export_scene.gltf(**kwargs)
    log("wrote %s (%.1f KiB)" % (path, os.path.getsize(path) / 1024.0))


def render_preview(armature, directory: str, clips=None, phases=None,
                   view: str = "side") -> None:
    """Strips of poses per clip, from the angle that shows the error.

    A walk cycle reads from the side and a strafe reads from the front; the
    three-quarter view that looks best hides both. So the view is a choice.
    """
    os.makedirs(directory, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_WORKBENCH"
    scene.render.resolution_x = 260
    scene.render.resolution_y = 360
    scene.render.film_transparent = False

    # Blender space: the character faces -Y and its left is +X.
    views = {
        "side": (Vector((6.0, 0.0, 0.95)), (math.radians(90.0), 0.0,
                                            math.radians(90.0))),
        "front": (Vector((0.0, -6.0, 0.95)), (math.radians(90.0), 0.0, 0.0)),
        "three_quarter": (Vector((3.6, -4.6, 1.7)),
                          (math.radians(78.0), 0.0, math.radians(38.0))),
    }
    location, rotation = views[view]
    camera_data = bpy.data.cameras.new("preview")
    camera_data.type = "ORTHO"
    camera_data.ortho_scale = 2.4
    camera = bpy.data.objects.new("preview", camera_data)
    bpy.context.collection.objects.link(camera)
    camera.location = location
    camera.rotation_euler = rotation
    scene.camera = camera

    selected = [row for row in CLIPS if not clips or row[0] in clips]
    steps = phases or [0.0, 0.25, 0.5, 0.75]
    for name, _duration, loop, keys in selected:
        for index, phase in enumerate(steps):
            apply_pose(armature, sample_keys(keys, phase, loop))
            bpy.context.view_layer.update()
            scene.render.filepath = os.path.join(
                directory, "%s_%s_%d.png" % (name, view, index))
            bpy.ops.render.render(write_still=True)
    log("preview frames in " + directory)


def main() -> None:
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    out_dir = DEFAULT_OUT
    preview_dir = None
    preview_clips = None
    preview_view = "side"
    preview_phases = None
    skip_export = False
    index = 0
    while index < len(argv):
        if argv[index] == "--out" and index + 1 < len(argv):
            out_dir = argv[index + 1]
            index += 2
        elif argv[index] == "--preview" and index + 1 < len(argv):
            preview_dir = argv[index + 1]
            index += 2
        elif argv[index] == "--preview-clips" and index + 1 < len(argv):
            preview_clips = set(argv[index + 1].split(","))
            index += 2
        elif argv[index] == "--preview-view" and index + 1 < len(argv):
            preview_view = argv[index + 1]
            index += 2
        elif argv[index] == "--preview-phases" and index + 1 < len(argv):
            preview_phases = [float(v) for v in argv[index + 1].split(",")]
            index += 2
        elif argv[index] == "--no-export":
            skip_export = True
            index += 1
        else:
            index += 1

    subject = prepare_mesh()
    armature = build_armature(subject)
    skin(subject, armature)
    bake_clips(armature)
    if not skip_export:
        export_glb(os.path.join(out_dir, "humanoid_rig.glb"))
    if preview_dir:
        render_preview(armature, preview_dir, preview_clips, preview_phases,
                       preview_view)


if __name__ == "__main__":
    main()
