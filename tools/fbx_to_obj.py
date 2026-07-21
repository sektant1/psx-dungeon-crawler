#!/usr/bin/env python3
"""Minimal binary-FBX -> extended OBJ converter (no external deps).

Companion to gltf_to_obj.py for kits that ship FBX only (e.g. the
low-poly-torches pack). Parses the binary FBX node tree directly,
extracts each Geometry's positions / normals / UVs, triangulates
polygons (fan), and emits one OBJ per mesh in the same extended format
ObjLoader reads (`vt` v-flipped like the glTF path).

Supports FBX 7.1-7.7 binary (32/64-bit node records, zlib-deflated
arrays). Mapping modes handled: ByPolygonVertex, ByVertex/ByControlPoint
(+ optional index arrays). Object-level geometric transforms are NOT
applied; meshes come out in their authored local space.

Usage: fbx_to_obj.py <src.fbx> <out-dir> [name ...]
Names (optional) override the model names, in tree order.
"""
import os
import struct
import sys
import zlib

ARRAY_TYPES = {b'f': ('f', 4), b'd': ('d', 8), b'l': ('q', 8),
               b'i': ('i', 4), b'b': ('b', 1)}
SCALAR_TYPES = {b'Y': ('h', 2), b'C': ('?', 1), b'I': ('i', 4),
                b'F': ('f', 4), b'D': ('d', 8), b'L': ('q', 8)}


def parse_node(data, pos, big):
    # 7500+: 64-bit end/prop fields and 25-byte sentinels.
    if big:
        end, nprops, plen = struct.unpack_from('<QQQ', data, pos)
        pos += 24
    else:
        end, nprops, plen = struct.unpack_from('<III', data, pos)
        pos += 12
    nlen = data[pos]
    pos += 1
    if end == 0:
        return None, pos
    name = data[pos:pos + nlen].decode('ascii', 'replace')
    pos += nlen
    props = []
    for _ in range(nprops):
        t = data[pos:pos + 1]
        pos += 1
        if t in SCALAR_TYPES:
            fmt, sz = SCALAR_TYPES[t]
            props.append(struct.unpack_from('<' + fmt, data, pos)[0])
            pos += sz
        elif t in ARRAY_TYPES:
            fmt, sz = ARRAY_TYPES[t]
            n, enc, clen = struct.unpack_from('<III', data, pos)
            pos += 12
            raw = data[pos:pos + clen]
            pos += clen
            if enc:
                raw = zlib.decompress(raw)
            props.append(list(struct.unpack('<%d%s' % (n, fmt), raw)))
        elif t in (b'S', b'R'):
            n = struct.unpack_from('<I', data, pos)[0]
            pos += 4
            props.append(data[pos:pos + n])
            pos += n
        else:
            raise ValueError('unknown FBX property type %r' % t)
    children = []
    while pos < end:
        child, pos = parse_node(data, pos, big)
        if child:
            children.append(child)
    return (name, props, children), max(pos, end)


def parse_fbx(path):
    data = open(path, 'rb').read()
    assert data[:20] == b'Kaydara FBX Binary  ', 'not a binary FBX'
    version = struct.unpack_from('<I', data, 23)[0]
    big = version >= 7500
    pos = 27
    roots = []
    while pos < len(data):
        node, pos = parse_node(data, pos, big)
        if node is None:
            break
        roots.append(node)
    return roots


def find_all(nodes, name):
    return [n for n in nodes if n[0] == name]


def child(node, name):
    for c in node[2]:
        if c[0] == name:
            return c
    return None


def layer_values(geom, layer_name, value_name, index_name, polyverts, nvert):
    """Resolve a layer element to one value index per polygon-vertex."""
    layer = child(geom, layer_name)
    if not layer:
        return None, None
    values = child(layer, value_name)[1][0]
    mapping = child(layer, 'MappingInformationType')[1][0].decode()
    ref = child(layer, 'ReferenceInformationType')[1][0].decode()
    idx_node = child(layer, index_name)
    indices = idx_node[1][0] if idx_node else None
    if mapping == 'ByPolygonVertex':
        per_pv = indices if ref == 'IndexToDirect' and indices else \
            list(range(len(polyverts)))
    elif mapping in ('ByVertice', 'ByVertex', 'ByControlPoint'):
        base = indices if ref == 'IndexToDirect' and indices else \
            list(range(nvert))
        per_pv = [base[v] for v in polyverts]
    else:
        raise ValueError('unhandled mapping mode ' + mapping)
    return values, per_pv


