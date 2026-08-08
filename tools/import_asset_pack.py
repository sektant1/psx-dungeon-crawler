#!/usr/bin/env python3
"""Vendor asset packs -> engine content, in one repeatable pass.

`assets/source/` holds downloaded packs exactly as they were unzipped: FBX in
whatever units the author worked in, PNGs in directories named after the tool
that made them, and the same model shipped three times under different folders.
None of that is loadable, and none of it is nameable -- `Swords/Katana/
katana_01/katana_01.fbx` is not a runtime path and `Material_Base_color.png` is
not a texture id.

This is the step between. It reads a manifest describing each pack, and for
every source model it publishes:

    assets/meshes/<domain>/<name>.obj        extended OBJ, engine conventions
    assets/textures/<domain>/<name>.png      canonicalised, deduplicated
    assets/materials/<domain>.mat            one material per texture
    assets/prefabs/<domain>.prefab.toml      placeable definitions, sizes measured
    *.meta                                   resource-database sidecars

Everything it writes is derived. Delete the outputs, re-run, and you get the
same bytes -- which is what makes re-importing a pack after a manifest change a
non-event rather than a merge.

WHY BLENDER. `tools/fbx_to_obj.py` parses FBX without dependencies and is fine
for a single-mesh kit, but it ignores object-level transforms, cannot resolve a
material's texture, and has no idea what an armature is. Half of these packs
author models away from the origin with a node transform doing the placing, so
that path silently scatters them. Blender is already this repo's authoring tool
(`blend_to_obj.py`, `author_humanoid_rig.py`) and it reads all three source
formats correctly.

CONVENTIONS. Identical to tools/blend_to_obj.py, and for the same reasons:
Blender is Z-up/-Y-forward and the engine is Y-up/-Z-forward, so positions go
through `(x, z, -y)`; UVs are written v-flipped because every material in this
engine samples that way; meshes are triangulated because the OBJ loader wants
triangles; modifiers are applied.

Usage:
  tools/import_asset_pack.py [--manifest F] [--pack ID ...] [--dry-run]
                             [--report F] [--jobs N]

  --pack      import only these packs (repeatable); default is every pack
  --dry-run   print what would be written, touch nothing
  --report    where the Blender phase leaves its JSON (default: a temp file)

Requires the `blender` CLI on PATH.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(REPO, "assets")
DEFAULT_MANIFEST = os.path.join(ASSETS, "source", "packs.toml")

# --- naming ------------------------------------------------------------------
#
# docs/asset-naming.md is the standard: stable ids are lowercase snake case,
# runtime paths are lowercase snake, material ids are exactly three PascalCase
# segments. A vendor name reaches none of those on its own -- "BreadEnd",
# "Material.001_Base_color", "Crank Flashlight 1" -- so every name published
# below goes through canonical() first, and nothing else invents one.

_SNAKE_BOUNDARY = re.compile(r"(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])")
_NON_TOKEN = re.compile(r"[^a-z0-9]+")
_ORDINAL = re.compile(r"^(.*?)([0-9]+)$")

# Vendor words that describe the export, not the subject. Dropping them is what
# turns `Material_Base_color` into nothing and lets the caller fall back to the
# directory name, which is the actual subject.
_NOISE = {
    "material", "materials", "base", "color", "colour", "defaultmaterial",
    "texture", "tex", "map", "diffuse", "albedo", "png", "fbx", "obj",
    "default", "lambert", "standardsurface", "mat",
}


def canonical(text: str, *, drop_noise: bool = False) -> str:
    """Vendor spelling -> a stable lowercase-snake id."""
    spaced = _SNAKE_BOUNDARY.sub("_", text)
    tokens = [t for t in _NON_TOKEN.sub("_", spaced.lower()).split("_") if t]
    if drop_noise:
        tokens = [t for t in tokens if t not in _NOISE]
    if not tokens:
        return ""
    # `door1` -> `door_01`: the standard forbids unpadded ordinals, and a pack
    # that mixes `arrow_01` with `Bottle3` should not publish both spellings.
    merged: list[str] = []
    for token in tokens:
        match = _ORDINAL.match(token)
        if match and match.group(1):
            merged.append(match.group(1))
            merged.append("%02d" % int(match.group(2)))
        elif token.isdigit():
            merged.append("%02d" % int(token))
        else:
            merged.append(token)
    name = "_".join(merged)
    if name[0].isdigit():
        name = "n_" + name
    return name


def pascal(text: str) -> str:
    return "".join(part.capitalize() for part in canonical(text).split("_") if part)


def unique(name: str, taken: set) -> str:
    """A name nobody has claimed, numbered rather than hashed.

    docs/asset-naming.md permits a short source hash on collision, but a
    readable `_02` is what an author can actually find again in a browser, and
    the importer is deterministic so the number is stable across runs.
    """
    if name not in taken:
        taken.add(name)
        return name
    for n in range(2, 1000):
        candidate = "%s_%02d" % (name, n)
        if candidate not in taken:
            taken.add(candidate)
            return candidate
    raise RuntimeError("cannot disambiguate " + name)


# --- manifest ----------------------------------------------------------------

def load_manifest(path: str) -> dict:
    try:
        import tomllib
    except ImportError:  # pragma: no cover - python < 3.11
        import tomli as tomllib  # type: ignore
    with open(path, "rb") as stream:
        return tomllib.load(stream)


def pack_defaults(pack: dict) -> dict:
    out = {
        "id": pack["id"],
        "label": pack.get("label", pack["id"]),
        "source": pack["source"],
        "domain": pack.get("domain", pack["id"]),
        "models": pack.get("models", ["**/*.fbx"]),
        "exclude": pack.get("exclude", []),
        # Source unit -> metre. Left at 1.0 unless a pack is measurably wrong;
        # --dry-run prints the measured bounds so the number is chosen from
        # evidence rather than from what the vendor's readme claims.
        "scale": float(pack.get("scale", 1.0)),
        # One OBJ per mesh object in the file. A pack that ships `ToolsAll.fbx`
        # means every tool in it, and joining them produces one unplaceable
        # blob; a pack that ships a model split into panels means one prop.
        "split_objects": bool(pack.get("split_objects", True)),
        # Some packs put twelve props in ONE object with twelve disconnected
        # islands -- `FruitsSingle.fbx` is one mesh holding every fruit. Object
        # splitting cannot see those; separating by loose parts can, and
        # without it the whole set publishes as a single unplaceable mesh.
        "split_loose": bool(pack.get("split_loose", False)),
        "role": pack.get("role", "prop"),
        "placement": pack.get("placement", "prop"),
        "collider": pack.get("collider", "box"),
        # Skinned packs are not published as OBJ at all -- see
        # tools/author_hand_rigs.py, which owns the rigged rows.
        "skinned": bool(pack.get("skinned", False)),
        "bake_colours": bool(pack.get("bake_colours", False)),
        # Per-model escapes from the pack-wide defaults, keyed by source stem.
        # Vendor packs are not internally consistent -- PSXForest ships a
        # "small tree" four times the height of its other small tree -- and one
        # scale per pack cannot express that. Overriding the outlier is honest;
        # re-authoring the pack is not this tool's job.
        "model_scale": pack.get("model_scale", {}),
        "model_texture": pack.get("model_texture", {}),
        # "a longsword is 1.1 m", stated instead of "multiply by 0.53".
        #
        # These packs are not internally consistent: medieval-weapons ships
        # swords in metres and katanas at 4.7x, and the vendor's own comments
        # say the gun scale is off. A single `scale` cannot express that, and a
        # per-model multiplier table for 231 meshes is 231 guesses nobody can
        # check. A rule says what the thing IS -- match a name, give its
        # longest axis in metres -- and the importer measures the mesh and
        # solves for the factor. Rules are tried in order, first match wins,
        # and anything unmatched keeps the pack `scale`.
        "size_rules": [
            (str(rule["match"]), float(rule["length"]))
            for rule in pack.get("size_rule", [])
        ],
        "texture_fallback": pack.get("texture_fallback", ""),
        # Owner segment of the material id. These packs ship WITH the engine --
        # they are its default content library, not one game's art -- so the
        # owner is `Builtin`, and `Game/...` stays available for art a title
        # adds on top. docs/asset-naming.md permits an explicit owner beside
        # Engine and Game; `Engine` is wrong here, that prefix means an engine
        # subsystem's own material (Engine/Psx/Lit), not shipped content.
        "owner": pack.get("owner", "Builtin"),
        "material_shader": pack.get("material_shader", "lit"),
        "material_address": pack.get("material_address", "clamp"),
        "two_sided": bool(pack.get("two_sided", False)),
        "prefix": pack.get("prefix", ""),
    }
    return out


def source_models(pack: dict) -> list:
    """Every model file the pack names, deduplicated by content.

    Packs nest a second copy of themselves (`Guns/guns/Ak/ak.fbx`) and ship the
    same model as FBX *and* OBJ. Importing both publishes two prefabs that are
    one prop, so identical bytes collapse to whichever path sorts first --
    stable, and it prefers the shallower directory.
    """
    import glob

    root = os.path.join(ASSETS, "source", pack["source"])
    found: list[str] = []
    for pattern in pack["models"]:
        found += glob.glob(os.path.join(root, pattern), recursive=True)
    excluded = []
    for pattern in pack["exclude"]:
        excluded += glob.glob(os.path.join(root, pattern), recursive=True)
    skip = set(os.path.abspath(p) for p in excluded)

    seen: dict[str, str] = {}
    ordered = []
    for path in sorted(set(found), key=lambda p: (p.count(os.sep), p)):
        if os.path.abspath(path) in skip:
            continue
        low = path.lower()
        if "__macosx" in low or os.path.basename(path).startswith("."):
            continue
        digest = hashlib.sha1(open(path, "rb").read()).hexdigest()
        if digest in seen:
            continue
        seen[digest] = path
        ordered.append(path)
    return ordered


# --- the Blender phase -------------------------------------------------------
#
# Passed with --python-expr so this tool stays one file. It writes a JSON report
# and nothing else: every decision about where a file lands, what it is called
# and what material it gets is made back in the host process, where it can be
# read without launching Blender.

BLENDER_SCRIPT = r'''
import bpy, json, os, sys, math
from mathutils import Matrix, Vector

job = json.loads(os.environ["RAVEN_IMPORT_JOB"])
report = {"models": [], "errors": []}


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def import_file(path):
    lower = path.lower()
    if lower.endswith(".fbx"):
        bpy.ops.import_scene.fbx(filepath=path, use_image_search=True)
    elif lower.endswith(".obj"):
        bpy.ops.wm.obj_import(filepath=path)
    elif lower.endswith((".glb", ".gltf")):
        bpy.ops.import_scene.gltf(filepath=path)
    elif lower.endswith(".blend"):
        with bpy.data.libraries.load(path) as (src, dst):
            dst.objects = src.objects
        for obj in dst.objects:
            if obj is not None:
                bpy.context.collection.objects.link(obj)
    else:
        raise ValueError("no importer for " + path)


def to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def material_textures(obj):
    """Image files this object's materials sample, in slot order."""
    out = []
    for slot in obj.material_slots:
        image = None
        material = slot.material
        if material and material.use_nodes:
            for node in material.node_tree.nodes:
                if node.type == "TEX_IMAGE" and node.image:
                    image = node.image
                    break
        path = ""
        if image:
            try:
                path = bpy.path.abspath(image.filepath_from_user())
            except Exception:
                path = bpy.path.abspath(image.filepath)
        # The basename survives even when the path does not. These packs were
        # exported with embedded media, so every material names
        # `<model>.fbm/atlas.png` -- a directory the unzip never created. The
        # file IS in the pack, one level up, and its name is the only thing
        # that says which atlas this model wants.
        out.append({"material": material.name if material else "",
                    "image": path if path and os.path.isfile(path) else "",
                    "basename": os.path.basename(path) if path else ""})
    return out


