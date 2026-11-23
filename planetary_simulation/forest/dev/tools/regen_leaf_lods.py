"""Rebuilds the LOD1/LOD2 leaf meshes from the shipped LOD0 ones, with a gentler card growth per level.
Same operation as convert_assets.py's foliage thinning: keep a random subset of the leaf islands (nested:
LOD2 is a subset of LOD1) and scale each survivor about its centroid, keep*grow^2 constant."""
import sys, random
import numpy as np

MODELS = sys.argv[1]
# name: (LOD0 keep, LOD0 grow, [(keep, grow) for LOD1, LOD2])
SPEC = {
    "island": (0.36, 1.75, [(0.200, 2.35), (0.078, 3.75)]),
    "small":  (0.18, 2.35, [(0.100, 3.15), (0.0398, 5.0)]),
    "fir_a":  (0.34, 1.85, [(0.186, 2.50), (0.0727, 4.0)]),
    "fir_b":  (0.34, 1.85, [(0.186, 2.50), (0.0727, 4.0)]),
    "fir_c":  (0.34, 1.85, [(0.186, 2.50), (0.0727, 4.0)]),
}

def read(path):
    v, vt, vn, f = [], [], [], []
    for l in open(path):
        p = l.split()
        if not p: continue
        if p[0] == "v": v.append([float(x) for x in p[1:4]])
        elif p[0] == "vt": vt.append([float(x) for x in p[1:3]])
        elif p[0] == "vn": vn.append([float(x) for x in p[1:4]])
        elif p[0] == "f":
            c = [tuple(int(t) - 1 for t in q.split("/")) for q in p[1:]]
            assert all(a == b == d for a, b, d in c), "not index-aligned"
            f.append([a for a, _, _ in c])
    return np.array(v), np.array(vt), np.array(vn), np.array(f)

def islands(nv, faces):
    parent = list(range(nv))
    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]; a = parent[a]
        return a
    for tri in faces:
        r = find(tri[0])
        for x in tri[1:]:
            parent[find(x)] = r
    return np.array([find(tri[0]) for tri in faces])

def write(path, v, vt, vn, faces):
    with open(path, "w") as fh:
        fh.write("#componeng_indexed\n")
        fh.write("".join("v %.5f %.5f %.5f\n" % tuple(x) for x in v))
        fh.write("".join("vt %.5f %.5f\n" % tuple(x) for x in vt))
        fh.write("".join("vn %.4f %.4f %.4f\n" % tuple(x) for x in vn))
        fh.write("".join("f " + " ".join("%d/%d/%d" % (i + 1, i + 1, i + 1) for i in tri) + "\n" for tri in faces))

for name, (k0, g0, levels) in SPEC.items():
    v, vt, vn, faces = read(f"{MODELS}/{name}_leaf_lod0.obj")
    root = islands(len(v), faces)
    ids = sorted(set(root.tolist()))
    rng = random.Random(hash(name) & 0xFFFF if False else sum(map(ord, name)) * 7919)
    order = ids[:]; rng.shuffle(order)
    print(f"{name}: LOD0 {len(faces)} tris, {len(ids)} islands")
    for lod, (k, g) in enumerate(levels, start=1):
        n = max(1, round(len(ids) * k / k0))
        chosen = set(order[:n])
        s = g / g0
        sel = np.array([r in chosen for r in root])
        fsel = faces[sel]
        used = np.unique(fsel.ravel())
        remap = -np.ones(len(v), dtype=int); remap[used] = np.arange(len(used))
        nvv = v[used].copy()
        vroot = np.empty(len(v), dtype=int)
        for tri, r in zip(faces, root):
            for x in tri: vroot[x] = r
        for r in chosen:
            m = vroot[used] == r
            if m.any():
                c = nvv[m].mean(axis=0)
                nvv[m] = c + (nvv[m] - c) * s
        write(f"{MODELS}/{name}_leaf_lod{lod}.obj", nvv, vt[used], vn[used], remap[fsel])
        print(f"  lod{lod}: {n} islands, {len(fsel)} tris, scale x{s:.3f}")
