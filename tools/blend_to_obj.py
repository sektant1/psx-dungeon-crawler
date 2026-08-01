#!/usr/bin/env python3
"""Blender .blend -> extended OBJ, in the engine's conventions.

The step that was missing from the front of the asset pipeline. `gltf_to_obj.py`
and `fbx_to_obj.py` handle downloaded packs; a model *you* author lands in
`assets/source/models/` as a .blend, and there was no path from there into the
engine that did not involve opening Blender and remembering six export settings.

Six, and each one is silently wrong in a different way:

  selection   a real .blend has the model *and* the studio -- backdrops, a light
              rig, a camera. Exporting everything gives you a mesh with a grey
              wall welded to it, which reads as "the importer is broken".
  axis        Blender is Z-up, the engine is Y-up. Getting this wrong lays the
              model on its face, and the fix looks like a rotation to author in
              rather than an export flag to change.
  modifiers   an unapplied Subsurf or Mirror exports the cage: half a raccoon.
  triangles   the OBJ loader wants triangles; an n-gon comes through as a hole.
  units       Blender metres are engine metres, but a model built at 100x
              "because it looked right in the viewport" is a wall on import.
  UVs         the engine's materials sample with v flipped (see any .material's
              uvScale), so an unflipped export is upside-down texture.

And a seventh that has nothing to do with export settings, because Blender has
no setting for it: **a low-poly model's colour usually lives in its materials,
not in a texture.** Blender shows you a coloured raccoon; the OBJ carries UVs
into an atlas that may not exist, and the engine draws it grey or -- worse --
samples whatever texture the material happens to name and paints the ears
green. So `--bake-colours` (the default) writes each face's material base
colour onto its vertices, in the extended OBJ form ObjLoader already reads
(`v x y z r g b a`) and the PSX shader already multiplies. What you saw in
Blender is what the engine draws, with no texture and no atlas.

This script fixes all seven and prints what it did, so the numbers a kit.toml
entry needs -- the size, in the units the engine will see -- come out of the
conversion instead of being measured by hand afterwards.

Usage:
  tools/blend_to_obj.py <src.blend> <out-dir> [--object NAME] [--scale S]
                        [--name OUT] [--ground-center] [--list]

  --list      print the meshes in the file and exit. Start here: a downloaded
              .blend rarely names its subject in English.
  --object    which mesh to export (default: the largest, which is the subject
              often enough to be a useful default and is always reported).
  --scale     multiply positions by this on the way out.
  --name      output stem; defaults to the object name, lowercased.
  --ground-center
              move the exported mesh so its footprint centre is x/z zero and
              its lowest point is y zero in engine coordinates.
  --ground-center-from OBJ
              apply another OBJ's ground-centre pivot. Split meshes exported
              from one source then retain their shared local origin.
  --material-index N
              export only faces assigned to material slot N after modifiers.
  --no-bake-colours
              write a plain OBJ instead. For a model that really is textured,
              where per-vertex colour would multiply the texture darker.

Requires the `blender` CLI on PATH. Everything below the argument parsing runs
*inside* Blender, because that is the only thing that can read a .blend.
"""
import argparse
import os
import subprocess
import sys

