#!/usr/bin/env python3
"""Reorders the forest's OBJ meshes for the GPU's vertex cache (and fetch / overdraw), in place.

    pip install numpy meshoptimizer
    python3 tools/optimize_meshes.py [--dry-run] [--threshold 1.05] [files...]   # default: assets/models/*.obj

Run it after convert_assets.py. It does not change what is drawn: the same triangles with the same
winding, positions, normals and UVs, only shared and in a different order.

What it does, per mesh:
  1. merges identical v/vt/vn corners, so a vertex used by several triangles is stored (and shaded) once;
  2. meshoptimizer.optimize_vertex_cache: a triangle order that keeps recently shaded vertices in the
     GPU's post-transform cache;
  3. meshoptimizer.optimize_overdraw: within that, draws the triangles likeliest to hide others first
     (limited by --threshold: how much cache efficiency it may give up, 1.05 = 5%);
  4. meshoptimizer.optimize_vertex_fetch: vertices numbered in the order they are first used, so the
     vertex buffer is read front to back.

The output starts with the line `#componeng_indexed` (a comment to any other OBJ reader), which tells
the engine's loader (Mesh::generateFromWavefrontObj, kIndexedMarker) that entry i of v, vt and vn
together are vertex i and that faces name vertices (`f 1/1/1 2/2/2 3/3/3`). Without that line the
loader still makes one vertex per triangle corner, as it does for any plain OBJ, so a plain file keeps
working and a marked one costs a fraction of the vertex shading.
"""
import argparse
import glob
import multiprocessing
import os

import numpy as np
import meshoptimizer as mo

MARKER = "#componeng_indexed"


def parse(path):
    vs, vts, vns, faces = [], [], [], []
    other = set()
    already = False
    with open(path) as f:
        for line in f:
            if line.startswith("v "):
                vs.append(line[2:].strip())
            elif line.startswith("vt "):
                vts.append(line[3:].strip())
            elif line.startswith("vn "):
                vns.append(line[3:].strip())
            elif line.startswith("f "):
                idx = [tuple(int(x) - 1 for x in c.split("/")) for c in line.split()[1:]]
                for k in range(1, len(idx) - 1):      # fan, as the engine's loader does for quads
                    faces.append((idx[0], idx[k], idx[k + 1]))
            elif line.startswith(MARKER):
                already = True
            elif line.strip() and not line.startswith("#"):
                other.add(line.split()[0])
    return vs, vts, vns, np.array(faces, dtype=np.int64).reshape(-1, 3, 3), other, already


def acmr(indices, cache=16):
    """Average cache miss ratio of a FIFO post-transform cache: vertex shader runs per triangle."""
    fifo, misses = [], 0
    for i in indices.tolist():
        if i not in fifo:
            misses += 1
            fifo.append(i)
            if len(fifo) > cache:
                fifo.pop(0)
    return misses / (len(indices) / 3)


def optimize(path, threshold, dry):
    vs, vts, vns, faces, other, already = parse(path)
    tris = len(faces)
    corners = faces.reshape(-1, 3)                      # (3 * tris, [v, vt, vn])
    uniq, inverse = np.unique(corners, axis=0, return_inverse=True)
    indices = inverse.astype(np.uint32).reshape(-1)
    nverts = len(uniq)

    pos = np.array([[float(x) for x in vs[p].split()[:3]] for p in uniq[:, 0]], dtype=np.float32)

    before = acmr(np.arange(3 * tris, dtype=np.uint32))   # the soup a plain OBJ becomes
    cached = np.zeros_like(indices)
    mo.optimize_vertex_cache(cached, indices, len(indices), nverts)
    out = np.zeros_like(indices)
    mo.optimize_overdraw(out, cached, pos, len(indices), nverts, pos.strides[0], threshold)
    # Fetch order: renumber the vertices by first use (`out` is rewritten to the new numbering).
    ids = np.arange(nverts, dtype=np.uint32)
    new_ids = np.zeros_like(ids)
    used = mo.optimize_vertex_fetch(new_ids, out, ids, len(out), nverts, ids.itemsize)
    assert used == nverts
    after = acmr(out)

    if not dry:
        tmp = path + ".tmp"
        with open(tmp, "w") as f:
            f.write(MARKER + "\n")
            f.write("".join("v %s\n" % vs[uniq[i, 0]] for i in new_ids))
            f.write("".join("vt %s\n" % vts[uniq[i, 1]] for i in new_ids))
            f.write("".join("vn %s\n" % vns[uniq[i, 2]] for i in new_ids))
            tri = out.reshape(-1, 3) + 1
            f.write("".join("f %d/%d/%d %d/%d/%d %d/%d/%d\n" % (a, a, a, b, b, b, c, c, c)
                            for a, b, c in tri.tolist()))
        os.replace(tmp, path)
    return os.path.basename(path), tris, nverts, before, after, other


def work(args):
    return optimize(*args)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="*")
    ap.add_argument("--threshold", type=float, default=1.05)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("-j", type=int, default=os.cpu_count())
    a = ap.parse_args()
    here = os.path.dirname(os.path.abspath(__file__))
    files = a.files or sorted(glob.glob(os.path.join(here, "..", "assets", "models", "*.obj")))
    with multiprocessing.Pool(a.j) as pool:
        results = pool.map(work, [(f, a.threshold, a.dry_run) for f in files], chunksize=1)
    soup = unique = 0
    print("%-28s %9s %12s %10s %8s %8s" % ("mesh", "triangles", "verts (soup)", "verts now", "ACMR was", "ACMR now"))
    for name, tris, nverts, b, af, other in results:
        soup += 3 * tris
        unique += nverts
        print("%-28s %9d %12d %10d %8.2f %8.2f%s" % (name, tris, 3 * tris, nverts, b, af,
              ("   (ignored lines: %s)" % ",".join(sorted(other))) if other else ""))
    print("vertices uploaded: %d -> %d (%.0f%%)" % (soup, unique, 100.0 * unique / max(soup, 1)))


if __name__ == "__main__":
    main()
