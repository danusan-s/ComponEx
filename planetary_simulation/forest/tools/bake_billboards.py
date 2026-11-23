"""Bakes the forest's distant-tree billboards (impostors).

Run through Blender after convert_assets.py (it reads the LOD0 meshes and the
converted textures from forest/assets):

    blender -b --python bake_billboards.py -- <forest/assets dir> <forest/include dir>

For every tree species this renders the full-detail tree (trunk, branches and
leaves) from kViews evenly spaced azimuths with an orthographic camera, as flat
albedo with alpha (no lighting: the billboard shader lights it at run time, so
it matches the 3D trees whatever the sun does), and packs the views into one
atlas, textures/<species>_billboard.png. Views are laid out row by row, top
row first, view k looking at the tree from model-space direction
(sin a, 0, cos a) with a = k * 2pi / kViews.

It also writes include/billboard_data.hpp with each atlas's layout and the
rectangle the quad must cover, in model space.
"""
import math
import os
import sys

import bpy
import numpy as np

argv = sys.argv[sys.argv.index("--") + 1:]
OUT_ASSETS, OUT_INCLUDE = argv
MODELS = os.path.join(OUT_ASSETS, "models")
TEXTURES = os.path.join(OUT_ASSETS, "textures")

VIEWS = 8
COLS = 4                 # atlas columns; rows = VIEWS / COLS
PIXELS_PER_METRE = 44    # model space, before the tree's own scale
MARGIN = 0.035           # transparent border around each view, fraction of its size
MAX_CELL = 640
SAMPLES = 24             # TAA samples: anti-aliases the alpha-cut leaf edges

# species -> part -> texture file. Mirrors forest_assets.cpp.
SPECIES = {
    "island": dict(trunk="island_trunk_albedo.jpg", branch="island_branches_albedo.jpg", leaf="island_leaves_albedo.png"),
    "small": dict(trunk="small_trunk_albedo.jpg", branch="small_branches_albedo.jpg", leaf="small_leaves_albedo.png"),
    "fir_a": dict(trunk="fir_branches_albedo.jpg", leaf="fir_twigs_albedo.png"),
    "fir_b": dict(trunk="fir_branches_albedo.jpg", leaf="fir_twigs_albedo.png"),
    "fir_c": dict(trunk="fir_branches_albedo.jpg", leaf="fir_twigs_albedo.png"),
}


def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.image_settings.color_depth = "8"
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    scene.display_settings.display_device = "sRGB"
    scene.eevee.taa_render_samples = SAMPLES
    scene.render.filter_size = 1.0
    return scene


def make_material(name, tex_file, alpha_cut):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    emit = nt.nodes.new("ShaderNodeEmission")
    tex = nt.nodes.new("ShaderNodeTexImage")
    img = bpy.data.images.load(os.path.join(TEXTURES, tex_file))
    img.colorspace_settings.name = "sRGB"
    tex.image = img
    tex.interpolation = "Linear"
    # The OBJs hold V flipped (the engine does not flip images), so undo it.
    mapping = nt.nodes.new("ShaderNodeMapping")
    mapping.inputs["Scale"].default_value = (1.0, -1.0, 1.0)
    mapping.inputs["Location"].default_value = (0.0, 1.0, 0.0)
    coords = nt.nodes.new("ShaderNodeTexCoord")
    nt.links.new(coords.outputs["UV"], mapping.inputs["Vector"])
    nt.links.new(mapping.outputs["Vector"], tex.inputs["Vector"])
    nt.links.new(tex.outputs["Color"], emit.inputs["Color"])
    if alpha_cut:
        transparent = nt.nodes.new("ShaderNodeBsdfTransparent")
        mix = nt.nodes.new("ShaderNodeMixShader")
        cut = nt.nodes.new("ShaderNodeMath")
        cut.operation = "GREATER_THAN"
        cut.inputs[1].default_value = 0.45
        nt.links.new(tex.outputs["Alpha"], cut.inputs[0])
        nt.links.new(cut.outputs["Value"], mix.inputs["Fac"])
        nt.links.new(transparent.outputs["BSDF"], mix.inputs[1])
        nt.links.new(emit.outputs["Emission"], mix.inputs[2])
        nt.links.new(mix.outputs["Shader"], out.inputs["Surface"])
    else:
        nt.links.new(emit.outputs["Emission"], out.inputs["Surface"])
    mat.use_backface_culling = False
    return mat


def import_species(name, textures):
    objs = []
    for part, tex_file in textures.items():
        path = os.path.join(MODELS, "%s_%s_lod0.obj" % (name, part))
        if not os.path.exists(path):
            continue
        bpy.ops.wm.obj_import(filepath=path, forward_axis="NEGATIVE_Z", up_axis="Y")
        obj = bpy.context.selected_objects[0]
        obj.data.materials.clear()
        obj.data.materials.append(make_material("%s_%s" % (name, part), tex_file, alpha_cut=part == "leaf"))
        objs.append(obj)
    return objs


def measure(objs):
    """Horizontal radius (about the trunk axis) and vertical range of the tree,
    in metres, ignoring the odd stray card."""
    # The importer turns Y-up into Z-up with an object rotation, not in the vertices.
    pts = np.concatenate([np.array([tuple(o.matrix_world @ v.co) for v in o.data.vertices]) for o in objs])
    radius = np.hypot(pts[:, 0], pts[:, 1])
    return float(np.percentile(radius, 99.8)), float(pts[:, 2].min()), float(pts[:, 2].max())