def slot_colours(obj):
    colours = []
    for slot in obj.material_slots:
        rgba = (1.0, 1.0, 1.0, 1.0)
        material = slot.material
        if material:
            if material.use_nodes:
                bsdf = next((n for n in material.node_tree.nodes
                             if n.type == "BSDF_PRINCIPLED"), None)
                if bsdf:
                    rgba = tuple(bsdf.inputs["Base Color"].default_value)
            else:
                rgba = tuple(material.diffuse_color)
        colours.append((to_linear(rgba[0]), to_linear(rgba[1]),
                        to_linear(rgba[2]), rgba[3]))
    return colours or [(1.0, 1.0, 1.0, 1.0)]


def write_obj(obj, out_path, scale, bake_colours, source_name):
    """One mesh object -> the extended OBJ the engine's loader reads."""
    depsgraph = bpy.context.evaluated_depsgraph_get()
    evaluated = obj.evaluated_get(depsgraph)
    mesh = evaluated.to_mesh()

    import bmesh
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    # Split non-manifold edges -- an edge shared by three or more faces.
    #
    # The engine's mesh loader refuses these outright, and it is not being
    # fussy: an edge with three faces has no single surface orientation, so a
    # normal, a tangent and a collision winding are all ambiguous there.
    #
    # Low-poly vendor art produces them by accident: a flat ear or fin welded
    # into a solid body shares its root edge with both. Only one model in this
    # library trips it, but a pack imported next month will trip it again, so
    # the repair belongs here rather than in a note about that one mesh.
    #
    # Splitting duplicates the vertices along the seam and changes nothing about
    # the rendered image -- the same triangles are drawn in the same places, and
    # each now has an unambiguous surface to belong to.
    nonmanifold = [e for e in bm.edges if not e.is_manifold and len(e.link_faces) > 2]
    if nonmanifold:
        bmesh.ops.split_edges(bm, edges=nonmanifold)
    bm.to_mesh(mesh)
    bm.free()

    # Object transform, then Blender -> engine axes, then the pack's scale.
    # Baked in: a mesh that is only upright under one node transform is not an
    # asset, and half these packs place the model with a node transform.
    s = scale
    axis = Matrix(((s, 0, 0, 0), (0, 0, s, 0), (0, -s, 0, 0), (0, 0, 0, 1)))
    xform = axis @ obj.matrix_world
    normal_xform = xform.to_3x3().inverted_safe().transposed()

    colours = slot_colours(evaluated)
    uv_layer = mesh.uv_layers.active

    verts, uvs, norms, faces, index = [], [], [], [], {}
    used_slots = set()
    skipped = 0
    for poly in mesh.polygons:
        # Drop zero-area triangles outright.
        #
        # Patching their normal was the first attempt and it only moved the
        # problem: a degenerate triangle also has no tangent frame and no UV
        # area, so the engine's import validation rejected the model for the
        # tangent instead. The triangle contributes nothing to the rendered
        # image either way -- it has no area -- so the honest fix is not to
        # publish it. One in 1158 in the AK's mesh; a handful across the pack.
        if poly.area < 1e-9:
            skipped += 1
            continue
        slot = min(poly.material_index, len(colours) - 1)
        used_slots.add(poly.material_index)
        colour = colours[slot] if bake_colours else None
        tri = []
        for loop_index in poly.loop_indices:
            loop = mesh.loops[loop_index]
            co = xform @ mesh.vertices[loop.vertex_index].co
            no = normal_xform @ loop.normal
            # A zero-length normal comes out of a degenerate triangle in the
            # source -- one in 1158 in the AK's mesh, and enough to fail the
            # engine's import validation for the whole model. `normalized()` on
            # a zero vector returns zero rather than raising, so the check has
            # to be here; the face normal is the best available answer, and the
            # polygon's own is what a renderer would compute anyway.
            if no.length_squared < 1e-12:
                no = normal_xform @ poly.normal
            if no.length_squared < 1e-12:
                no = Vector((0.0, 1.0, 0.0))
            no = no.normalized()
            uv = uv_layer.data[loop_index].uv if uv_layer else (0.0, 0.0)
            key = (round(co.x, 6), round(co.y, 6), round(co.z, 6),
                   round(uv[0], 6), round(uv[1], 6),
                   round(no.x, 4), round(no.y, 4), round(no.z, 4), colour)
            at = index.get(key)
            if at is None:
                at = len(verts) + 1
                index[key] = at
                verts.append((co.x, co.y, co.z, colour))
                # v flipped, matching gltf_to_obj/blend_to_obj: every material
                # in this engine samples that way.
                uvs.append((uv[0], 1.0 - uv[1]))
                norms.append((no.x, no.y, no.z))
            tri.append(at)
        faces.append(tri)

    if not faces:
        evaluated.to_mesh_clear()
        return None

    # Drop triangles that would make an edge non-manifold.
    #
    # The engine's loader refuses a mesh where three or more faces share an
    # edge, and it is not being fussy -- such an edge has no single surface
    # orientation, so its normal, tangent and collision winding are all
    # ambiguous. Low-poly vendor art produces them by accident: a flat ear or
    # fin welded into a solid body shares its root edge with both sides.
    #
    # Two repairs were tried before this one and neither survives the round
    # trip. Splitting the edges in bmesh is undone by the welding loop below,
    # which keys vertices on (position, uv, normal, colour) and merges the split
    # pair straight back. Duplicating the vertices here is undone by Assimp,
    # which joins identical vertices on load for exactly the same reason.
    #
    # So the face is dropped, the same answer degenerate triangles get. It is
    # the third or later face on an edge that already has two -- an interior
    # flap that is invisible from outside the model -- and one cow in a library
    # of 447 meshes has any.
    edge_use = {}
    repaired = 0
    kept_faces = []
    for triangle in faces:
        keys = []
        conflict = False
        for k in range(3):
            a, b = triangle[k], triangle[(k + 1) % 3]
            key = (a, b) if a < b else (b, a)
            keys.append(key)
            if edge_use.get(key, 0) >= 2:
                conflict = True
        if conflict:
            repaired += 1
            continue
        for key in keys:
            edge_use[key] = edge_use.get(key, 0) + 1
        kept_faces.append(triangle)
    faces = kept_faces
    if not faces:
        evaluated.to_mesh_clear()
        return None

    lo = [min(v[i] for v in verts) for i in range(3)]
    hi = [max(v[i] for v in verts) for i in range(3)]

    with open(out_path, "w") as f:
        f.write("# %s -- imported by tools/import_asset_pack.py from %s\n"
                % (obj.name, source_name))
        if bake_colours:
            f.write("# extended OBJ: v x y z r g b a (linear vertex colour)\n")
        f.write("o %s\n" % os.path.splitext(os.path.basename(out_path))[0])
        for v in verts:
            if bake_colours and v[3] is not None:
                f.write("v %.6f %.6f %.6f %.4f %.4f %.4f %.4f\n"
                        % (v[0], v[1], v[2], v[3][0], v[3][1], v[3][2], v[3][3]))
            else:
                f.write("v %.6f %.6f %.6f\n" % (v[0], v[1], v[2]))
        for uv in uvs:
            f.write("vt %.6f %.6f\n" % uv)
        for n in norms:
            f.write("vn %.6f %.6f %.6f\n" % n)
        for tri in faces:
            f.write("f %s\n" % " ".join("%d/%d/%d" % (i, i, i) for i in tri))

    evaluated.to_mesh_clear()
    return {
        "verts": len(verts),
        "tris": len(faces),
        "min": lo,
        "max": hi,
        "size": [hi[i] - lo[i] for i in range(3)],
        "slots": sorted(used_slots),
        "degenerate": skipped,
        "repaired": repaired,
    }


