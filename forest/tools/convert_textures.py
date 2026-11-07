"""Re-encodes the Poly Haven textures for the forest demo.

    python3 -I convert_textures.py <work-dir> <forest/assets dir>

Foliage gets its alpha map merged into an RGBA PNG, with colour bled outward
into the transparent texels so cut-out edges don't pick up a dark halo when
mip-mapped. Normal maps stay as the OpenGL (+Y) variants. "arm" maps are
R = ambient occlusion, G = roughness, B = metallic.
"""
import os
import sys

import numpy as np
from PIL import Image, ImageFilter

WORK, OUT_ASSETS = sys.argv[1:3]
M = os.path.join(WORK, "models")

# Textures to emit: name -> (albedo source, normal source, arm source, alpha source|None)
M = os.path.join(WORK, "models")


def tex(model, stem, kind):
    return os.path.join(M, model, "textures", "%s_%s_1k.jpg" % (stem, kind))


TEXTURES = {
    "island_trunk": (tex("island_tree_02", "island_tree_02", "diff"), tex("island_tree_02", "island_tree_02", "nor_gl"), tex("island_tree_02", "island_tree_02", "arm"), None),
    "island_branches": (tex("island_tree_02", "island_tree_02_branches", "diff"), tex("island_tree_02", "island_tree_02_branches", "nor_gl"), tex("island_tree_02", "island_tree_02_branches", "arm"), None),
    "island_leaves": (tex("island_tree_02", "island_tree_02_leaves", "diff"), None, None, os.path.join(M, "island_tree_02", "alpha", "leaves_alpha.png")),
    "small_trunk": (tex("tree_small_02", "tree_small_02", "diff"), tex("tree_small_02", "tree_small_02", "nor_gl"), tex("tree_small_02", "tree_small_02", "arm"), None),
    "small_branches": (tex("tree_small_02", "tree_small_02_branch", "diff"), tex("tree_small_02", "tree_small_02_branch", "nor_gl"), tex("tree_small_02", "tree_small_02_branch", "arm"), None),
    "small_leaves": (tex("tree_small_02", "tree_small_02_leaves", "diff"), None, None, os.path.join(M, "tree_small_02", "alpha", "leaves_alpha.png")),
    "fir_branches": (tex("fir_sapling_medium", "fir_sapling_medium_branches", "diff"), tex("fir_sapling_medium", "fir_sapling_medium_branches", "nor_gl"), tex("fir_sapling_medium", "fir_sapling_medium_branches", "arm"), None),
    "fir_twigs": (tex("fir_sapling_medium", "fir_sapling_medium_twigs", "diff"), None, None, os.path.join(M, "fir_sapling_medium", "alpha", "twigs_alpha.png")),
    "deadlog": (tex("dead_tree_trunk_02", "dead_tree_trunk_02", "diff"), tex("dead_tree_trunk_02", "dead_tree_trunk_02", "nor_gl"), tex("dead_tree_trunk_02", "dead_tree_trunk_02", "arm"), None),
    "stump": (tex("tree_stump_01", "tree_stump_01", "diff"), tex("tree_stump_01", "tree_stump_01", "nor_gl"), tex("tree_stump_01", "tree_stump_01", "arm"), None),
    "boulder": (tex("boulder_01", "boulder_01", "diff"), tex("boulder_01", "boulder_01", "nor_gl"), tex("boulder_01", "boulder_01", "arm"), None),
    "rockmoss": (tex("rock_moss_set_01", "rock_moss_set_01", "diff"), tex("rock_moss_set_01", "rock_moss_set_01", "nor_gl"), tex("rock_moss_set_01", "rock_moss_set_01", "rough"), None),
}


# ---------------------------------------------------------------------------
# Textures
# ---------------------------------------------------------------------------
def bleed(rgb, alpha):
    """Pushes colour outward into transparent texels so bilinear/mip sampling
    at the edge of a cut-out blends leaf colour, not the black background."""
    a = alpha.astype(np.float32) / 255.0
    out = rgb.astype(np.float32)
    known = a > 0.5
    fill = np.zeros_like(out)
    weight = np.zeros(a.shape, np.float32)
    for radius in (2, 4, 8, 16, 32):
        premult = Image.fromarray((out * known[..., None]).astype(np.uint8))
        w = Image.fromarray((known * 255).astype(np.uint8))
        pb = np.asarray(premult.filter(ImageFilter.GaussianBlur(radius)), np.float32)
        wb = np.asarray(w.filter(ImageFilter.GaussianBlur(radius)), np.float32) / 255.0
        ok = (wb > 0.02) & (weight == 0)
        fill[ok] = pb[ok] / wb[ok][:, None]
        weight[ok] = 1
    mean = out[known].mean(axis=0) if known.any() else np.array([80, 110, 50], np.float32)
    fill[weight == 0] = mean
    out[~known] = fill[~known]
    return np.clip(out, 0, 255).astype(np.uint8)


def emit_texture(name, albedo, normal, arm, alpha):
    d = os.path.join(OUT_ASSETS, "textures")
    os.makedirs(d, exist_ok=True)
    rgb = np.asarray(Image.open(albedo).convert("RGB"))
    if alpha:
        im = Image.open(alpha)
        if im.mode.startswith("I;16"):
            a = (np.asarray(im, np.float32) / 257.0).astype(np.uint8)
        else:
            a = np.asarray(im.convert("L"))
        if a.shape != rgb.shape[:2]:
            a = np.asarray(Image.fromarray(a).resize((rgb.shape[1], rgb.shape[0])))
        rgb = bleed(rgb, a)
        Image.fromarray(np.dstack([rgb, a]), "RGBA").save(os.path.join(d, name + "_albedo.png"), optimize=True)
    else:
        Image.fromarray(rgb).save(os.path.join(d, name + "_albedo.jpg"), quality=88)
    if normal:
        Image.open(normal).convert("RGB").save(os.path.join(d, name + "_normal.jpg"), quality=88)
    if arm:
        im = Image.open(arm)
        if im.mode == "L":  # rough-only set: AO = 1, metal = 0
            r = np.asarray(im)
            im = Image.fromarray(np.dstack([np.full_like(r, 255), r, np.zeros_like(r)]))
        im.convert("RGB").save(os.path.join(d, name + "_arm.jpg"), quality=85)


def emit_ground(name, src_dir, out_name):
    d = os.path.join(OUT_ASSETS, "textures")
    Image.open(os.path.join(src_dir, "Diffuse.jpg")).convert("RGB").save(os.path.join(d, out_name + "_albedo.jpg"), quality=88)
    Image.open(os.path.join(src_dir, "nor_gl.jpg")).convert("RGB").save(os.path.join(d, out_name + "_normal.jpg"), quality=88)
    ao = np.asarray(Image.open(os.path.join(src_dir, "AO.jpg")).convert("L"))
    ro = np.asarray(Image.open(os.path.join(src_dir, "Rough.jpg")).convert("L"))
    Image.fromarray(np.dstack([ao, ro, np.zeros_like(ao)])).save(os.path.join(d, out_name + "_arm.jpg"), quality=85)




if __name__ == "__main__":
    for name, (a, n, r, al) in TEXTURES.items():
        emit_texture(name, a, n, r, al)
    emit_ground("forrest_ground_01", os.path.join(WORK, "textures", "forrest_ground_01"), "ground_forest")
    emit_ground("rock_boulder_dry", os.path.join(WORK, "textures", "rock_boulder_dry"), "ground_rock")
