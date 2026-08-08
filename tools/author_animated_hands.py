#!/usr/bin/env python3
"""Animated FPS hand+weapon packs -> PSX viewmodel rigs the engine can wear.

WHAT THESE PACKS ARE, AND WHY THEY ARE WORTH THE TROUBLE

`assets/source/animations/` holds five packs of first-person hands ALREADY
HOLDING a weapon, each with the clips a shooter actually needs -- idle, shoot,
reload, draw, walk, run, inspect -- authored by somebody who animates for a
living. The STALKER pack goes further and animates the weapon's own parts: it
has bones for the bolt, the magazine, the trigger and the charging handle, and
separate fast and tactical reloads.

Nothing procedural gets near that. `tools/author_hand_rigs.py` writes clips from
a pose table because there was no animator and no motion capture; these ARE the
animator. A reload where the left hand actually finds the magazine well is not
something a pose table produces.

THE THREE PROBLEMS, AND WHY EACH IS SOLVED HERE RATHER THAN AT THE CALL SITE

  1. POLYCOUNT. The rifle pack is 20,420 vertices. The engine's own arms rig is
     906. Dropping a modern-detail viewmodel into a game whose look is
     fifth-generation console is not a compromise, it is a different game in the
     corner of the screen -- and CLAUDE.md says the image is frozen. So the mesh
     is decimated to a PSX budget, which the skinning survives because Blender's
     Decimate keeps vertex groups.

  2. SCALE AND FACING. The packs are authored in centimetres (a 26.8-unit
     forearm is 26.8 cm), and their weapon points down Blender -Y, which the
     glTF axis conversion turns into the engine's +Z -- backwards, since a node's
     forward is its local -Z. Both are baked here, so the rig is correct on any
     node rather than correct only under one compensating transform.

  3. MATERIALS. `Renderer::attachSkinnedMesh` takes ONE material for the whole
     mesh, and these packs use three or four -- hands, sleeves, weapon, glass.
     The albedos are atlased into a horizontal strip and the UVs remapped per
     material slot, exactly as tools/author_hand_rigs.py does, so one texture
     fetch serves the lot.

OUTPUT, per pack -- the same shape the procedural rigs produce, so the game sees
one kind of hands rig and not two:

  assets/meshes/viewmodels/<id>.glb            + .meta (gltf2ozz owns it)
  assets/textures/viewmodels/<id>_albedo.png   the atlas
  assets/animations/viewmodels/<id>/*.ozz      skeleton + clips
  assets/materials/viewmodel_hands.mat         appended
  assets/config/viewmodel_hands.toml           appended

Usage:
  tools/author_animated_hands.py [--pack ID ...] [--dry-run] [--skip-cook]
                                 [--budget N]

Requires the `blender` CLI; cooking additionally requires gltf2ozz.
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
SOURCE = os.path.join(ASSETS, "source", "animations")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# --- the packs ----------------------------------------------------------------
#
# `clips` maps the ENGINE's clip vocabulary onto whatever the pack's animator
# called them. The engine side is fixed -- a weapon definition asks for "fire",
# "reload", "draw" -- so this table is the whole of the per-pack difference and
# adding a sixth pack is a row here.
#
# A missing entry is not an error: a pack with no walk cycle simply has no walk
# clip, and the viewmodel falls back to idle. Stating them explicitly rather
# than pattern-matching, because "Take" meaning "draw" and "Shot" meaning "fire"
# are things only a person reading the file can know.

PACKS = [
    {
        "id": "hands_rifle",
        "name": "Rifle Hands",
        "source": "animated_fps_hands_rifle_animation_pack.glb",
        "textures": "animated-fps-hands-rifle-animation-pack/textures",
        # Centimetres: the forearm bone measures 26.8 units.
        "scale": 0.01,
        "clips": {
            "idle": "Arms_FPS_Anim_Idle",
            "fire": "Arms_FPS_Anim_Shoot",
            "reload": "Arms_FPS_Anim_Reload_Fast",
            "draw": "Arms_FPS_Anim_Draw",
            "walk": "Arms_FPS_Anim_Walk",
            "run": "Arms_FPS_Anim_Run",
            "inspect": "Arms_FPS_Anim_rifle_inspect",
        },
    },
    {
        "id": "hands_aks74",
        "name": "AKS-74 Hands",
        "source": "stalker-aks-74-reanimation/source/hands_aks74_stalker_soc.fbx",
        "textures": "stalker-aks-74-reanimation/textures",
        "scale": 0.01,
        # The richest of the set: the weapon's bolt, magazine and trigger are
        # animated, and the reload comes in fast and tactical variants -- which
        # is exactly the distinction WeaponAmmoDef already draws.
        "clips": {
            "idle": "Walk",
            "fire": "Shot",
            "reload": "Reload_f",
            "reload_tactical": "Reload_t",
            "walk": "Walk",
            "inspect": "Inspect",
        },
    },
    {
        "id": "hands_scar",
        "name": "SCAR Hands",
        "source": "animated_scar/source/SCAR.fbx",
        "textures": "animated_scar/textures",
        "scale": 0.01,
        "clips": {
            "idle": "Idle",
            "fire": "Shoot",
            "reload": "Reload",
            "draw": "Take",
            "holster": "Hide",
            "walk": "Walk",
            "run": "Run",
        },
    },
    {
        "id": "hands_shotgun",
        "name": "Shotgun Hands",
        "source": "animated-shotgun/source/AnimatedShotgun.fbx",
        "textures": "animated-shotgun/textures",
        "scale": 0.01,
        # This pack has no idle: the animator treated the rest pose as one. It
        # also splits its reload into start/loop/end, which is exactly the shape
        # WeaponAmmoDef::shellByShell describes -- pump the first shell, feed one
        # at a time, close the action -- so the three map straight onto it.
        "clips": {
            "fire": "Shoot",
            "reload": "Reload",
            "reload_start": "ReloadStart",
            "reload_end": "ReloadEnd",
            "draw": "Take",
            "holster": "Hide",
            "inspect": "Watch",
        },
    },
    {
        "id": "hands_pp19",
        "name": "PP-19 Hands",
        "source": "animated-pp-19-01/source/3.fbx",
        "textures": "animated-pp-19-01/textures",
        "scale": 0.01,
        "clips": {
            "idle": "Idle",
            "fire": "Shoot",
            "reload": "Reload",
            "draw": "Take",
        },
    },
]

# Vertices to keep. The engine's own arms rig is 906 and the kit props run
# 200-2500, so 1800 sits a viewmodel at the top of the range the rest of the
# game occupies -- it is the thing closest to the camera and carries the most
# silhouette, which is where the budget should go.
DEFAULT_BUDGET = 1800

# Atlas cell size, in pixels. These packs ship 4096px albedos -- ten of them on
# the rifle rig, which atlased to 40960x4096 and a 135 MB PNG. That is not a
# size problem to solve later: it is the wrong ART, four times the resolution of
# anything else in the game, and it would read as a photograph glued to the
# corner of a PSX screen.
#
# 256 is what the engine's own hand atlases use, so a rig imported here sits at
# exactly the resolution of the rig beside it.
ATLAS_CELL = 256


def pack_by_id(ident: str) -> dict:
    for pack in PACKS:
        if pack["id"] == ident:
            return pack
    raise SystemExit("no such pack: " + ident)


# --- textures -----------------------------------------------------------------

def albedo_textures(pack: dict) -> list:
    """The pack's base-colour PNGs, in a stable order.

    Only albedo: the engine's PSX surface shader samples one texture and has no
    normal or roughness input, so copying those would be publishing files
    nothing can read.
    """
    directory = os.path.join(SOURCE, pack["textures"])
    if not os.path.isdir(directory):
        return []
    out = []
    for name in sorted(os.listdir(directory)):
        low = name.lower()
        if not low.endswith((".png", ".jpeg", ".jpg")):
            continue
        if any(tag in low for tag in ("_normal", "_metallic", "_roughness",
                                      "_ao", "_height", "_opengl", "_n.tga",
                                      "_orm")):
            continue
        out.append(os.path.join(directory, name))
    return out


def to_png(paths: list, scratch: str) -> list:
    """Re-encode any non-PNG texture to PNG, through Blender.

    These packs ship JPEG albedos and `pngkit` reads PNG only -- deliberately,
    because it exists to move pixel art around without a colour-managed round
    trip that can shift a value. Blender is already a dependency here and its
    `Image.save()` re-encodes without applying a view transform, so a JPEG
    arrives as the same pixels in a container pngkit can read.

    Returns the list with every path pointing at something readable, in order.
    """
    pending = [p for p in paths if not p.lower().endswith(".png")]
    if not pending:
        return paths
    os.makedirs(scratch, exist_ok=True)
    mapping = {p: os.path.join(scratch,
                               os.path.splitext(os.path.basename(p))[0] + ".png")
               for p in pending}
    script = (
        "import bpy, json, os\n"
        "for src, dst in json.loads(os.environ['RAVEN_PNG_JOBS']).items():\n"
        "    image = bpy.data.images.load(src)\n"
        # Blender loads an image lazily, and save() on one whose pixels have
        # never been touched fails with "does not have any image data". Reading
        # the size is what forces the decode.
        "    _ = image.size[0]\n"
        # `save(filepath=...)` keeps the SOURCE encoding whatever file_format
        # says -- it wrote JPEG bytes under a .png name, which pngkit then
        # rightly refused. filepath_raw + save() is the idiom that re-encodes.
        "    image.file_format = 'PNG'\n"
        "    image.filepath_raw = dst\n"
        "    image.save()\n")
    env = dict(os.environ, RAVEN_PNG_JOBS=json.dumps(mapping))
    subprocess.run(["blender", "-b", "--factory-startup", "--python-expr",
                    script], env=env, capture_output=True, text=True)
    return [mapping.get(p, p) for p in paths]


# --- the Blender phase --------------------------------------------------------

BLENDER_SCRIPT = r'''
import bpy, json, math, os, sys
from mathutils import Matrix, Vector

job = json.loads(os.environ["RAVEN_ANIMHANDS_JOB"])
report = {"packs": [], "errors": []}


def patch_fbx_light_import():
    """Work around a Blender 5.2 bug that makes ANY FBX with a light unloadable.

    `io_scene_fbx.blen_read_light` sets `lamp.cycles.cast_shadow`, which Cycles
    5.2 no longer has, and the AttributeError aborts the whole import. Two of
    these five packs ship the animator's light rig, so without this they simply
    cannot be read.

    The lights are deleted by strip_junk() a moment later anyway, so swallowing
    the error and handing back a default lamp loses nothing. Scoped to this
    function so that if a future Blender fixes the bug, this quietly stops
    doing anything rather than masking the new behaviour.
    """
    try:
        from io_scene_fbx import import_fbx
    except ImportError:
        return
    original = import_fbx.blen_read_light

    def guarded(fbx_tmpl, fbx_obj, global_scale):
        try:
            return original(fbx_tmpl, fbx_obj, global_scale)
        except AttributeError:
            return bpy.data.lights.new(name="fbx_light", type="POINT")

    import_fbx.blen_read_light = guarded


patch_fbx_light_import()


def load(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    if path.lower().endswith((".glb", ".gltf")):
        bpy.ops.import_scene.gltf(filepath=path)
    else:
        bpy.ops.import_scene.fbx(filepath=path, use_image_search=False,
                                 ignore_leaf_bones=False)


def strip_junk():
    """Remove what is in the file but not part of the rig.

    These are working files, not exports: they carry the animator's camera, the
    default cube, and the wire shapes an IK control is displayed as. All of them
    are meshes, none of them is the subject, and joining them into the viewmodel
    would weld a camera-shaped lump to the player's hands.
    """
    removed = []
    for obj in list(bpy.data.objects):
        if obj.type in ("CAMERA", "LIGHT", "EMPTY"):
            removed.append(obj.name)
            bpy.data.objects.remove(obj, do_unlink=True)
            continue
        if obj.type != "MESH":
            continue
        name = obj.name.lower()
        # An unskinned mesh in a rigged file is scenery or a control shape: the
        # subject is by definition the geometry the armature deforms.
        skinned = any(m.type == "ARMATURE" for m in obj.modifiers) or \
            (obj.parent and obj.parent.type == "ARMATURE")
        control = name.startswith(("icosphere", "shape_", "circle", "plane",
                                   "cube")) and len(obj.data.vertices) < 300
        if control or not skinned:
            removed.append(obj.name)
            bpy.data.objects.remove(obj, do_unlink=True)
    return removed


def atlas_uvs(mesh_obj, cells):
    """Remap each material slot into its own cell of the horizontal atlas."""
    if cells <= 1:
        return
    uv = mesh_obj.data.uv_layers.active
    if not uv:
        return
    slots = max(1, len(mesh_obj.material_slots))
    for poly in mesh_obj.data.polygons:
        cell = min(poly.material_index, cells - 1) if slots > 1 else 0
        for loop_index in poly.loop_indices:
            u, v = uv.data[loop_index].uv
            uv.data[loop_index].uv = ((u + cell) / cells, v)


def decimate(mesh_obj, budget):
    """Cut the mesh to a PSX vertex budget, keeping the skin weights.

    Blender's Decimate carries vertex groups through, so the collapsed mesh is
    still bound to the same skeleton and the same clips drive it. Collapse
    rather than un-subdivide: these are already triangulated production meshes,
    and un-subdivide needs a subdivision history they do not have.
    """
    before = len(mesh_obj.data.vertices)
    if before <= budget:
        return before, before
    bpy.context.view_layer.objects.active = mesh_obj

    # Decimate's `ratio` is over FACES, and collapsing a face does not remove a
    # predictable number of vertices -- a single pass keyed on the vertex ratio
    # lands roughly twice over budget. So it runs until the budget is met, with
    # a pass cap: each pass is cheap and four of them reach 6% of the original,
    # which is below anything this is asked for.
    for _ in range(4):
        current = len(mesh_obj.data.vertices)
        if current <= budget:
            break
        modifier = mesh_obj.modifiers.new("psx_decimate", "DECIMATE")
        modifier.decimate_type = "COLLAPSE"
        modifier.ratio = max(0.05, float(budget) / float(current))
        # Keeps UV seams and material boundaries from being collapsed across,
        # which is what stops a decimated hand sampling the weapon's atlas cell
        # along the seam.
        modifier.use_collapse_triangulate = True
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    return before, len(mesh_obj.data.vertices)


for entry in job["packs"]:
    try:
        load(entry["source"])
        removed = strip_junk()

        meshes = [o for o in bpy.data.objects if o.type == "MESH"]
        armatures = [o for o in bpy.data.objects if o.type == "ARMATURE"]
        if not meshes or not armatures:
            report["errors"].append(
                "%s: no skinned mesh survived the cleanup" % entry["id"])
            continue
        rig = armatures[0]

        # One mesh, because the skinned draw takes one mesh and one material.
        bpy.ops.object.select_all(action="DESELECT")
        for mesh in meshes:
            mesh.select_set(True)
        target = max(meshes, key=lambda o: len(o.data.vertices))
        bpy.context.view_layer.objects.active = target
        if len(meshes) > 1:
            bpy.ops.object.join()
        mesh = bpy.context.view_layer.objects.active

        atlas_uvs(mesh, entry["cells"])
        before, after = decimate(mesh, entry["budget"])

        # Scale to metres and turn the rig to face the engine's forward.
        #
        # The pack points its weapon down Blender -Y. glTF export maps Blender
        # (x, y, z) onto engine (x, z, -y), so -Y arrives as +Z -- behind the
        # camera, because a node's forward is its local -Z. A half turn about up
        # fixes it once, here, rather than in every scene that places a rig.
        # SCALE, ANCHOR and FACING, measured off the MESH.
        #
        # The bones were the obvious source and they cannot be trusted here: two
        # of these packs have a mesh bound at a wholly different scale from the
        # skeleton it is bound to, so a rig whose upper arm measured 0.99 units
        # imported fifteen metres across. Whatever relationship those files have
        # between armature space and mesh space, it is not one this tool can
        # read off a bone length.
        #
        # The mesh cannot lie about it: it IS what appears on screen. So the
        # rig is scaled until the mesh is the width of a pair of hands holding a
        # weapon, and anchored so that mesh hangs below and in front of the eye
        # -- which is the composition, stated directly instead of inferred.
        #
        # This is coarse on purpose. It gets every rig into frame at a sane
        # size; the last centimetres are a judgement call, and the in-game
        # Viewmodel panel (F1) has live sliders and a "Copy rig framing" button
        # for exactly that.
        world = [mesh.matrix_world @ v.co for v in mesh.data.vertices]
        if not world:
            report["errors"].append("%s: mesh has no vertices" % entry["id"])
            continue
        lo = Vector((min(v.x for v in world), min(v.y for v in world),
                     min(v.z for v in world)))
        hi = Vector((max(v.x for v in world), max(v.y for v in world),
                     max(v.z for v in world)))

        # 0.58 m across. The first calibration used 0.95 -- the arm span of the
        # engine's own rig -- and that is the wrong measurement to match: these
        # packs' meshes include the WEAPON, so matching total width to an
        # arm span made the gun fill two thirds of the screen.
        width = max(hi.x - lo.x, 1e-6)
        s = 0.58 / width
        measured = width

        # THE EYE, which the viewmodel node is: centred across the mesh, at its
        # TOP and its BACK, so the hands and the weapon hang below and reach
        # forward from it.
        #
        # Taken from the mesh rather than a camera bone because only three of
        # the five packs have one, and the two without offer `Root` -- which
        # sits at the character's base and put the hands 1.4 m above the camera.
        # Blender +z is up; +y is what the glTF conversion makes the engine's
        # forward, so the back of the mesh is its minimum y.
        anchor_point = Vector(((lo.x + hi.x) * 0.5, lo.y, hi.z))

        # Placement is baked into the DATA, and that is a compromise.
        #
        # Two approaches, neither clean:
        #
        #   OBJECT transforms (obj.scale / obj.location) leave the skinning
        #   untouched, because they apply after the pose is evaluated. But the
        #   node TRS they become does not survive the ozz cook -- the skeleton
        #   root does not carry it -- so the rig arrives at its original size
        #   and is nowhere near the camera. Tried; the viewmodel vanished.
        #
        #   DATA transforms (mesh.data / rig.data) do survive, because they are
        #   in the vertices and the rest pose. But the animation F-curves still
        #   hold bone translations in the ORIGINAL space, so a clip poses bones
        #   somewhere their vertices were never weighted for. The bind pose
        #   measures correctly -- which is why this survived several rounds of
        #   checking bounds -- and the mesh pulls apart once a clip plays.
        #
        # Data wins for now because a rig in the wrong place is fixable by
        # eye and a rig that is not there at all is not. THE TEARING IS REAL AND
        # UNFIXED: the honest repair is to scale the location F-curves by the
        # same factor as the rest pose, which is the remaining work here.
        scale_matrix = Matrix.Scale(s, 4)
        mesh.data.transform(scale_matrix @ mesh.matrix_world)
        rig.data.transform(scale_matrix @ rig.matrix_world)
        mesh.matrix_world = Matrix.Identity(4)
        rig.matrix_world = Matrix.Identity(4)

        recentre = Matrix.Translation(-(anchor_point * s))
        mesh.data.transform(recentre)
        rig.data.transform(recentre)

        # The location curves, scaled to match the rest pose they now drive.
        #
        # This is the half that was missing. A pose bone's `location` is in its
        # own rest space, so a rest skeleton scaled by `s` needs its authored
        # translations scaled by `s` too -- otherwise a reload that slides the
        # magazine 8 cm slides it 8 metres.
        # Blender 4.4+ moved f-curves into slotted actions
        # (action.layers[].strips[].channelbags[].fcurves); `action.fcurves` is
        # gone in 5.2. Both layouts are walked so this keeps working either way.
        def action_curves(action):
            legacy = getattr(action, "fcurves", None)
            if legacy is not None:
                return list(legacy)
            found = []
            for layer in getattr(action, "layers", []):
                for strip in getattr(layer, "strips", []):
                    for bag in getattr(strip, "channelbags", []):
                        found.extend(bag.fcurves)
            return found

        for action in bpy.data.actions:
            for curve in action_curves(action):
                if not curve.data_path.endswith(".location"):
                    continue
                for key in curve.keyframe_points:
                    key.co[1] *= s
                    key.handle_left[1] *= s
                    key.handle_right[1] *= s

        # THE FACING, from where the MESH ended up.
        #
        # A flat 180-degree yaw used to be applied to every pack, on the
        # reasoning that they face glTF +z. Some do and some do not: one rig
        # came out with its hands 28 cm BEHIND the eye, aimed at the back of the
        # player's head.
        #
        # Decided on the mesh rather than the hand bones for the same reason the
        # scale is: in two of these packs the bones and the mesh disagree about
        # which way is forward, and the mesh is the half that gets drawn.
        #
        # Blender +y is what the glTF conversion turns into the engine's -z,
        # which is forward. A viewmodel reaches forward from the eye, so a mesh
        # whose bulk sits at negative y is backwards and gets turned.
        count = len(mesh.data.vertices)
        centroid = (sum(v.co.y for v in mesh.data.vertices) / float(count)) \
            if count else 0.0
        flipped = centroid < 0.0
        if flipped:
            turn = Matrix.Rotation(math.radians(180.0), 4, "Z")
            mesh.data.transform(turn)
            rig.data.transform(turn)

        # Clip names. The packs prefix them with the exporting object
        # ("Armature|Arms_FPS_Anim_Idle", "Camera.002|Akito Rig|Reload"), and
        # gltf2ozz uses the action name as the clip's filename -- so an
        # unstripped name becomes a file with a pipe in it that no weapon
        # definition can reference.
        # These files export the SAME clip several times, once per object that
        # carried a channel -- "Armature|Armature|Reload", "Camera.001|Armature|
        # Reload", "Camera|hud_mesh|Reload_f". Stripping the prefixes collapses
        # them onto one name, and Blender then disambiguates with .001, .002,
        # which is how the first run produced `fire.001` through `fire.005`.
        #
        # So the duplicates are dropped rather than renamed. The one kept is the
        # first in armature order, because the armature's own action is the one
        # that actually drives the skeleton -- a camera's copy animates a camera
        # this game does not use.
        discovered = []
        wanted = {v: k for k, v in entry["clips"].items()}
        rig_name = rig.name.lower()

        def rank(action):
            parts = action.name.split("|")
            # Prefer an action whose own prefix names the armature.
            owned = 0 if any(rig_name in p.lower() for p in parts[:-1]) else 1
            return (owned, len(parts), action.name)

        seen = {}
        for action in sorted(bpy.data.actions, key=rank):
            tail = action.name.split("|")[-1]
            discovered.append(tail)
            engine_name = wanted.get(tail)
            if engine_name is None:
                # Not in the map: keep it under its own cleaned name rather than
                # dropping it, so --dry-run's list is the whole truth and a clip
                # nobody mapped is still cookable.
                engine_name = tail.lower().replace(" ", "_").replace(".", "_")
            if engine_name in seen:
                bpy.data.actions.remove(action)
                continue
            seen[engine_name] = True
            action.name = engine_name
            action.use_fake_user = True

        # A camera action that survived the pass above animates nothing this
        # game draws, and gltf2ozz would cook it into a clip a weapon could
        # accidentally name.
        for action in list(bpy.data.actions):
            if action.name.startswith("cameraaction"):
                bpy.data.actions.remove(action)

        # Put every clip in its own NLA track, and export tracks rather than
        # actions.
        #
        # The FBX packs arrive with all their clips in bpy.data.actions but only
        # one assigned to the armature, and the glTF exporter's ACTIONS mode
        # then wrote exactly that one -- the AKS-74 rig cooked a single clip
        # named after its skeleton while the GLB-sourced rig cooked all eight.
        # A track per action is unambiguous: the exporter writes one glTF
        # animation per track, named after the track, for every pack the same
        # way.
        if rig.animation_data is None:
            rig.animation_data_create()
        rig.animation_data.action = None
        for track in list(rig.animation_data.nla_tracks):
            rig.animation_data.nla_tracks.remove(track)
        for action in sorted(bpy.data.actions, key=lambda a: a.name):
            track = rig.animation_data.nla_tracks.new()
            track.name = action.name
            start = int(action.frame_range[0])
            track.strips.new(action.name, start, action)

        bpy.ops.object.select_all(action="DESELECT")
        mesh.select_set(True)
        rig.select_set(True)
        bpy.context.view_layer.objects.active = rig

        joints = [b.name for b in rig.data.bones]
        if not entry["dry_run"]:
            bpy.ops.export_scene.gltf(
                filepath=entry["glb"],
                export_format="GLB",
                use_selection=True,
                export_yup=True,
                export_apply=False,
                export_animations=True,
                export_animation_mode="NLA_TRACKS",
                export_nla_strips=True,
                export_bake_animation=True,
                export_skins=True,
                export_materials="NONE",
                export_extras=False,
            )

        # Where the hands and the whole rig end up RELATIVE TO THE EYE, in
        # engine axes. This is the number the framing is judged on: a correct
        # first-person rig puts its hands a little below and well in front of
        # the eye, and reporting it turns "drag the slider until it looks right"
        # into arithmetic.
        verts = [mesh.matrix_world @ v.co for v in mesh.data.vertices]
        bounds = [0.0] * 6
        if verts:
            bounds = [min(v.x for v in verts), max(v.x for v in verts),
                      min(v.z for v in verts), max(v.z for v in verts),
                      min(-v.y for v in verts), max(-v.y for v in verts)]

        report["packs"].append({
            "id": entry["id"],
            "scale": s,
            "arm_bone": measured,
            "centroid_y": centroid,
            "flipped": flipped,
            "bounds": bounds,
            "removed": removed,
            "verts_before": before,
            "verts_after": after,
            "joints": joints,
            "clips": sorted(set(discovered)),
            "final_clips": sorted({a.name for a in bpy.data.actions}),
        })
    except Exception as exc:
        import traceback
        report["errors"].append("%s: %s\n%s"
                                % (entry["id"], exc, traceback.format_exc()))

with open(job["report"], "w") as f:
    json.dump(report, f, indent=1)
print("RAVEN_ANIMHANDS_DONE %d packs, %d errors"
      % (len(report["packs"]), len(report["errors"])))
'''


def run_blender(entries: list, report_path: str) -> dict:
    job = {"packs": entries, "report": report_path}
    env = dict(os.environ, RAVEN_ANIMHANDS_JOB=json.dumps(job))
    result = subprocess.run(
        ["blender", "-b", "--factory-startup", "--python-expr", BLENDER_SCRIPT],
        env=env, capture_output=True, text=True)
    if not os.path.isfile(report_path):
        sys.stderr.write(result.stdout[-6000:] + "\n" + result.stderr[-3000:])
        raise SystemExit("blender produced no report")
    with open(report_path) as stream:
        return json.load(stream)


# --- cooking ------------------------------------------------------------------

def find_gltf2ozz(explicit: str) -> str:
    if explicit:
        return explicit
    candidate = os.path.join(
        REPO, "build", "_deps", "ozz_animation_source-build", "src",
        "animation", "offline", "gltf", "gltf2ozz")
    return candidate if os.path.isfile(candidate) else ""


def cook(rig: str, glb: str, tool: str) -> bool:
    """GLB -> runtime skeleton and clips, staged then renamed.

    A failed conversion leaves the previous cook alone rather than an empty
    directory, which is the difference between "this rig did not update" and
    "the player has no hands".
    """
    out = os.path.join(ASSETS, "animations", "viewmodels", rig)
    stage = out + ".stage"
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(stage, exist_ok=True)

    config = {
        "skeleton": {
            "filename": "%s.skeleton.ozz" % rig,
            "import": {"enable": True, "raw": False,
                       "types": {"skeleton": True, "marker": False,
                                 "camera": False, "geometry": False,
                                 "light": False, "null": False, "any": False}},
        },
        "animations": [{
            "clip": "*", "filename": "clip_*.ozz", "raw": False,
            "additive": False, "sampling_rate": 30, "iframe_interval": 2,
            "optimize": True,
            "optimization_settings": {"tolerance": 0.0005, "distance": 0.1,
                                      "override": []},
            "tracks": {"properties": [], "motion": {"enable": False}},
        }],
    }
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


def write_sidecar(rig_id: str, glb: str, label: str) -> None:
    guid = hashlib.sha1(rig_id.encode()).hexdigest()[:16]
    with open(glb + ".meta", "w") as f:
        f.write("# Resource database record. Written by "
                "tools/author_animated_hands.py.\n"
                "#\n"
                "# A skinned rig: gltf2ozz owns it, not the static mesh row.\n"
                "schema = 1\n"
                'guid = "%s"\n'
                'type = "mesh"\n'
                'name = "%s"\n'
                'tags = ["viewmodel", "skinned", "hands", "animated"]\n'
                "\n"
                "[import]\n"
                "skip = true\n"
                'skip_reason = "skinned rig: gltf2ozz owns it"\n'
                % (guid, label))


# --- sockets ------------------------------------------------------------------

def sockets_for(joints: list) -> list:
    """Named attach points, matched against whatever the pack called its bones.

    The socket layer exists exactly for this. These rigs come from five
    different authors with five different naming schemes -- `Hand_R_038`,
    `hand_r`, `mixamorig:RightHand` -- and a weapon must never have to know
    which. It asks for "right_hand" and this decides what that means on this
    skeleton.
    """
    wanted = [
        # Exact names first, then prefixes. The order inside each list is the
        # order they are tried, and it matters: two of these packs name their
        # bones `hand_L.L` and `hand_L.R`, where the base reads "L" and the real
        # side is the SUFFIX. A prefix match on "hand_l" hits both, so the exact
        # name has to win or the right hand resolves to the left one.
        ("right_hand", ["hand_l.r", "hand_r", "hand.r", "righthand"]),
        ("left_hand", ["hand_l.l", "hand_l", "hand.l", "lefthand"]),
        ("right_forearm", ["lowerarm_l.r", "forearm_r", "forearm.r",
                           "lowerarm_r"]),
        ("left_forearm", ["lowerarm_l.l", "forearm_l", "forearm.l",
                          "lowerarm_l"]),
        # The weapon's own parts, on the packs that animate them. A muzzle flash
        # belongs at the muzzle and a shell ejects from the bolt, and these are
        # the only rigs in the project where those points exist at all.
        ("weapon", ["wpn_body", "weapon", "receiver", "gun"]),
        ("weapon_bolt", ["wpn_zatvor", "bolt", "slide"]),
        ("weapon_magazine", ["wpn_cartrige", "wpn_cartridge", "magazine",
                             "pmag", "mag_"]),
        ("weapon_muzzle", ["wpn_silencer", "muzzle", "barrel", "silencer"]),
    ]
    lowered = {joint.lower(): joint for joint in joints}
    out = []
    for name, needles in wanted:
        match = None
        # Exact over the whole needle list before any prefix is tried, so a
        # pack that spells a bone exactly is never beaten by another pack's
        # prefix rule.
        for needle in needles:
            if needle in lowered:
                match = lowered[needle]
                break
        if not match:
            for needle in needles:
                for low, original in sorted(lowered.items()):
                    # Prefix, never substring: `hand_r` must not match
                    # `IK_Hand_Cntrl_R`, which is a control and deforms nothing.
                    if low.startswith(needle) and "ik_" not in low:
                        match = original
                        break
                if match:
                    break
        if match:
            out.append((name, match))
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pack", action="append", default=[])
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--skip-cook", action="store_true")
    parser.add_argument("--gltf2ozz", default="")
    parser.add_argument("--budget", type=int, default=DEFAULT_BUDGET)
    args = parser.parse_args()

    packs = [pack_by_id(p) for p in args.pack] if args.pack else PACKS
    mesh_dir = os.path.join(ASSETS, "meshes", "viewmodels")
    os.makedirs(mesh_dir, exist_ok=True)

    import pngkit

    entries, published = [], []
    for pack in packs:
        source = os.path.join(SOURCE, pack["source"])
        if not os.path.isfile(source):
            print("%-16s MISSING %s" % (pack["id"], pack["source"]))
            continue
        textures = albedo_textures(pack)
        cells = max(1, len(textures))
        rel_texture = ""
        if textures and not args.dry_run:
            textures = to_png(
                textures,
                os.path.join(REPO, "build", "animhands-textures", pack["id"]))
            textures = [t for t in textures if os.path.isfile(t)]
            cells = max(1, len(textures))
            image = pngkit.atlas([pngkit.read(t) for t in textures],
                                 cell=ATLAS_CELL)
            rel_texture = "textures/viewmodels/%s_albedo.png" % pack["id"]
            out = os.path.join(ASSETS, rel_texture)
            os.makedirs(os.path.dirname(out), exist_ok=True)
            pngkit.write(out, image)

        glb = os.path.join(mesh_dir, pack["id"] + ".glb")
        entries.append({"id": pack["id"], "source": source, "glb": glb,
                        "scale": pack["scale"], "cells": cells,
                        "clips": pack["clips"], "budget": args.budget,
                        "dry_run": args.dry_run})
        published.append({"id": pack["id"], "label": pack["name"], "glb": glb,
                          "texture": rel_texture, "cells": cells,
                          "material": "Builtin/Viewmodel/%s" % "".join(
                              w.capitalize() for w in pack["id"].split("_")[1:]),
                          "clips": pack["clips"]})

    if not entries:
        raise SystemExit("no pack matched")

    report_path = os.path.join(REPO, "build", "animated_hands_report.json")
    os.makedirs(os.path.dirname(report_path), exist_ok=True)
    report = run_blender(entries, report_path)
    for error in report["errors"]:
        print("  ! " + error)

    built = {row["id"]: row for row in report["packs"]}
    final = []
    tool = "" if args.skip_cook else find_gltf2ozz(args.gltf2ozz)
    for rig in published:
        row = built.get(rig["id"])
        if not row:
            continue
        rig["sockets"] = sockets_for(row["joints"])
        # Cooked clip names, for the idle fallback above.
        rig["fallback_idle"] = row["final_clips"][0] if row["final_clips"] \
            else "idle"
        print("%-16s %5d -> %4d verts  %2d joints  %2d sockets  clips: %s"
              % (rig["id"], row["verts_before"], row["verts_after"],
                 len(row["joints"]), len(rig["sockets"]),
                 ", ".join(row["final_clips"])[:70]))
        if args.dry_run:
            continue
        write_sidecar(rig["id"], rig["glb"], rig["label"])
        if tool and not cook(rig["id"], rig["glb"], tool):
            print("  ! cook failed for " + rig["id"])
            continue
        final.append(rig)

    if final and not args.dry_run:
        append_config(final)
        append_materials(final)
    print("\n%d animated rigs authored" % len(final))
    return 0


def append_materials(rigs: list) -> None:
    """One material per rig, appended to the hands material script.

    Appended rather than rewritten: tools/author_hand_rigs.py owns the top of
    that file and the two tools must not overwrite each other's work.
    """
    path = os.path.join(ASSETS, "materials", "viewmodel_hands.mat")
    existing = ""
    if os.path.isfile(path):
        with open(path) as stream:
            existing = stream.read()
    with open(path, "a") as f:
        if "author_animated_hands" not in existing:
            f.write("\n# --- animated hand+weapon rigs "
                    "(tools/author_animated_hands.py) ---\n")
        for rig in rigs:
            if '"%s"' % rig["material"] in existing:
                continue
            f.write('\n[material."%s"]\n' % rig["material"])
            f.write('shader = "lit"\n')
            f.write('texture = "%s"\n' % os.path.basename(rig["texture"]))
            f.write('filter = "nearest"\n')
            f.write('address = "clamp"\n')
            f.write("highlight = false\n")


def append_config(rigs: list) -> None:
    """Rewrite this tool's section of viewmodel_hands.toml.

    Its OWN section: everything from the marker below to the end of the file.
    tools/author_hand_rigs.py owns everything above it, and the two must not
    overwrite each other -- but a generator that merely appends, skipping ids it
    already sees, can never change a value it wrote before. That is how the
    framing added here silently failed to appear on a re-run.
    """
    marker = "# --- animated hand+weapon rigs "
    path = os.path.join(ASSETS, "config", "viewmodel_hands.toml")
    existing = ""
    if os.path.isfile(path):
        with open(path) as stream:
            existing = stream.read()
    head = existing.split(marker)[0].rstrip() + "\n"

    with open(path, "w") as f:
        f.write(head)
        f.write('''
# --- animated hand+weapon rigs -----------------------------------------------
#
# Written by tools/author_animated_hands.py from assets/source/animations.
# Everything ABOVE this line belongs to tools/author_hand_rigs.py; everything
# below is regenerated whole on each run.
#
# These differ from the rigs above in one way that matters: the WEAPON IS PART
# OF THE RIG. They were authored as hands already holding a gun, so the clips
# have the left hand on the foregrip and the reload actually finds the magazine
# well -- which is the whole reason to prefer them over a weapon hung on a
# socket and animated separately.
#
# `bundled_weapon = true` is how a weapon definition knows not to attach its own
# model on top: naming one of these rigs IS choosing the weapon's presentation.
#
# Each states its own `offset`/`rotation`/`scale`, because the global
# [player_viewmodel] block cannot serve them -- see the comment on
# HandsDefinition::hasFraming.
''')
        for rig in rigs:
            f.write("\n[[rig]]\n")
            f.write('id = "%s"\n' % rig["id"])
            f.write('name = "%s"\n' % rig["label"])
            f.write('model = "meshes/viewmodels/%s.glb"\n' % rig["id"])
            f.write('skeleton = "animations/viewmodels/%s/%s.skeleton.ozz"\n'
                    % (rig["id"], rig["id"]))
            f.write('clip_dir = "animations/viewmodels/%s"\n' % rig["id"])
            f.write('material = "%s"\n' % rig["material"])
            f.write("bundled_weapon = true\n")
            # The tool bakes the half turn onto the engine's forward and
            # normalises the rig to metres with its eye at the origin, so the
            # right placement is "no turn, no shrink, sit at the eye". The small
            # drop and push forward is the composition every shooter uses: the
            # weapon low enough to leave the centre of the screen readable.
            # Down a little and well forward. The eye anchor puts the rig AT
            # the camera, so without the push the weapon is inside the near
            # plane and fills the frame.
            f.write("offset = [0.0, -0.10, -0.32]\n")
            f.write("rotation = [0.0, 0.0, 0.0]\n")
            f.write("scale = 1.0\n")
            idle = "idle" if "idle" in rig["clips"] else rig["fallback_idle"]
            f.write('idle_animation = "%s"\n' % idle)
            f.write("\n")
            for name, joint in rig["sockets"]:
                f.write("[[rig.socket]]\n")
                f.write('name = "%s"\n' % name)
                f.write('joint = "%s"\n' % joint)
                f.write("offset = [0.0, 0.0, 0.0]\n")
                f.write("rotation = [0.0, 0.0, 0.0]\n")
                f.write("scale = 1.0\n\n")


if __name__ == "__main__":
    sys.exit(main())