for entry in job["models"]:
    try:
        reset()
        import_file(entry["source"])
        meshes = [o for o in bpy.data.objects if o.type == "MESH"]
        if not meshes:
            report["errors"].append("no mesh in " + entry["source"])
            continue
        if not entry["split_objects"] and len(meshes) > 1:
            # Join into the largest, so a model shipped as separate panels
            # publishes as one prop rather than as twelve.
            bpy.ops.object.select_all(action="DESELECT")
            for o in meshes:
                o.select_set(True)
            target = max(meshes, key=lambda o: len(o.data.vertices))
            bpy.context.view_layer.objects.active = target
            bpy.ops.object.join()
            meshes = [target]

        if entry["split_loose"]:
            bpy.ops.object.select_all(action="DESELECT")
            for o in meshes:
                o.select_set(True)
            bpy.context.view_layer.objects.active = meshes[0]
            bpy.ops.object.mode_set(mode="EDIT")
            bpy.ops.mesh.select_all(action="SELECT")
            bpy.ops.mesh.separate(type="LOOSE")
            bpy.ops.object.mode_set(mode="OBJECT")
            meshes = [o for o in bpy.data.objects if o.type == "MESH"]

        armature = any(o.type == "ARMATURE" for o in bpy.data.objects)
        for obj in sorted(meshes, key=lambda o: o.name):
            out = os.path.join(entry["out_dir"], "__pending__.obj")
            stats = write_obj(obj, out, entry["scale"], entry["bake_colours"],
                              os.path.basename(entry["source"]))
            if stats is None:
                continue
            record = dict(stats)
            record["source"] = entry["source"]
            record["object"] = obj.name
            record["armature"] = armature
            record["textures"] = material_textures(obj)
            record["temp"] = out + ".%d" % len(report["models"])
            os.replace(out, record["temp"])
            report["models"].append(record)
    except Exception as exc:  # keep going: one bad file is not a failed import
        report["errors"].append("%s: %s" % (entry["source"], exc))