# The half of this file that runs inside Blender. Passed with --python-expr so
# the tool stays one file: a second script beside it is one more thing to move.
BLENDER_SCRIPT = r'''
import bpy, json, os, sys, math

argv = json.loads(os.environ["PSX_BLEND_ARGS"])

meshes = [o for o in bpy.data.objects if o.type == "MESH"]
if not meshes:
    print("PSX_ERROR no mesh objects in this .blend")
    sys.exit(1)

def volume(o):
    d = o.dimensions
    return max(d.x, 1e-6) * max(d.y, 1e-6) * max(d.z, 1e-6)

if argv["list"]:
    for o in sorted(meshes, key=volume, reverse=True):
        d = o.dimensions
        print("PSX_MESH %-24s %8.3f x %8.3f x %8.3f  (%d verts)"
              % (o.name, d.x, d.y, d.z, len(o.data.vertices)))
    sys.exit(0)

if argv["object"]:
    chosen = next((o for o in meshes if o.name == argv["object"]), None)
    if chosen is None:
        print("PSX_ERROR no mesh called '%s'" % argv["object"])
        print("PSX_ERROR available: %s" % ", ".join(o.name for o in meshes))
        sys.exit(1)
else:
    # The largest mesh. A studio .blend is the subject plus backdrops, and the
    # subject is usually the biggest thing that is not a wall -- a guess, which
    # is why it is always printed.
    chosen = max(meshes, key=volume)
    print("PSX_NOTE picked the largest mesh: '%s' (--list to see the rest)"
          % chosen.name)

# Files are often saved in Edit, Sculpt or Texture Paint mode. Selection
# operators have no valid object context there in background Blender, so make
# object mode explicit before choosing export subject.
if bpy.context.object and bpy.context.object.mode != "OBJECT":
    bpy.ops.object.mode_set(mode="OBJECT")
bpy.ops.object.select_all(action="DESELECT")
chosen.select_set(True)
bpy.context.view_layer.objects.active = chosen

if argv["material_index"] >= 0:
    import bmesh
    depsgraph = bpy.context.evaluated_depsgraph_get()
    evaluated = chosen.evaluated_get(depsgraph)
    mesh = bpy.data.meshes.new_from_object(evaluated, depsgraph=depsgraph)
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.delete(
        bm,
        geom=[face for face in bm.faces
              if face.material_index != argv["material_index"]],
        context="FACES",
    )
    bmesh.ops.delete(bm, geom=[vert for vert in bm.verts if not vert.link_faces],
                     context="VERTS")
    bm.to_mesh(mesh)
    bm.free()
    split = bpy.data.objects.new(chosen.name + "_split", mesh)
    bpy.context.collection.objects.link(split)
    split.matrix_world = chosen.matrix_world
    chosen.select_set(False)
    split.select_set(True)
    bpy.context.view_layer.objects.active = split
    chosen = split

out = argv["out"]

if not argv["bake"]:
    # Modifiers are applied by the exporter, so a Subsurf or a Mirror comes
    # through as geometry rather than as a cage.
    bpy.ops.wm.obj_export(
        filepath=out,
        export_selected_objects=True,
        export_uv=True,
        export_normals=True,
        export_materials=False,   # the engine names materials in kit.toml
        export_triangulated_mesh=True,
        apply_modifiers=True,
        # Blender is Z-up / -Y-forward; the engine is Y-up / -Z-forward. These
        # two are the whole axis conversion, and getting them wrong lays the
        # model on its face in a way that looks like bad authoring.
        forward_axis="NEGATIVE_Z",
        up_axis="Y",
        global_scale=argv["scale"],
    )
else:
    # Written by hand, because Blender's OBJ exporter cannot emit the extended
    # `v x y z r g b a` form -- and that form is the whole point: it is what
    # carries a low-poly model's material colours into an engine that has no
    # texture for it.
    import bmesh
    from mathutils import Matrix

    depsgraph = bpy.context.evaluated_depsgraph_get()
    evaluated = chosen.evaluated_get(depsgraph)   # modifiers applied
    mesh = evaluated.to_mesh()

    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])  # the OBJ loader wants tris
    bm.to_mesh(mesh)
    bm.free()
    mesh.calc_normals_split()

    # Object transform, then Blender -> engine axes, then the scale. Baked into
    # the positions rather than left for the scene to apply: a converted mesh
    # that only looks right under one node transform is not an asset.
    s = argv["scale"]
    axis = Matrix(((s, 0, 0, 0), (0, 0, s, 0), (0, -s, 0, 0), (0, 0, 0, 1)))
    xform = axis @ chosen.matrix_world
    normal_xform = xform.to_3x3().inverted_safe().transposed()

    # sRGB -> linear. The shader multiplies vColour into an already-linear
    # pipeline (see psx.frag), so handing it Blender's sRGB swatch directly
    # comes out visibly washed out.
    def to_linear(c):
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

    colours = []
    for slot in evaluated.material_slots:
        m = slot.material
        rgba = (1.0, 1.0, 1.0, 1.0)
        if m:
            if m.use_nodes:
                bsdf = next((n for n in m.node_tree.nodes
                             if n.type == "BSDF_PRINCIPLED"), None)
                if bsdf:
                    rgba = tuple(bsdf.inputs["Base Color"].default_value)
            else:
                rgba = tuple(m.diffuse_color)
        colours.append((to_linear(rgba[0]), to_linear(rgba[1]),
                        to_linear(rgba[2]), rgba[3]))
    if not colours:
        colours = [(1.0, 1.0, 1.0, 1.0)]

    uv_layer = mesh.uv_layers.active

    # One OBJ vertex per (position, uv, normal, colour) combination: a corner
    # where two differently-coloured faces meet needs two, or the colour bleeds
    # across the seam.
    verts, uvs, norms, index = [], [], [], {}
    faces = []
    for poly in mesh.polygons:
        colour = colours[min(poly.material_index, len(colours) - 1)]
        tri = []
        for loop_index in poly.loop_indices:
            loop = mesh.loops[loop_index]
            co = xform @ mesh.vertices[loop.vertex_index].co
            no = (normal_xform @ loop.normal).normalized()
            uv = uv_layer.data[loop_index].uv if uv_layer else (0.0, 0.0)
            key = (round(co.x, 6), round(co.y, 6), round(co.z, 6),
                   round(uv[0], 6), round(uv[1], 6),
                   round(no.x, 4), round(no.y, 4), round(no.z, 4), colour)
            slot = index.get(key)
            if slot is None:
                slot = len(verts) + 1  # OBJ indices are 1-based
                index[key] = slot
                verts.append((co, colour))
                # v flipped here, so the file matches what gltf_to_obj emits and
                # every material in this engine can leave uvScale alone.
                uvs.append((uv[0], 1.0 - uv[1]))
                norms.append(no)
            tri.append(slot)
        faces.append(tri)

    with open(out, "w") as f:
        f.write("# %s -- baked by tools/blend_to_obj.py\n" % chosen.name)
        f.write("# extended OBJ: v x y z r g b a (linear vertex colour)\n")
        f.write("o %s\n" % chosen.name.replace(" ", "_"))
        for co, c in verts:
            f.write("v %.6f %.6f %.6f %.4f %.4f %.4f %.4f\n"
                    % (co.x, co.y, co.z, c[0], c[1], c[2], c[3]))
        for uv in uvs:
            f.write("vt %.6f %.6f\n" % (uv[0], uv[1]))
        for n in norms:
            f.write("vn %.6f %.6f %.6f\n" % (n.x, n.y, n.z))
        for tri in faces:
            f.write("f %s\n" % " ".join("%d/%d/%d" % (i, i, i) for i in tri))

    print("PSX_BAKED %d materials -> vertex colours" % len(colours))
    evaluated.to_mesh_clear()

d = chosen.dimensions
s = argv["scale"]
# Reported in the axis order the engine will see, so the numbers can be pasted
# straight into a kit.toml `size`.
print("PSX_SIZE %.4f %.4f %.4f" % (d.x * s, d.z * s, d.y * s))
print("PSX_VERTS %d" % len(chosen.data.vertices))
print("PSX_OK %s" % out)
'''


