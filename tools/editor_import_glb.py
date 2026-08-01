#!/usr/bin/env python3
"""Import one GLB as engine OBJ parts, textures, materials, and kit entries."""

import argparse
import base64
import json
import math
import os
import re
import shutil
import sys

from gltf_to_obj import load_gltf, read_accessor


def slug(value):
    value = re.sub(r"[^a-z0-9]+", "_", value.lower()).strip("_")
    return value or "model"


def identity():
    return [1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            0.0, 0.0, 0.0, 1.0]


def multiply(a, b):
    out = [0.0] * 16
    for col in range(4):
        for row in range(4):
            out[col * 4 + row] = sum(
                a[k * 4 + row] * b[col * 4 + k] for k in range(4))
    return out


def local_matrix(node):
    if "matrix" in node:
        return [float(v) for v in node["matrix"]]
    tx, ty, tz = node.get("translation", [0.0, 0.0, 0.0])
    x, y, z, w = node.get("rotation", [0.0, 0.0, 0.0, 1.0])
    sx, sy, sz = node.get("scale", [1.0, 1.0, 1.0])
    xx, yy, zz = x * x, y * y, z * z
    xy, xz, yz = x * y, x * z, y * z
    wx, wy, wz = w * x, w * y, w * z
    return [
        (1.0 - 2.0 * (yy + zz)) * sx,
        (2.0 * (xy + wz)) * sx,
        (2.0 * (xz - wy)) * sx,
        0.0,
        (2.0 * (xy - wz)) * sy,
        (1.0 - 2.0 * (xx + zz)) * sy,
        (2.0 * (yz + wx)) * sy,
        0.0,
        (2.0 * (xz + wy)) * sz,
        (2.0 * (yz - wx)) * sz,
        (1.0 - 2.0 * (xx + yy)) * sz,
        0.0,
        tx, ty, tz, 1.0,
    ]


def point(matrix, value):
    x, y, z = value[:3]
    return (matrix[0] * x + matrix[4] * y + matrix[8] * z + matrix[12],
            matrix[1] * x + matrix[5] * y + matrix[9] * z + matrix[13],
            matrix[2] * x + matrix[6] * y + matrix[10] * z + matrix[14])


def direction(matrix, value):
    x, y, z = value[:3]
    # Inverse-transpose of upper-left 3x3, preserving normals under scale.
    a, b, c = matrix[0], matrix[4], matrix[8]
    d, e, f = matrix[1], matrix[5], matrix[9]
    g, h, i = matrix[2], matrix[6], matrix[10]
    det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)
    if abs(det) < 1e-12:
        return (0.0, 1.0, 0.0)
    inv = [
        (e * i - f * h) / det, (c * h - b * i) / det,
        (b * f - c * e) / det, (f * g - d * i) / det,
        (a * i - c * g) / det, (c * d - a * f) / det,
        (d * h - e * g) / det, (b * g - a * h) / det,
        (a * e - b * d) / det,
    ]
    nx = inv[0] * x + inv[3] * y + inv[6] * z
    ny = inv[1] * x + inv[4] * y + inv[7] * z
    nz = inv[2] * x + inv[5] * y + inv[8] * z
    length = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
    return nx / length, ny / length, nz / length


def mesh_instances(document):
    nodes = document.get("nodes", [])
    scenes = document.get("scenes", [])
    roots = []
    if scenes:
        scene = document.get("scene", 0)
        roots = scenes[scene].get("nodes", [])
    if not roots:
        children = {child for node in nodes for child in node.get("children", [])}
        roots = [index for index in range(len(nodes)) if index not in children]

    result = []

    def visit(index, parent):
        node = nodes[index]
        world = multiply(parent, local_matrix(node))
        if "mesh" in node:
            result.append((node["mesh"], world,
                           node.get("name") or "mesh_%d" % node["mesh"]))
        for child in node.get("children", []):
            visit(child, world)

    for root in roots:
        visit(root, identity())
    if not result:
        result = [(index, identity(), mesh.get("name") or "mesh_%d" % index)
                  for index, mesh in enumerate(document.get("meshes", []))]
    return result


def image_payload(document, blob, source, glb_path):
    image = document["images"][source]
    mime = image.get("mimeType", "")
    if "bufferView" in image:
        view = document["bufferViews"][image["bufferView"]]
        start = view.get("byteOffset", 0)
        data = blob[start:start + view["byteLength"]]
    else:
        uri = image.get("uri", "")
        if uri.startswith("data:"):
            header, encoded = uri.split(",", 1)
            mime = header.split(";", 1)[0][5:]
            data = base64.b64decode(encoded)
        elif uri:
            with open(os.path.join(os.path.dirname(glb_path), uri), "rb") as stream:
                data = stream.read()
            if not mime:
                mime = "image/" + os.path.splitext(uri)[1].lstrip(".")
        else:
            raise ValueError("image %d has no payload" % source)
    extensions = {"image/png": ".png", "image/jpeg": ".jpg"}
    if mime not in extensions:
        raise ValueError("unsupported base-colour image type '%s'" % mime)
    return data, extensions[mime]


