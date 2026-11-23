#!/usr/bin/env python3
"""Downloads the forest demo's source assets from Poly Haven (CC0).

Usage: python3 -I fetch_assets.py <work-dir>

Writes the original glTF models (1k textures) plus their alpha masks, and the
ground texture sets, under <work-dir>. convert_assets.py turns that into the
engine-ready files in forest/assets. The work dir is large (~230 MB, mostly
raw mesh data) and is not meant to be committed.
"""
import json
import os
import sys
import urllib.request

API = "https://api.polyhaven.com/files/"
RES = "1k"

MODELS = [
    "island_tree_02",
    "tree_small_02",
    "fir_sapling_medium",
    "dead_tree_trunk_02",
    "tree_stump_01",
    "rock_moss_set_01",
    "boulder_01",
]

# Texture sets: asset id -> map types wanted (Poly Haven's own names).
TEXTURES = {
    "forrest_ground_01": ["Diffuse", "nor_gl", "Rough", "AO"],
    "rock_boulder_dry": ["Diffuse", "nor_gl", "Rough", "AO"],
}


def get(url, dest):
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    if os.path.exists(dest):
        return
    print("  ", url.split("/")[-1])
    req = urllib.request.Request(url, headers={"User-Agent": "componex-forest-fetch"})
    with urllib.request.urlopen(req, timeout=120) as r, open(dest, "wb") as f:
        while chunk := r.read(1 << 20):
            f.write(chunk)


def files_json(asset):
    req = urllib.request.Request(API + asset, headers={"User-Agent": "componex-forest-fetch"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.load(r)


def main(out):
    for asset in MODELS:
        print(asset)
        files = files_json(asset)
        gltf = files["gltf"][RES]["gltf"]
        base = os.path.join(out, "models", asset)
        get(gltf["url"], os.path.join(base, f"{asset}.gltf"))
        for rel, info in gltf["include"].items():
            get(info["url"], os.path.join(base, rel))
        # The glTF's baseColor jpgs carry no alpha; the cut-out shape lives in
        # a separate *_alpha map.
        for key, val in files.items():
            if key.lower().endswith("alpha") and RES in val:
                get(val[RES]["png"]["url"], os.path.join(base, "alpha", f"{key.lower()}.png"))

    for asset, maps in TEXTURES.items():
        print(asset)
        files = files_json(asset)
        for m in maps:
            get(files[m][RES]["jpg"]["url"], os.path.join(out, "textures", asset, f"{m}.jpg"))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
