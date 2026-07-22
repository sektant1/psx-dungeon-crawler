#!/usr/bin/env python3
"""Re-tessellate a tile OBJ that is flat on one axis as a low-density grid.

The kit tiles are flat panels emitted with hundreds of coplanar triangles. A
single 2-triangle quad is too coarse for this project's PSX shader, which
*deliberately* affine-interpolates UVs (see psx.vert): long triangles then swim
and stretch near the screen corners. So we emit a modest NxN grid instead --
far fewer triangles than the source, but short enough edges that the affine
warp stays controlled.

Winding is derived from the source normal direction and verified against each
emitted triangle's geometric normal, so the quads face the room (a mis-wound
floor gets back-face culled and reads as a black hole).

Usage: simplify_flat_tiles.py <in.obj> <out.obj> [subdivisions=8]
"""
import sys

EPS = 1e-3


def main():
    src, dst = sys.argv[1], sys.argv[2]
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 8

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
    nsum = sum(nm[const_axis] for nm in norms) if norms else 1.0
    ndir = 1.0 if nsum >= 0 else -1.0
    normal = [0.0, 0.0, 0.0]
    normal[const_axis] = ndir

    def pos(i, j):
        av = a0 + (a1 - a0) * (i / n)
        bv = b0 + (b1 - b0) * (j / n)
        c = [0.0, 0.0, 0.0]
        c[const_axis] = const_val
        c[a] = av
        c[b] = bv
        return c

    # Build the (n+1)x(n+1) vertex grid; OBJ indices are 1-based, row-major.
    def vid(i, j):
        return i * (n + 1) + j + 1

    def cross_axis(p0, p1, p2):
        # const-axis component of (p1-p0) x (p2-p0)
        e1 = [p1[k] - p0[k] for k in range(3)]
        e2 = [p2[k] - p0[k] for k in range(3)]
        cx = [e1[1] * e2[2] - e1[2] * e2[1],
              e1[2] * e2[0] - e1[0] * e2[2],
              e1[0] * e2[1] - e1[1] * e2[0]]
        return cx[const_axis]

    # Per-cell triangles, offsets into the 2x2 corner block:
    #   T1 = (0,0)(1,0)(1,1)   T2 = (0,0)(1,1)(0,1)
    # Decide once whether to reverse winding so the geometric normal matches
    # ndir (Ogre culls clockwise; a mis-wound floor is culled -> black).
    tri1 = [(0, 0), (1, 0), (1, 1)]
    tri2 = [(0, 0), (1, 1), (0, 1)]
    p = [pos(di, dj) for (di, dj) in tri1]
    if (cross_axis(p[0], p[1], p[2]) >= 0) != (ndir >= 0):
        tri1 = [tri1[0], tri1[2], tri1[1]]
        tri2 = [tri2[0], tri2[2], tri2[1]]

    with open(dst, 'w') as o:
        o.write(f"# re-tessellated from {src} "
                f"({n}x{n} grid, flat on axis {const_axis})\n")
        for i in range(n + 1):
            for j in range(n + 1):
                c = pos(i, j)
                base = "v " + " ".join(f"{x:.6f}" for x in c)
                if col:
                    base += " " + " ".join(f"{x:.4f}" for x in col)
                o.write(base + "\n")
        for i in range(n + 1):
            for j in range(n + 1):
                u = u0 + (u1 - u0) * (i / n)
                v = v0 + (v1 - v0) * (j / n)
                o.write(f"vt {u:.6f} {v:.6f}\n")
        o.write(f"vn {normal[0]:.6f} {normal[1]:.6f} {normal[2]:.6f}\n")

        def emit(ci, cj, tri):
            refs = [f"{vid(ci + di, cj + dj)}/{vid(ci + di, cj + dj)}/1"
                    for (di, dj) in tri]
            o.write("f " + " ".join(refs) + "\n")

        for i in range(n):
            for j in range(n):
                emit(i, j, tri1)
                emit(i, j, tri2)


if __name__ == '__main__':
    main()