def material_info(document, primitive):
    if "material" not in primitive:
        return [1.0, 1.0, 1.0, 1.0], None, False
    material = document["materials"][primitive["material"]]
    pbr = material.get("pbrMetallicRoughness", {})
    factor = pbr.get("baseColorFactor", [1.0, 1.0, 1.0, 1.0])
    texture = pbr.get("baseColorTexture")
    source = None
    if texture:
        source = document["textures"][texture["index"]]["source"]
    return factor, source, material.get("doubleSided", False)


def collect_parts(document, blob):
    parts = []
    for mesh_index, matrix, node_name in mesh_instances(document):
        mesh = document["meshes"][mesh_index]
        for primitive_index, primitive in enumerate(mesh.get("primitives", [])):
            if primitive.get("mode", 4) != 4:
                raise ValueError("only triangle primitives are supported")
            attributes = primitive.get("attributes", {})
            if "POSITION" not in attributes:
                raise ValueError("primitive has no POSITION accessor")
            positions = [point(matrix, p)
                         for p in read_accessor(document, blob,
                                                attributes["POSITION"])]
            normals = ([direction(matrix, n)
                        for n in read_accessor(document, blob,
                                               attributes["NORMAL"])]
                       if "NORMAL" in attributes else
                       [(0.0, 1.0, 0.0)] * len(positions))
            uvs = (read_accessor(document, blob, attributes["TEXCOORD_0"])
                   if "TEXCOORD_0" in attributes else
                   [(0.0, 0.0)] * len(positions))
            colours = (read_accessor(document, blob, attributes["COLOR_0"])
                       if "COLOR_0" in attributes else
                       [(1.0, 1.0, 1.0, 1.0)] * len(positions))
            factor, image, double_sided = material_info(document, primitive)
            baked_colours = []
            for colour in colours:
                rgba = tuple(colour) + (1.0,) * (4 - len(colour))
                baked_colours.append(tuple(rgba[i] * factor[i] for i in range(4)))
            indices = ([item[0] for item in read_accessor(
                            document, blob, primitive["indices"])]
                       if "indices" in primitive else list(range(len(positions))))
            if len(indices) % 3:
                raise ValueError("triangle index count is not divisible by three")
            parts.append({
                "name": "%s_%d" % (node_name, primitive_index),
                "positions": positions, "normals": normals, "uvs": uvs,
                "colours": baked_colours, "indices": indices,
                "image": image, "double_sided": double_sided,
                "has_vertex_colours": "COLOR_0" in attributes,
            })
    if not parts:
        raise ValueError("GLB contains no mesh primitives")
    return parts


def write_obj(path, part, pivot):
    with open(path, "w", encoding="utf-8") as stream:
        stream.write("# generated by tools/editor_import_glb.py\n")
        for position, colour in zip(part["positions"], part["colours"]):
            p = tuple(position[i] - pivot[i] for i in range(3))
            stream.write("v %.6f %.6f %.6f %.6f %.6f %.6f %.6f\n" %
                         (p + tuple(colour)))
        for uv in part["uvs"]:
            stream.write("vt %.6f %.6f\n" % (uv[0], 1.0 - uv[1]))
        for normal in part["normals"]:
            stream.write("vn %.6f %.6f %.6f\n" % tuple(normal))
        for offset in range(0, len(part["indices"]), 3):
            values = [part["indices"][offset + i] + 1 for i in range(3)]
            stream.write("f %d/%d/%d %d/%d/%d %d/%d/%d\n" %
                         (values[0], values[0], values[0],
                          values[1], values[1], values[1],
                          values[2], values[2], values[2]))


def replace_catalog_block(path, model_slug, pieces):
    with open(path, encoding="utf-8") as stream:
        text = stream.read()
    begin = "# BEGIN editor import: %s" % model_slug
    end = "# END editor import: %s" % model_slug
    rows = ["", begin]
    for piece in pieces:
        rows.extend([
            "[[piece]]", 'id = "%s"' % piece["id"],
            'role = "imported_model"', 'mesh = "%s"' % piece["mesh"],
            'material = "%s"' % piece["material"], 'socket = "prop"',
            "import_scale = 1.0",
            "size = [%.4f, %.4f, %.4f]" % tuple(piece["size"]), "",
        ])
    rows.append(end)
    block = "\n".join(rows) + "\n"
    pattern = re.compile(r"\n?# BEGIN editor import: %s\n.*?"
                         r"# END editor import: %s\n?" %
                         (re.escape(model_slug), re.escape(model_slug)), re.S)
    text = pattern.sub("\n", text).rstrip() + block
    with open(path, "w", encoding="utf-8") as stream:
        stream.write(text)


