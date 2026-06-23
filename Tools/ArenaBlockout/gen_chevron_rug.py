# Build a red+cream CHEVRON flatweave rug by recoloring a real CC0 flatweave
# (ambientCG Carpet015 — its weft rib already reads as a kilim/dhurrie). We keep the
# real weave's Normal / Roughness / AO untouched (physically real fibers) and only
# replace the base color: a generated chevron tinted by the weave's own luminance so
# individual threads still show through.
#
# Output -> DownloadedTextures/floor/red_chevron_rug{.jpg,_nor_gl.jpg,_arm.jpg}
# plus a preview of just the base color for visual review.
# Standalone (PIL + numpy). Re-run to overwrite (idempotent).

import os
import numpy as np
from PIL import Image

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
# Source weave = ambientCG Carpet015 (CC0). If _tmp_tex/Carpet015 is gone, re-fetch:
#   iwr "https://ambientcg.com/get?file=Carpet015_2K-JPG.zip" -OutFile c.zip; Expand-Archive c.zip _tmp_tex/Carpet015
SRC = os.path.join(TOOLS_DIR, "_tmp_tex", "Carpet015")
DEST = os.path.join(TOOLS_DIR, "DownloadedTextures", "floor")
NAME = "red_chevron_rug"

# --- chevron tuning (pixels @ 2K) ---
PERIOD = 512.0     # horizontal width of one zigzag V (full up-down)
BAND = 120.0       # stripe thickness along the rug length
# colors sampled to match the reference photo (terracotta red + warm cream)
RED = np.array([168, 70, 54], dtype=float)
CREAM = np.array([214, 197, 169], dtype=float)
SEAM = np.array([120, 92, 74], dtype=float)   # thin darker line between bands
SEAM_FRAC = 0.10   # fraction of each band that is the darker seam line


def find(folder, *needles):
    for f in os.listdir(folder):
        low = f.lower()
        if all(n in low for n in needles) and low.endswith((".jpg", ".png")):
            return os.path.join(folder, f)
    raise FileNotFoundError(f"{needles} not in {folder}")


def main():
    os.makedirs(DEST, exist_ok=True)
    carpet = np.asarray(Image.open(find(SRC, "color")).convert("RGB")).astype(float)
    N = carpet.shape[0]

    ys, xs = np.mgrid[0:N, 0:N].astype(float)
    tri = np.abs(((xs % PERIOD) / PERIOD) * 2.0 - 1.0)   # 0..1..0 triangle wave
    phase = ys + tri * (PERIOD / 2.0)                    # shear y by triangle -> zigzag
    pos = (phase / BAND)
    stripe = np.floor(pos).astype(int) % 2              # alternating bands
    within = pos - np.floor(pos)                        # 0..1 position inside a band

    base = np.where(stripe[..., None] == 0, RED, CREAM)
    # thin darker seam at each band boundary for woven definition
    seam_mask = (within < SEAM_FRAC) | (within > 1.0 - SEAM_FRAC)
    base[seam_mask] = SEAM

    # imprint real weave luminance variation onto the flat color
    lum = carpet.mean(axis=2)
    factor = np.clip(lum / lum.mean(), 0.55, 1.5)
    out = np.clip(base * factor[..., None], 0, 255).astype(np.uint8)

    Image.fromarray(out, "RGB").save(os.path.join(DEST, f"{NAME}.jpg"), quality=95)
    # reuse the real weave maps as-is
    Image.open(find(SRC, "normalgl")).convert("RGB").save(
        os.path.join(DEST, f"{NAME}_nor_gl.jpg"), quality=95)
    ao = np.asarray(Image.open(find(SRC, "ambientocclusion")).convert("L"))
    rough = np.asarray(Image.open(find(SRC, "rough")).convert("L"))
    metal = np.zeros_like(ao)
    arm = np.dstack([ao, rough, metal]).astype(np.uint8)
    Image.fromarray(arm, "RGB").save(os.path.join(DEST, f"{NAME}_arm.jpg"), quality=95)

    # preview copy for review
    Image.fromarray(out, "RGB").save(
        os.path.join(TOOLS_DIR, "_tmp_tex", "rug_preview.jpg"), quality=92)
    print(f"[OK] {NAME} base+nor_gl+arm -> {DEST}  (tile {N}px)")


if __name__ == "__main__":
    main()
