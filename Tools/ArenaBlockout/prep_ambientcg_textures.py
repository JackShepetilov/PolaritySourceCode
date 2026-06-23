# Convert an ambientCG PBR set (Color / NormalGL / Roughness, no packed ARM) into the
# project's Poly-Haven-style layout that import_textures.py expects:
#   <name>.jpg          base color (sRGB diffuse)
#   <name>_nor_gl.jpg   OpenGL normal
#   <name>_arm.jpg      packed AO(R) / Roughness(G) / Metalness(B)
#
# ambientCG concrete/plaster sets ship NO AO and NO Metalness map, so for a flat,
# non-metallic surface we pack AO=255 (white = no occlusion) and Metalness=0 (black).
# If the source folder DOES contain an AmbientOcclusion / Metalness map, we use it.
#
# Source sets live under _tmp_tex/<id>/. Re-fetch any ambientCG set with:
#   iwr "https://ambientcg.com/get?file=<id>_2K-JPG.zip" -OutFile c.zip; Expand-Archive c.zip _tmp_tex/<id>
# Usage: edit JOBS below, then run with the system Python (PIL + numpy required).
# Standalone (no UE) — produces files under DownloadedTextures/<cat>/.

import os
import numpy as np
from PIL import Image

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
TMP = os.path.join(TOOLS_DIR, "_tmp_tex")
DEST_ROOT = os.path.join(TOOLS_DIR, "DownloadedTextures")

# (source_subdir under _tmp_tex, output category, output base name)
JOBS = [
    ("Plaster001", "render", "render_microcement_pale"),  # near-white smoothest
    ("Plaster002", "render", "render_cream_smooth"),      # warm cream, best curb match
    ("Plaster003", "render", "render_offwhite_troweled"), # pale, slight trowel
    ("Concrete046", "render", "render_pale_warm"),        # pale warm/greenish render
]


def find(folder, *needles):
    for f in os.listdir(folder):
        low = f.lower()
        if all(n in low for n in needles) and low.endswith((".jpg", ".png")):
            return os.path.join(folder, f)
    return None


def load_gray(path):
    return np.asarray(Image.open(path).convert("L"))


def main():
    for src_sub, cat, name in JOBS:
        src = os.path.join(TMP, src_sub)
        dest = os.path.join(DEST_ROOT, cat)
        os.makedirs(dest, exist_ok=True)

        color = find(src, "color")
        nor = find(src, "normalgl") or find(src, "nor_gl")
        rough = find(src, "rough")
        ao = find(src, "ambientocclusion") or find(src, "occlusion") or find(src, "_ao")
        metal = find(src, "metal")

        if not (color and nor and rough):
            print(f"[SKIP] {src_sub}: missing color/normal/roughness")
            continue

        # base color + normal: straight copy (re-encode to normalize jpg quality)
        Image.open(color).convert("RGB").save(
            os.path.join(dest, f"{name}.jpg"), quality=95)
        Image.open(nor).convert("RGB").save(
            os.path.join(dest, f"{name}_nor_gl.jpg"), quality=95)

        # pack ARM
        r_arr = load_gray(rough)
        h, w = r_arr.shape
        ao_arr = load_gray(ao) if ao else np.full((h, w), 255, np.uint8)
        metal_arr = load_gray(metal) if metal else np.zeros((h, w), np.uint8)
        arm = np.dstack([ao_arr, r_arr, metal_arr]).astype(np.uint8)
        Image.fromarray(arm, "RGB").save(
            os.path.join(dest, f"{name}_arm.jpg"), quality=95)

        print(f"[OK] {name}: color+nor_gl+arm  (AO={'map' if ao else 'flat 1.0'}, "
              f"Metal={'map' if metal else 'flat 0.0'})  -> {dest}")


if __name__ == "__main__":
    main()