with open(job["report"], "w") as f:
    json.dump(report, f, indent=1)
print("RAVEN_IMPORT_DONE %d models, %d errors"
      % (len(report["models"]), len(report["errors"])))
'''


def run_blender(models: list, report_path: str) -> dict:
    job = {"models": models, "report": report_path}
    env = dict(os.environ, RAVEN_IMPORT_JOB=json.dumps(job))
    command = ["blender", "-b", "--factory-startup",
               "--python-expr", BLENDER_SCRIPT]
    result = subprocess.run(command, env=env, capture_output=True, text=True)
    if not os.path.isfile(report_path):
        sys.stderr.write(result.stdout[-4000:] + "\n" + result.stderr[-4000:])
        raise SystemExit("blender produced no report")
    with open(report_path) as stream:
        return json.load(stream)


# --- publishing --------------------------------------------------------------

def sidecar(path: str, asset_type: str, name: str, tags: list) -> None:
    """A resource-database record for a file the importer just wrote.

    Guids are derived from the logical path rather than random, so re-running
    the importer does not renumber the database and produce a diff in every
    sidecar it touches.
    """
    logical = os.path.relpath(path, ASSETS).replace(os.sep, "/")
    guid = hashlib.sha1(logical.encode()).hexdigest()[:16]
    with open(path + ".meta", "w") as f:
        f.write("# Resource database record. Written by "
                "tools/import_asset_pack.py -- re-running regenerates it.\n")
        f.write("schema = 1\n")
        f.write('guid = "%s"\n' % guid)
        f.write('type = "%s"\n' % asset_type)
        f.write('name = "%s"\n' % name.replace('"', ""))
        if tags:
            f.write("tags = [%s]\n" % ", ".join('"%s"' % t for t in tags))


def publish_texture(src: str, domain: str, taken: dict, names: set,
                    dry: bool) -> str:
    """Copy a source PNG into textures/<domain>/, deduplicated by content.

    `taken` maps content digest -> published path, so the same image reaching us
    twice publishes once. `names` is the set of basenames already claimed, and
    it is a separate argument because the two answer different questions --
    comparing a bare name against a set of paths, which is what this did at
    first, silently never collides and so never dedupes.
    """
    digest = hashlib.sha1(open(src, "rb").read()).hexdigest()
    if digest in taken:
        return taken[digest]

    stem = canonical(os.path.splitext(os.path.basename(src))[0],
                     drop_noise=True)
    # `txt.png` is what this pack's author called every gun body atlas, so
    # sixteen of them canonicalise to the same word and publish as
    # `txt_albedo_07`, which names nothing. A stem that is a generic word for
    # "the texture" is treated exactly like an empty one: the directory it sits
    # in is the subject, and here that directory is the gun.
    if stem in ("txt", "tex", "image", "untitled", "skin"):
        stem = ""
    if not stem or stem.startswith("n_"):
        # `Material_Base_color.png` says nothing, and a numbered directory
        # (`Spiders/Gold/base.png`) canonicalises to `n_01`, which says less.
        # The directory the file sits in is the subject; for the variant
        # folders it is the one above that.
        parent = os.path.basename(os.path.dirname(src))
        if parent.isdigit():
            grand = os.path.basename(os.path.dirname(os.path.dirname(src)))
            stem = "%s_%02d" % (canonical(grand, drop_noise=True) or "variant",
                                int(parent))
        else:
            parts = [canonical(p, drop_noise=True)
                     for p in os.path.dirname(src).split(os.sep)[::-1]]
            stem = next((p for p in parts if p and not p.startswith("n_")),
                        "texture")

    # Domain-qualified, always.
    #
    # Texture BASENAMES have to be globally unique: assets.toml registers the
    # texture directories with a flat resource group, so a material naming
    # "door_albedo.png" gets whichever door the loader saw first. Two packs both
    # shipping a door is not a rare case -- the dungeon and cozy packs both do,
    # and so do five packs' worth of numbered variant folders -- so uniqueness
    # is built into the name rather than checked for afterwards.
    name = unique("%s_%s_albedo" % (domain, stem), names)
    rel = "textures/%s/%s.png" % (domain, name)
    out = os.path.join(ASSETS, rel)
    if not dry:
        os.makedirs(os.path.dirname(out), exist_ok=True)
        shutil.copyfile(src, out)
        sidecar(out, "texture", name.replace("_", " ").title(), [domain])
    taken[digest] = rel
    return rel


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", default=DEFAULT_MANIFEST)
    parser.add_argument("--pack", action="append", default=[])
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--report", default="")
    args = parser.parse_args()

    manifest = load_manifest(args.manifest)
    packs = [pack_defaults(p) for p in manifest.get("pack", [])]
    if args.pack:
        wanted = set(args.pack)
        packs = [p for p in packs if p["id"] in wanted]
        missing = wanted - {p["id"] for p in packs}
        if missing:
            raise SystemExit("no such pack: " + ", ".join(sorted(missing)))
    packs = [p for p in packs if not p["skinned"]]

    tmp = args.report or os.path.join(tempfile.gettempdir(),
                                      "raven_import_report.json")
    total = 0
    for pack in packs:
        total += publish_pack(pack, tmp, args.dry_run)
    print("\n%d meshes published from %d packs" % (total, len(packs)))
    return 0


def publish_pack(pack: dict, report_path: str, dry: bool) -> int:
    domain = pack["domain"]
    sources = source_models(pack)
    if not sources:
        print("%-18s no source models matched" % pack["id"])
        return 0

    mesh_dir = os.path.join(ASSETS, "meshes", domain)
    stage = os.path.join(tempfile.gettempdir(), "raven_import_" + domain)
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(stage, exist_ok=True)

    jobs = [{"source": s, "out_dir": stage,
             "scale": float(pack["model_scale"].get(
                 os.path.splitext(os.path.basename(s))[0], pack["scale"])),
             "split_objects": pack["split_objects"],
             "split_loose": pack["split_loose"],
             "bake_colours": pack["bake_colours"]} for s in sources]
    print("%-18s %d source files -> blender" % (pack["id"], len(jobs)))
    report = run_blender(jobs, report_path)
    for error in report["errors"]:
        print("  ! " + error)

    # Names come from the source stem, and from the object name only when one
    # file holds several. `Trees.fbx` holding twelve trees is the case that
    # decides this: `trees_tree_01` is noise, `tree_01` is the id.
    per_source: dict = {}
    for record in report["models"]:
        per_source.setdefault(record["source"], []).append(record)

    taken: set = set()
    textures: dict = {}
    # Basenames are globally unique, not unique per pack -- see publish_texture
    # -- so this is seeded with what every earlier pack already published.
    texture_names: set = published_texture_names()
    entries = []
    index = texture_index(pack)
    for source, records in sorted(per_source.items()):
        stem = canonical(os.path.splitext(os.path.basename(source))[0])
        for index_in_source, record in enumerate(sorted(
                records, key=lambda r: r["object"])):
            if len(records) == 1:
                name = stem
            elif pack["split_loose"]:
                # Loose parts have no names -- Blender numbers them
                # `Fruits.001` in whatever order the islands happened to be
                # stored. `_part_NN` is what docs/asset-naming.md calls that,
                # and it is at least stable across re-imports.
                name = "%s_part_%02d" % (stem, index_in_source + 1)
            else:
                objname = canonical(record["object"])
                name = objname if objname and objname != stem \
                    else "%s_%s" % (stem, objname or "part")
            name = unique(pack["prefix"] + name, taken)
            rel = "meshes/%s/%s.obj" % (domain, name)
            factor = size_factor(name, record, pack)
            if factor != 1.0:
                rescale(record, factor)
            if not dry:
                os.makedirs(mesh_dir, exist_ok=True)
                move_mesh(record["temp"], os.path.join(ASSETS, rel), factor)
                sidecar(os.path.join(ASSETS, rel), "mesh",
                        name.replace("_", " ").title(), [domain])
            image = resolve_texture(record, source, pack, index)
            texture_rel = publish_texture(image, domain, textures,
                                          texture_names, dry) if image else ""
            entries.append({
                "id": "%s.%s" % (domain, name),
                "name": name,
                "mesh": rel,
                "texture": texture_rel,
                "size": record["size"],
                "min": record["min"],
                "max": record["max"],
                "verts": record["verts"],
                "tris": record["tris"],
                "source": os.path.relpath(source, ASSETS).replace(os.sep, "/"),
            })
            if dry:
                print("  %-32s %7.3f x %7.3f x %7.3f  %5d tris  %s"
                      % (name, record["size"][0], record["size"][1],
                         record["size"][2], record["tris"],
                         os.path.basename(texture_rel or "-")))

    if not dry:
        write_materials(pack, entries)
        write_prefabs(pack, entries)
    shutil.rmtree(stage, ignore_errors=True)
    print("  %d meshes, %d textures" % (len(entries), len(set(textures.values()))))
    return len(entries)


def size_factor(name: str, record: dict, pack: dict) -> float:
    """The extra factor that makes this mesh the size its rule says it is."""
    longest = max(record["size"])
    if longest < 1e-6:
        return 1.0
    for match, length in pack["size_rules"]:
        if match in name:
            return length / longest
    return 1.0


def rescale(record: dict, factor: float) -> None:
    for key in ("size", "min", "max"):
        record[key] = [v * factor for v in record[key]]


def move_mesh(temp: str, out: str, factor: float) -> None:
    """Publish the staged OBJ, scaling positions on the way if asked.

    Rewriting `v` lines rather than re-exporting: the factor is only known
    after the mesh has been measured, and a second Blender pass to apply one
    multiplication would double the import time for every pack that uses a
    size rule.
    """
    if factor == 1.0:
        shutil.move(temp, out)
        return
    with open(temp) as stream, open(out, "w") as f:
        for line in stream:
            if not line.startswith("v "):
                f.write(line)
                continue
            parts = line.split()
            xyz = " ".join("%.6f" % (float(v) * factor) for v in parts[1:4])
            rest = (" " + " ".join(parts[4:])) if len(parts) > 4 else ""
            f.write("v %s%s\n" % (xyz, rest))
    os.remove(temp)


def published_texture_names() -> set:
    """Basenames already living under assets/textures.

    Read off disk rather than accumulated across packs in memory, so importing
    ONE pack still respects what the others published -- which is the normal
    case (`--pack weapons_modern`) and the one where an in-memory set would be
    empty and every collision would go unnoticed.
    """
    out: set = set()
    root = os.path.join(ASSETS, "textures")
    for directory, _, files in os.walk(root):
        for name in files:
            if name.lower().endswith(".png"):
                out.add(os.path.splitext(name)[0])
    return out


def texture_index(pack: dict) -> dict:
    """Every PNG in the pack, by lowercase basename.

    Built once so the "the FBX names an atlas that is not where it says"
    case -- which is every model in several of these packs -- costs one dict
    lookup rather than a tree walk per model.
    """
    root = os.path.join(ASSETS, "source", pack["source"])
    out: dict = {}
    for directory, _, files in os.walk(root):
        if "__macosx" in directory.lower():
            continue
        for name in files:
            if name.lower().endswith(".png") and not name.startswith("."):
                out.setdefault(name.lower(), os.path.join(directory, name))
    return out


def resolve_texture(record: dict, source: str, pack: dict, index: dict) -> str:
    """Which PNG this mesh samples, in descending order of how much it knows.

    An explicit `texture` in the manifest wins, because it is the only one an
    author wrote on purpose. Then what the material actually resolved to; then
    what it *named* but could not find; and only then a guess from the
    directory. Each step down is a step further from evidence, so the order
    matters more than any one rule.
    """
    override = pack.get("model_texture", {}).get(
        os.path.splitext(os.path.basename(source))[0], "")
    if override:
        candidate = os.path.join(ASSETS, "source", pack["source"], override)
        if os.path.isfile(candidate):
            return candidate
        print("  ! manifest names a missing texture: " + override)

    for texture in record["textures"]:
        if texture["image"]:
            return texture["image"]
    for texture in record["textures"]:
        found = index.get(texture.get("basename", "").lower())
        if found:
            return found
    if pack["texture_fallback"]:
        candidate = os.path.join(ASSETS, "source", pack["source"],
                                 pack["texture_fallback"])
        if os.path.isfile(candidate):
            return candidate
    return nearest_texture(source, pack)


def nearest_texture(source: str, pack: dict) -> str:
    """The PNG a model's own directory implies, walking up to the pack root.

    Vendor packs put one atlas beside the model and expect the FBX's material
    to name it. When the FBX does not -- and several of these do not, because
    they were exported with the texture unlinked -- the file next to it is the
    answer, and guessing that is better than publishing an untextured prop.
    """
    root = os.path.abspath(os.path.join(ASSETS, "source", pack["source"]))
    stem = os.path.splitext(os.path.basename(source))[0].lower()
    directory = os.path.dirname(os.path.abspath(source))

    def rank(filename: str) -> tuple:
        """Which of several PNGs beside a model is the model's own skin.

        Alphabetical order is not that: the AK's directory holds `mag.png` and
        `txt.png`, and `mag` sorts first, so every rifle in the pack imported
        wearing its magazine's texture. The pack's own naming is the signal --
        the body atlas is named after the model or after the author's word for
        "the texture", and the parts are named after the part.
        """
        name = os.path.splitext(filename)[0].lower()
        if name == stem:
            return (0, filename)          # ak.fbx -> ak.png
        if name in ("txt", "base", "albedo", "diffuse", "base_color",
                    "material_base_color", "defaultmaterial_base_color"):
            return (1, filename)          # the author's word for the atlas
        if name.startswith(stem) or stem.startswith(name):
            return (2, filename)
        # Named after a part -- a magazine, a bullet, a scope. Last, and only
        # reached when nothing else is there.
        if name in ("mag", "magazine", "bullet", "bullet_txt", "scope"):
            return (4, filename)
        return (3, filename)

    while True:
        pngs = sorted((f for f in os.listdir(directory)
                       if f.lower().endswith(".png") and
                       not f.startswith(".")), key=rank)
        if pngs:
            return os.path.join(directory, pngs[0])
        if os.path.normpath(directory) == os.path.normpath(root):
            return ""
        parent = os.path.dirname(directory)
        if parent == directory:
            return ""
        directory = parent


def write_materials(pack: dict, entries: list) -> None:
    """One material per published texture, named Owner/Domain/Name."""
    domain = pack["domain"]
    by_texture: dict = {}
    for entry in entries:
        if entry["texture"]:
            by_texture.setdefault(entry["texture"], []).append(entry["name"])

    out = os.path.join(ASSETS, "materials", "%s.mat" % domain)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        f.write("# %s -- written by tools/import_asset_pack.py.\n"
                "# Re-running the importer regenerates this file; hand edits\n"
                "# belong in the pack's manifest entry, not here.\n\n"
                % pack["label"])
        for texture, users in sorted(by_texture.items()):
            ident = material_id(pack["owner"], domain, texture)
            f.write('[material."%s"]\n' % ident)
            f.write('shader = "%s"\n' % pack["material_shader"])
            f.write('texture = "%s"\n' % os.path.basename(texture))
            # Nearest, always: this is pixel art authored at 64-256px, and
            # bilinear on it is the one setting that breaks the shipped look.
            f.write('filter = "nearest"\n')
            f.write('address = "%s"\n' % pack["material_address"])
            if pack["two_sided"]:
                f.write('cull = "none"\n')
            f.write("# %s\n\n" % ", ".join(sorted(users)[:8]))


def material_id(owner: str, domain: str, texture_rel: str) -> str:
    stem = os.path.splitext(os.path.basename(texture_rel))[0]
    if stem.endswith("_albedo"):
        stem = stem[: -len("_albedo")]
    return "%s/%s/%s" % (owner, pascal(domain), pascal(stem) or "Default")


def write_prefabs(pack: dict, entries: list) -> None:
    """The placeable definitions: what the editor lists and scenes reference.

    Sizes are measured off the exported vertices rather than copied from the
    vendor's readme, so a `size` in this file can be checked against the mesh
    without loading anything -- the same property kit.toml's hand-measured
    sizes had, now produced rather than maintained.
    """
    domain = pack["domain"]
    out = os.path.join(ASSETS, "prefabs", "%s.prefab.toml" % domain)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        f.write("# %s.\n"
                "#\n"
                "# Written by tools/import_asset_pack.py from\n"
                "#   assets/source/%s\n"
                "# Every size below is measured off the exported mesh in metres.\n"
                "# Re-running the importer regenerates this file.\n\n"
                % (pack["label"], pack["source"]))
        f.write("[library]\n")
        f.write('domain = "%s"\n' % domain)
        f.write("scale = 1.0        # meshes are published in metres\n")
        f.write('mesh_dir = "meshes/%s"\n\n' % domain)
        for entry in sorted(entries, key=lambda e: e["id"]):
            f.write("[[prefab]]\n")
            f.write('id = "%s"\n' % entry["id"])
            f.write('role = "%s"\n' % pack["role"])
            f.write('mesh = "%s"\n' % os.path.basename(entry["mesh"]))
            if entry["texture"]:
                f.write('material = "%s"\n'
                        % material_id(pack["owner"], domain, entry["texture"]))
            f.write('placement = "%s"\n' % pack["placement"])
            f.write("size = [%.4f, %.4f, %.4f]\n" % tuple(entry["size"]))
            # A mesh whose base is not at y=0 needs the offset stated, or every
            # placement sinks it into the floor by exactly that much.
            if abs(entry["min"][1]) > 1e-4:
                f.write("y_offset = %.4f\n" % entry["min"][1])
            f.write('collider = "%s"\n' % pack["collider"])
            f.write("# %d tris, from %s\n\n" % (entry["tris"], entry["source"]))


if __name__ == "__main__":
    sys.exit(main())
