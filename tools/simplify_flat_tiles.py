#!/usr/bin/env python3
"""Rewrite a tile OBJ that is flat on one axis as a 2-triangle quad.

Detects the constant axis (values span < EPS), then emits a quad spanning the
bounding box on the other two axes, preserving the constant coordinate, UV
bounding box, first-vertex colour (extended `v x y z r g b a`), and the average
face-normal sign so winding stays correct.

Usage: simplify_flat_tiles.py <in.obj> <out.obj>
"""
import sys

EPS = 1e-3


def main():
    src, dst = sys.argv[1], sys.argv[2]
    verts, uvs, norms = [], [], []
    with open(src) as f:
        for line in f:
            p = line.split()
            if not p:
                continue
            if p[0] == 'v':
                verts.append([float(x) for x in p[1:]])
            elif p[0] == 'vt':
                uvs.append([float(x) for x in p[1:3]])
            elif p[0] == 'vn':
                norms.append([float(x) for x in p[1:4]])

    spans = [max(v[i] for v in verts) - min(v[i] for v in verts) for i in range(3)]
    const_axis = min(range(3), key=lambda i: spans[i])
    if spans[const_axis] > EPS:
        sys.exit(f"{src}: not flat on any axis (spans={spans})")
    const_val = sum(v[const_axis] for v in verts) / len(verts)
    a, b = [i for i in range(3) if i != const_axis]

    a0, a1 = min(v[a] for v in verts), max(v[a] for v in verts)
    b0, b1 = min(v[b] for v in verts), max(v[b] for v in verts)

    us = [t[0] for t in uvs] or [0.0, 1.0]
    vs = [t[1] for t in uvs] or [0.0, 1.0]
    u0, u1, v0, v1 = min(us), max(us), min(vs), max(vs)

    col = verts[0][3:7] if len(verts[0]) >= 7 else []
    nsum = sum(n[const_axis] for n in norms) if norms else 1.0
    ndir = 1.0 if nsum >= 0 else -1.0
    normal = [0.0, 0.0, 0.0]
    normal[const_axis] = ndir

    def vline(av, bv):
        coord = [0.0, 0.0, 0.0]
        coord[const_axis] = const_val
        coord[a] = av
        coord[b] = bv
        base = "v " + " ".join(f"{c:.6f}" for c in coord)
        return base + (" " + " ".join(f"{c:.4f}" for c in col) if col else "")

    with open(dst, 'w') as o:
        o.write(f"# simplified from {src} (2-tri quad, flat on axis {const_axis})\n")
        o.write(vline(a0, b0) + "\n")
        o.write(vline(a1, b0) + "\n")
        o.write(vline(a1, b1) + "\n")
        o.write(vline(a0, b1) + "\n")
        o.write(f"vt {u0:.6f} {v0:.6f}\n")
        o.write(f"vt {u1:.6f} {v0:.6f}\n")
        o.write(f"vt {u1:.6f} {v1:.6f}\n")
        o.write(f"vt {u0:.6f} {v1:.6f}\n")
        o.write(f"vn {normal[0]:.6f} {normal[1]:.6f} {normal[2]:.6f}\n")
        if ndir >= 0:
            o.write("f 1/1/1 2/2/1 3/3/1\n")
            o.write("f 1/1/1 3/3/1 4/4/1\n")
        else:
            o.write("f 1/1/1 3/3/1 2/2/1\n")
            o.write("f 1/1/1 4/4/1 3/3/1\n")


if __name__ == '__main__':
    main()
