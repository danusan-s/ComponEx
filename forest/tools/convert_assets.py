"""Converts the Poly Haven downloads into the forest demo's engine-ready assets.

Run through Blender (it needs bpy), after fetch_assets.py; textures are handled
separately by convert_textures.py (Blender's Python has no PIL):

    blender -b --python convert_assets.py -- <work-dir> <forest/assets dir> <forest/include dir>

The source models are photogrammetry/scan grade (1-2 million triangles per
tree) and the engine has no LOD system or glTF loader, so for every model this:

  * splits it by material (the engine draws one material per mesh),
  * thins foliage: keeps a random subset of the individual leaf/twig cards and
    scales the survivors up so the canopy keeps its coverage,
  * decimates wood and rock,
  * writes LOD0 and LOD1 as OBJ (Y-up, v/vt/vn, V flipped because the engine
    does not flip images on load),
  * measures trunk radius / crown extents and writes include/species_data.hpp.
"""
import json
import os
import random
import sys
import zlib

import bmesh
import bpy
import numpy as np

argv = sys.argv[sys.argv.index("--") + 1:]
WORK, OUT_ASSETS, OUT_INCLUDE = argv
M = os.path.join(WORK, "models")

# ---------------------------------------------------------------------------
# Part tables. A part is one material of one source object.
#   wood:     decimate to `ratio` of the original triangle count
#   foliage:  keep `keep` of the leaf islands, grow each by `grow` about its
#             centroid, then decimate by `ratio`
# `lods` is a list of parameter dicts, index = LOD level. A part missing from
# a LOD (None) is dropped at that level.
# ---------------------------------------------------------------------------
TREES = [
    dict(name="island", src="island_tree_02", obj=None, scale_hint=(2.0, 3.2),
         parts=[
             dict(name="trunk", mat="island_tree_02", kind="wood", tex="island_trunk",
                  lods=[dict(ratio=0.15), dict(ratio=0.025), dict(ratio=0.012)]),
             dict(name="branch", mat="island_tree_02_branches", kind="wood", tex="island_branches",
                  lods=[dict(keep=0.3, grow=1.25, ratio=0.07), None, None]),
             dict(name="leaf", mat="island_tree_02_leaves", kind="foliage", tex="island_leaves",
                  lods=[dict(keep=0.36, grow=1.75, ratio=0.28), dict(keep=0.14, grow=2.8, ratio=0.28), dict(keep=0.03, grow=6.0, ratio=0.4)]),
         ]),
    dict(name="small", src="tree_small_02", obj=None, scale_hint=(2.2, 3.4),
         parts=[
             dict(name="trunk", mat="tree_small_02_trunk", kind="wood", tex="small_trunk",
                  lods=[dict(ratio=0.12), dict(ratio=0.025), dict(ratio=0.012)]),
             dict(name="branch", mat="tree_small_02_branches", kind="wood", tex="small_branches",
                  lods=[dict(keep=0.35, grow=1.25, ratio=0.1), None, None]),
             dict(name="leaf", mat="tree_small_02_leaves", kind="foliage", tex="small_leaves",
                  lods=[dict(keep=0.18, grow=2.35, ratio=0.27), dict(keep=0.06, grow=4.0, ratio=0.27), dict(keep=0.014, grow=8.5, ratio=0.4)]),
         ]),
]
for variant in "abc":
    TREES.append(dict(
        name="fir_" + variant, src="fir_sapling_medium", obj="fir_sapling_medium_%s_LOD0" % variant,
        scale_hint=(1.7, 2.6),
        parts=[
            dict(name="trunk", mat="fir_sapling_medium_branches", kind="wood", tex="fir_branches",
                 lods=[dict(ratio=0.25), dict(ratio=0.08), dict(ratio=0.03)]),
            dict(name="leaf", mat="fir_sapling_medium_twigs", kind="foliage", tex="fir_twigs",
                 lods=[dict(keep=0.34, grow=1.85, ratio=0.5), dict(keep=0.13, grow=3.0, ratio=0.5), dict(keep=0.03, grow=5.6, ratio=0.5)]),
        ]))