def render_views(scene, name, half_w, bottom, top, cell_w, cell_h):
    """Renders the views and returns them as float arrays (h, w, 4), top row first."""
    height = top - bottom
    centre_z = bottom + 0.5 * height
    cam_data = bpy.data.cameras.new("cam")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = max(2.0 * half_w * cell_h / cell_w, height)
    cam_data.clip_start = 0.1
    cam_data.clip_end = 400.0
    cam = bpy.data.objects.new("cam", cam_data)
    scene.collection.objects.link(cam)
    scene.camera = cam
    scene.render.resolution_x = cell_w
    scene.render.resolution_y = cell_h
    scene.render.resolution_percentage = 100

    target = bpy.data.objects.new("target", None)
    target.location = (0.0, 0.0, centre_z)
    scene.collection.objects.link(target)
    track = cam.constraints.new("TRACK_TO")
    track.target = target
    track.track_axis = "TRACK_NEGATIVE_Z"
    track.up_axis = "UP_Y"

    # The OBJ is Y-up and Blender Z-up with -Y forward, so a camera at OBJ
    # direction (sin a, 0, cos a) sits at (sin a, -cos a, 0) here.
    views = []
    tmp = os.path.join(OUT_ASSETS, "textures", "_bake_tmp.png")
    for k in range(VIEWS):
        a = 2.0 * math.pi * k / VIEWS
        cam.location = (math.sin(a) * 100.0, -math.cos(a) * 100.0, centre_z)
        scene.render.filepath = tmp
        bpy.ops.render.render(write_still=True)
        img = bpy.data.images.load(tmp)
        px = np.array(img.pixels[:], dtype=np.float32).reshape(cell_h, cell_w, 4)
        bpy.data.images.remove(img)
        views.append(px[::-1])  # Blender stores rows bottom-up
    os.remove(tmp)
    bpy.data.objects.remove(cam)
    bpy.data.objects.remove(target)
    return views


def bleed_colour(px, iterations=14):
    """Spreads the colour of opaque pixels into the transparent ones, so mip-
    mapping and filtering never blend in black."""
    rgb = px[..., :3].copy()
    known = px[..., 3] > 0.02
    if not known.any():
        return px
    mean = rgb[known].mean(axis=0)
    for _ in range(iterations):
        acc = np.zeros_like(rgb)
        cnt = np.zeros(known.shape, dtype=np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            k = np.roll(known, (dy, dx), axis=(0, 1))
            c = np.roll(rgb, (dy, dx), axis=(0, 1))
            acc += c * k[..., None]
            cnt += k
        fill = (~known) & (cnt > 0)
        rgb[fill] = acc[fill] / cnt[fill][..., None]
        known = known | fill
    rgb[~known] = mean
    out = px.copy()
    out[..., :3] = rgb
    return out


def bake(scene, name, textures):
    reset = reset_scene()
    objs = import_species(name, textures)
    radius, z0, z1 = measure(objs)
    half_w = radius * (1.0 + MARGIN)
    pad = (z1 - z0) * MARGIN
    bottom, top = z0 - pad, z1 + pad
    cell_w = min(MAX_CELL, int(math.ceil(2.0 * half_w * PIXELS_PER_METRE)))
    cell_h = min(MAX_CELL, int(math.ceil((top - bottom) * PIXELS_PER_METRE)))
    views = render_views(reset, name, half_w, bottom, top, cell_w, cell_h)

    rows = VIEWS // COLS
    atlas = np.zeros((rows * cell_h, COLS * cell_w, 4), dtype=np.float32)
    for k, v in enumerate(views):
        r, c = divmod(k, COLS)
        atlas[r * cell_h:(r + 1) * cell_h, c * cell_w:(c + 1) * cell_w] = bleed_colour(v)

    img = bpy.data.images.new("atlas", COLS * cell_w, rows * cell_h, alpha=True)
    img.colorspace_settings.name = "sRGB"
    img.pixels = atlas[::-1].ravel()
    img.file_format = "PNG"
    path = os.path.join(TEXTURES, "%s_billboard.png" % name)
    img.filepath_raw = path
    img.save()
    print("baked", name, "cell", cell_w, "x", cell_h, "half width %.2f" % half_w, "z %.2f..%.2f" % (bottom, top))
    return dict(name=name, half_w=half_w, bottom=bottom, top=top, cell_w=cell_w, cell_h=cell_h)


def main():
    results = [bake(None, n, t) for n, t in SPECIES.items()]
    lines = ["// Generated by tools/bake_billboards.py -- do not edit.",
             "#pragma once", "", "namespace forest {", "",
             "// Layout of the baked billboard atlases (textures/<name>_billboard.png) and the",
             "// model-space rectangle each billboard quad covers.",
             "inline constexpr int kBillboardViews = %d;" % VIEWS,
             "inline constexpr int kBillboardCols = %d;" % COLS,
             "inline constexpr int kBillboardRows = %d;" % (VIEWS // COLS), "",
             "struct BillboardSpecies {",
             "  const char *name;",
             "  float halfWidth, bottom, top;",
             "};", "",
             "inline constexpr BillboardSpecies kBillboards[] = {"]
    for r in results:
        lines.append('    {"%s", %.4ff, %.4ff, %.4ff},' % (r["name"], r["half_w"], r["bottom"], r["top"]))
    lines += ["};", "", "} // namespace forest", ""]
    with open(os.path.join(OUT_INCLUDE, "billboard_data.hpp"), "w") as fh:
        fh.write("\n".join(lines))


main()