def obj_pivot(path: str) -> tuple[float, float, float]:
    with open(path, "r", encoding="utf-8") as stream:
        positions = [tuple(float(value) for value in line.split()[1:4])
                     for line in stream if line.startswith("v ")]
    if not positions:
        raise ValueError("OBJ contains no positions: %s" % path)
    minimum = [min(p[axis] for p in positions) for axis in range(3)]
    maximum = [max(p[axis] for p in positions) for axis in range(3)]
    return ((minimum[0] + maximum[0]) * 0.5, minimum[1],
            (minimum[2] + maximum[2]) * 0.5)


def ground_center_obj(path: str, reference: str = "") -> None:
    """Normalize final exported coordinates, including evaluated modifiers."""
    with open(path, "r", encoding="utf-8") as stream:
        lines = stream.readlines()
    pivot = obj_pivot(reference or path)
    output = []
    for line in lines:
        if not line.startswith("v "):
            output.append(line)
            continue
        values = line.split()
        xyz = [float(values[i + 1]) - pivot[i] for i in range(3)]
        suffix = " " + " ".join(values[4:]) if len(values) > 4 else ""
        output.append("v %.6f %.6f %.6f%s\n" %
                      (xyz[0], xyz[1], xyz[2], suffix))
    with open(path, "w", encoding="utf-8") as stream:
        stream.writelines(output)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("blend")
    parser.add_argument("outdir")
    parser.add_argument("--object", default="")
    parser.add_argument("--name", default="")
    parser.add_argument("--scale", type=float, default=1.0)
    parser.add_argument("--ground-center", action="store_true")
    parser.add_argument("--ground-center-from", default="")
    parser.add_argument("--material-index", type=int, default=-1)
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--no-bake-colours", dest="bake", action="store_false")
    parser.set_defaults(bake=True)
    args = parser.parse_args()

    if not os.path.isfile(args.blend):
        print("no such .blend: %s" % args.blend, file=sys.stderr)
        return 1

    stem = args.name or (args.object or os.path.splitext(
        os.path.basename(args.blend))[0])
    stem = stem.lower().replace(" ", "_")
    out = os.path.abspath(os.path.join(args.outdir, stem + ".obj"))
    if not args.list:
        os.makedirs(args.outdir, exist_ok=True)

    env = dict(os.environ)
    env["PSX_BLEND_ARGS"] = repr({
        "object": args.object,
        "out": out,
        "scale": args.scale,
        "ground_center": args.ground_center,
        "material_index": args.material_index,
        "list": args.list,
        "bake": args.bake,
    }).replace("'", '"').replace("True", "true").replace("False", "false")

    proc = subprocess.run(
        ["blender", "-b", args.blend, "--python-expr", BLENDER_SCRIPT],
        capture_output=True, text=True, env=env)

    ok = False
    for line in (proc.stdout + proc.stderr).splitlines():
        if line.startswith("PSX_ERROR"):
            print(line[len("PSX_ERROR "):], file=sys.stderr)
        elif line.startswith("PSX_MESH"):
            print(line[len("PSX_MESH "):])
        elif line.startswith("PSX_NOTE"):
            print(line[len("PSX_NOTE "):])
        elif line.startswith("PSX_SIZE"):
            x, y, z = line.split()[1:4]
            # The three numbers a kit.toml entry wants, in its own syntax, so
            # the next step is a paste rather than a measurement.
            print("size = [%s, %s, %s]   # metres, for kit.toml" % (x, y, z))
        elif line.startswith("PSX_BAKED"):
            print(line[len("PSX_BAKED "):])
        elif line.startswith("PSX_VERTS"):
            print("%s vertices" % line.split()[1])
        elif line.startswith("PSX_OK"):
            ok = True
            print("wrote %s" % line.split(None, 1)[1])

    if args.list:
        return proc.returncode
    if not ok:
        # Blender's own output is long and mostly irrelevant; the tail is where
        # the actual failure is.
        print("blender failed; last output:", file=sys.stderr)
        for line in (proc.stdout + proc.stderr).splitlines()[-12:]:
            print("  " + line, file=sys.stderr)
        return proc.returncode or 1
    if args.ground_center or args.ground_center_from:
        ground_center_obj(out, args.ground_center_from)
        print("ground-centred final evaluated mesh")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