PROPS = [
    dict(name="log", src="dead_tree_trunk_02", obj=None,
         parts=[dict(name="body", mat="dead_tree_trunk_02", kind="wood", tex="deadlog",
                     lods=[dict(ratio=0.05), dict(ratio=0.01), None])]),
    dict(name="stump", src="tree_stump_01", obj=None,
         parts=[dict(name="body", mat="tree_stump_01", kind="wood", tex="stump",
                     lods=[dict(ratio=0.07), dict(ratio=0.015), None])]),
    dict(name="boulder", src="boulder_01", obj=None,
         parts=[dict(name="body", mat="boulder_01", kind="wood", tex="boulder",
                     lods=[dict(ratio=0.07), dict(ratio=0.012), None])]),
]
for i in (1, 2, 3, 4, 5, 6):
    PROPS.append(dict(
        name="rock%d" % i, src="rock_moss_set_01", obj="rock_moss_set_01_rock0%d" % i,
        parts=[dict(name="body", mat="rock_moss_set_01", kind="wood", tex="rockmoss",
                    lods=[dict(ratio=0.25), dict(ratio=0.05), None])]))

# ---------------------------------------------------------------------------
# Geometry
# ---------------------------------------------------------------------------
_loaded = {}


def load_source(src):
    # Items sharing a source file (fir variants, rocks) are consecutive,
    # so only re-import when the file changes.
    if _loaded.get("src") != src:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.import_scene.gltf(filepath=os.path.join(M, src, src + ".gltf"))
        _loaded["src"] = src
        _loaded["objs"] = {o.name: o for o in bpy.context.scene.objects if o.type == "MESH"}
    return _loaded["objs"]


def part_bmesh(obj, mat_name):
    """Faces of one material, transformed to world space about the object's
    own origin (so variants laid out side by side in the file all start at 0)."""
    idx = [i for i, s in enumerate(obj.material_slots) if s.material and s.material.name == mat_name]
    assert idx, "material %s not on %s" % (mat_name, obj.name)
    m = obj.matrix_world.copy()
    m.translation = (0, 0, 0)
    me = obj.data.copy()
    me.transform(m)
    bm = bmesh.new()
    bm.from_mesh(me)
    bpy.data.meshes.remove(me)
    drop = [f for f in bm.faces if f.material_index not in idx]
    bmesh.ops.delete(bm, geom=drop, context="FACES")
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")
    return bm


def islands(bm):
    bm.faces.ensure_lookup_table()
    seen = bytearray(len(bm.faces))
    out = []
    for f in bm.faces:
        if seen[f.index]:
            continue
        seen[f.index] = 1
        stack, comp = [f], [f]
        while stack:
            cur = stack.pop()
            for e in cur.edges:
                for nf in e.link_faces:
                    if not seen[nf.index]:
                        seen[nf.index] = 1
                        stack.append(nf)
                        comp.append(nf)
        out.append(comp)
    return out


def thin_islands(bm, keep, grow, rng):
    """Keeps a random `keep` fraction of the islands, scaled by `grow` about
    their centroids; everything else is deleted."""
    comps = islands(bm)
    kept_faces = set()
    dead = []
    for comp in comps:
        if rng.random() <= keep:
            verts = {v for f in comp for v in f.verts}
            c = sum((v.co for v in verts), type(next(iter(verts)).co)()) / len(verts)
            for v in verts:
                v.co = c + (v.co - c) * grow
            kept_faces.update(f.index for f in comp)
        else:
            dead.extend(comp)
    bmesh.ops.delete(bm, geom=dead, context="FACES")
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context="VERTS")


def decimate(bm, ratio, weld=False):
    if ratio >= 0.999:
        return bm
    if weld:
        # Some scans are triangle soups (every face owns its vertices), which
        # edge-collapse cannot reduce. Welding keeps the shape and, because UVs
        # are per-corner, keeps the UV seams too.
        bmesh.ops.remove_doubles(bm, verts=bm.verts[:], dist=1e-5)
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    me = bpy.data.meshes.new("tmp")
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new("tmp", me)
    bpy.context.scene.collection.objects.link(obj)
    mod = obj.modifiers.new("d", "DECIMATE")
    mod.decimate_type = "COLLAPSE"
    mod.ratio = max(0.001, ratio)
    mod.use_collapse_triangulate = True
    dg = bpy.context.evaluated_depsgraph_get()
    ev = obj.evaluated_get(dg)
    me2 = bpy.data.meshes.new_from_object(ev)
    bpy.data.objects.remove(obj)
    bpy.data.meshes.remove(me)
    bm2 = bmesh.new()
    bm2.from_mesh(me2)
    bpy.data.meshes.remove(me2)
    return bm2