def convert(path, outdir, names):
    roots = parse_fbx(path)
    objects = None
    for r in roots:
        if r[0] == 'Objects':
            objects = r
    geoms = find_all(objects[2], 'Geometry')
    models = [n for n in find_all(objects[2], 'Model')
              if n[1] and b'Mesh' in n[1][-1]]
    os.makedirs(outdir, exist_ok=True)
    for gi, geom in enumerate(geoms):
        verts = child(geom, 'Vertices')[1][0]
        raw_idx = child(geom, 'PolygonVertexIndex')[1][0]
        nvert = len(verts) // 3
        # Split into polygons: negative index = last of polygon, XOR -1.
        polys, cur = [], []
        for i in raw_idx:
            if i < 0:
                cur.append(~i)
                polys.append(cur)
                cur = []
            else:
                cur.append(i)
        polyverts = [v for p in polys for v in p]
        pv_offsets = {}
        off = 0
        for pi, p in enumerate(polys):
            pv_offsets[pi] = off
            off += len(p)

        nrm_vals, nrm_pv = layer_values(geom, 'LayerElementNormal', 'Normals',
                                        'NormalsIndex', polyverts, nvert)
        uv_vals, uv_pv = layer_values(geom, 'LayerElementUV', 'UV',
                                      'UVIndex', polyverts, nvert)

        # Weld per polygon-vertex tuples into OBJ-style unique vertices.
        key_to_out = {}
        opos, ouv, onrm, faces = [], [], [], []

        def emit(pv_index, vert_index):
            key = (vert_index,
                   nrm_pv[pv_index] if nrm_pv else -1,
                   uv_pv[pv_index] if uv_pv else -1)
            if key in key_to_out:
                return key_to_out[key]
            opos.append(tuple(verts[vert_index * 3:vert_index * 3 + 3]))
            if uv_vals:
                u = uv_pv[pv_index]
                ouv.append((uv_vals[u * 2], uv_vals[u * 2 + 1]))
            else:
                ouv.append((0.0, 0.0))
            if nrm_vals:
                n = nrm_pv[pv_index]
                onrm.append(tuple(nrm_vals[n * 3:n * 3 + 3]))
            else:
                onrm.append((0.0, 1.0, 0.0))
            key_to_out[key] = len(opos) - 1
            return key_to_out[key]

        for pi, poly in enumerate(polys):
            base = pv_offsets[pi]
            out = [emit(base + k, v) for k, v in enumerate(poly)]
            for k in range(1, len(out) - 1):  # fan-triangulate
                faces.append((out[0], out[k], out[k + 1]))

        name = names[gi] if gi < len(names) else (
            models[gi][1][1].split(b'\x00')[0].decode('ascii', 'replace')
            if gi < len(models) else 'mesh%d' % gi)
        out_path = os.path.join(outdir, name + '.obj')
        with open(out_path, 'w') as f:
            f.write('# generated by fbx_to_obj.py from %s geometry %d\n' %
                    (os.path.basename(path), gi))
            for p in opos:
                f.write('v %.6f %.6f %.6f\n' % p)
            for t in ouv:
                # Same v-flip as gltf_to_obj.py: ObjLoader flips back.
                f.write('vt %.6f %.6f\n' % (t[0], 1.0 - t[1]))
            for n in onrm:
                f.write('vn %.6f %.6f %.6f\n' % n)
            for a, b, c in faces:
                f.write('f %d/%d/%d %d/%d/%d %d/%d/%d\n' %
                        (a + 1, a + 1, a + 1, b + 1, b + 1, b + 1,
                         c + 1, c + 1, c + 1))
        print('wrote', out_path, len(opos), 'verts', len(faces), 'tris')


if __name__ == '__main__':
    convert(sys.argv[1], sys.argv[2], sys.argv[3:])