def import_glb(source, asset_root, manifest_path):
    if os.path.splitext(source)[1].lower() != ".glb":
        raise ValueError("model import accepts .glb files")
    document, blob = load_gltf(source)
    parts = collect_parts(document, blob)
    model_slug = slug(os.path.splitext(os.path.basename(source))[0])
    prefix = "import_%s" % model_slug

    all_positions = [position for part in parts for position in part["positions"]]
    minimum = [min(p[axis] for p in all_positions) for axis in range(3)]
    maximum = [max(p[axis] for p in all_positions) for axis in range(3)]
    model_pivot = ((minimum[0] + maximum[0]) * 0.5, minimum[1],
                   (minimum[2] + maximum[2]) * 0.5)

    mesh_dir = os.path.join(asset_root, "meshes", "props")
    texture_dir = os.path.join(asset_root, "textures")
    material_dir = os.path.join(asset_root, "materials")
    os.makedirs(mesh_dir, exist_ok=True)
    os.makedirs(texture_dir, exist_ok=True)
    os.makedirs(material_dir, exist_ok=True)

    images = {}
    material_rows = []
    catalog_pieces = []
    manifest_parts = []
    for index, part in enumerate(parts):
        part_id = prefix if len(parts) == 1 else "%s_p%d" % (prefix, index)
        obj_name = part_id + ".obj"
        part_min = [min(p[axis] for p in part["positions"]) for axis in range(3)]
        part_max = [max(p[axis] for p in part["positions"]) for axis in range(3)]
        part_pivot = ((part_min[0] + part_max[0]) * 0.5, part_min[1],
                      (part_min[2] + part_max[2]) * 0.5)
        part_size = [part_max[axis] - part_min[axis] for axis in range(3)]
        write_obj(os.path.join(mesh_dir, obj_name), part, part_pivot)
        material = ("Game/PropVertexColour" if part["has_vertex_colours"] else
                    "Engine/Psx/PrototypeSurface")
        if part["image"] is not None:
            source_index = part["image"]
            if source_index not in images:
                payload, extension = image_payload(document, blob, source_index,
                                                   source)
                image_name = "%s_image%d%s" % (prefix, source_index, extension)
                with open(os.path.join(texture_dir, image_name), "wb") as stream:
                    stream.write(payload)
                images[source_index] = image_name
            image_name = images[source_index]
            material = "Game/%s/part%d" % (prefix, index)
            cull = "        cull_hardware none\n" if part["double_sided"] else ""
            material_rows.append(
                "material %s\n{\n    technique { pass {\n%s"
                "        vertex_program_ref PSX_VS_Lit { }\n"
                "        fragment_program_ref PSX_FS_Lit { }\n"
                "        texture_unit\n        {\n"
                "            texture %s\n            filtering none\n"
                "            tex_address_mode wrap\n        }\n    } }\n}\n" %
                (material, cull, image_name))
        piece = {
            "id": part_id,
            "mesh": "meshes/props/" + obj_name,
            "material": material,
            "size": part_size,
        }
        catalog_pieces.append(piece)
        manifest_parts.append({"prefab": "kit." + part_id,
                               "name": part["name"],
                               "offset": [part_pivot[axis] - model_pivot[axis]
                                          for axis in range(3)]})

    material_path = ""
    if material_rows:
        material_path = os.path.join(material_dir, prefix + ".material")
        with open(material_path, "w", encoding="utf-8") as stream:
            stream.write("\n".join(material_rows))

    replace_catalog_block(os.path.join(asset_root, "config", "kit.toml"),
                          model_slug, catalog_pieces)
    with open(manifest_path, "w", encoding="utf-8") as stream:
        json.dump({"material_script": material_path,
                   "parts": manifest_parts}, stream, indent=2)
        stream.write("\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source")
    parser.add_argument("asset_root")
    parser.add_argument("manifest")
    args = parser.parse_args()
    try:
        import_glb(os.path.abspath(args.source), os.path.abspath(args.asset_root),
                   os.path.abspath(args.manifest))
    except (KeyError, IndexError, OSError, ValueError, AssertionError) as error:
        print("GLB import failed: %s" % error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