def to_yup(co):
    return (co.x, co.z, -co.y)


def write_obj(path, bm, shift):
    """Triangulated v/vt/vn OBJ, Y-up, smooth vertex normals, V flipped."""
    bmesh.ops.triangulate(bm, faces=bm.faces[:])
    bm.normal_update()
    uv = bm.loops.layers.uv.active
    assert uv is not None, "mesh has no UVs"
    verts, norms, uvs, lines = {}, {}, {}, []
    pos_out, nrm_out, uv_out = [], [], []

    def idx(table, out, key, fmt_args, fmt):
        i = table.get(key)
        if i is None:
            out.append(fmt % fmt_args)
            i = len(out)
            table[key] = i
        return i

    for f in bm.faces:
        corner = []
        for loop in f.loops:
            v = loop.vert
            px, py, pz = to_yup(v.co)
            px -= shift[0]
            py -= shift[1]
            pz -= shift[2]
            pi = idx(verts, pos_out, (round(px, 5), round(py, 5), round(pz, 5)), (px, py, pz), "v %.5f %.5f %.5f")
            nx, ny, nz = to_yup(v.normal)
            ni = idx(norms, nrm_out, (round(nx, 3), round(ny, 3), round(nz, 3)), (nx, ny, nz), "vn %.4f %.4f %.4f")
            entry = loop[uv]
            u, w = entry.uv if hasattr(entry, "uv") else entry
            ti = idx(uvs, uv_out, (round(u, 5), round(w, 5)), (u, 1.0 - w), "vt %.5f %.5f")
            corner.append("%d/%d/%d" % (pi, ti, ni))
        lines.append("f " + " ".join(corner))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as fh:
        fh.write("\n".join(pos_out + uv_out + nrm_out + lines) + "\n")
    return len(lines)


def convert_item(item, kind):
    objs = load_source(item["src"])
    obj = objs[item["obj"]] if item["obj"] else next(iter(objs.values()))
    meta = dict(name=item["name"], parts=[])
    rng = random.Random(zlib.crc32(item["name"].encode()))

    # Raw bmeshes per part (LOD0 source), to find the ground level and the
    # point every model is re-centred on (Blender is Z-up, the OBJ Y-up).
    raw = {p["name"]: part_bmesh(obj, p["mat"]) for p in item["parts"]}
    ground = min(min(v.co.z for v in bm.verts) for bm in raw.values())
    if kind == "tree":
        # Trunk base: the lowest vertices of the first (wood) part.
        base = [v.co for v in raw[item["parts"][0]["name"]].verts if v.co.z < ground + 0.25]
        ax = float(np.mean([c.x for c in base]))
        ay = float(np.mean([c.y for c in base]))
    else:
        pts = [v.co for bm in raw.values() for v in bm.verts]
        ax = (min(c.x for c in pts) + max(c.x for c in pts)) / 2
        ay = (min(c.y for c in pts) + max(c.y for c in pts)) / 2
    shift = (ax, ground, -ay)  # in OBJ (x, y, z) axes
    for p in item["parts"]:
        meta["parts"].append(dict(name=p["name"], tex=p["tex"], kind=p["kind"],
                                  lods=[l is not None for l in p["lods"]]))
    tris = []
    for lod in range(3):
        total = 0
        for p in item["parts"]:
            params = p["lods"][lod]
            if params is None:
                continue
            bm = raw[p["name"]].copy()
            if "keep" in params:
                thin_islands(bm, params["keep"], params["grow"], rng)
            bm = decimate(bm, params["ratio"], weld=p["kind"] == "wood")
            n = write_obj(os.path.join(OUT_ASSETS, "models", "%s_%s_lod%d.obj" % (item["name"], p["name"], lod)), bm, shift)
            total += n
            print("   ", item["name"], p["name"], "lod", lod, n)
            if lod == 0:
                pts = np.array([to_yup(v.co) for v in bm.verts])
                pts -= np.array(shift)
                p["pts"] = pts
            bm.free()
        tris.append(total)
    meta["tris"] = tris

    # Measurements from LOD0.
    wood = [p for p in item["parts"] if p["kind"] == "wood"]
    foliage = [p for p in item["parts"] if p["kind"] == "foliage"]
    allpts = np.concatenate([p["pts"] for p in item["parts"]])
    meta["height"] = float(allpts[:, 1].max())
    if kind == "tree":
        # Trunk radius: low branches and roots pollute a bounding box, so take
        # a low percentile of the distance from the trunk axis instead.
        trunk = wood[0]["pts"]
        band = trunk[(trunk[:, 1] > 0.8) & (trunk[:, 1] < 1.8)]
        if len(band) < 8:
            band = trunk[trunk[:, 1] < 2.5]
        axis = np.median(band[:, [0, 2]], axis=0)
        dist = np.hypot(band[:, 0] - axis[0], band[:, 2] - axis[1])
        meta["trunk_radius"] = float(np.clip(np.percentile(dist, 20), 0.12, 0.5))
        leaf = foliage[0]["pts"]
        meta["crown_bottom"] = float(np.percentile(leaf[:, 1], 3))
        meta["crown_top"] = float(leaf[:, 1].max())
        cx, cz = float(leaf[:, 0].mean()), float(leaf[:, 2].mean())
        meta["crown_center"] = [cx, float((meta["crown_bottom"] + meta["crown_top"]) / 2), cz]
        meta["crown_radius"] = float(np.percentile(np.hypot(leaf[:, 0] - cx, leaf[:, 2] - cz), 90))
    else:
        meta["extent"] = [float(allpts[:, 0].max() - allpts[:, 0].min()), float(allpts[:, 2].max() - allpts[:, 2].min())]
    for bm in raw.values():
        bm.free()
    print("converted", item["name"], "tris per LOD", tris)
    return meta


# ---------------------------------------------------------------------------
def cxx(v):
    return "%.4ff" % v


def write_header(trees, props):
    lines = ["// Generated by tools/convert_assets.py from the Poly Haven (CC0) models -- do not edit.",
             "#pragma once", "", "namespace forest {", "",
             "struct TreeSpecies {",
             "  const char *name;",
             "  bool hasBranches;     // a separate branch mesh exists at LOD0",
             "  float height, trunkRadius;",
             "  float crownX, crownY, crownZ, crownRadius, crownBottom, crownTop;",
             "  float minScale, maxScale;",
             "};", "",
             "struct PropModel {",
             "  const char *name;",
             "  float height, extentX, extentZ;",
             "};", "",
             "inline constexpr TreeSpecies kTreeSpecies[] = {"]
    hints = {t["name"]: t["scale_hint"] for t in TREES}
    for t in trees:
        lo, hi = hints[t["name"]]
        lines.append('    {"%s", %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s},' % (
            t["name"], "true" if any(p["name"] == "branch" for p in t["parts"]) else "false",
            cxx(t["height"]), cxx(t["trunk_radius"]),
            cxx(t["crown_center"][0]), cxx(t["crown_center"][1]), cxx(t["crown_center"][2]),
            cxx(t["crown_radius"]), cxx(t["crown_bottom"]), cxx(t["crown_top"]), cxx(lo), cxx(hi)))
    lines += ["};", "", "inline constexpr PropModel kPropModels[] = {"]
    for p in props:
        lines.append('    {"%s", %s, %s, %s},' % (
            p["name"], cxx(p["height"]), cxx(p["extent"][0]), cxx(p["extent"][1])))
    lines += ["};", "", "} // namespace forest", ""]
    with open(os.path.join(OUT_INCLUDE, "species_data.hpp"), "w") as fh:
        fh.write("\n".join(lines))


def main():
    trees = [convert_item(t, "tree") for t in TREES]
    props = [convert_item(p, "prop") for p in PROPS]
    write_header(trees, props)
    with open(os.path.join(WORK, "convert_report.json"), "w") as fh:
        json.dump(dict(trees=trees, props=props), fh, indent=1)


main()
