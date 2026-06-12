# Renders shaded crops of the island heightmap around given world points
# (offline QA for terrain work). Output: Island/crop_<label>.png
#
# Usage: python render_terrain_crop.py x y half label [x y half label ...]

import os
import sys

import numpy as np
from PIL import Image

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
N = 2017
SCALE = 100.0
HALF = (N - 1) * SCALE * 0.5
SEA_FLOOR = -2000.0


def load_height(path):
    px = np.asarray(Image.open(path), dtype=np.float64)
    return (px - 32768.0) / 1.28 + SEA_FLOOR


def render(H, cx, cy, half, label, out_dir, exag=2.0):
    r0 = max(0, int((cy - half + HALF) / SCALE))
    r1 = min(N, int((cy + half + HALF) / SCALE) + 1)
    c0 = max(0, int((cx - half + HALF) / SCALE))
    c1 = min(N, int((cx + half + HALF) / SCALE) + 1)
    Hc = H[r0:r1, c0:c1]
    gy, gx = np.gradient(Hc * exag, SCALE)
    nz = 1.0 / np.sqrt(gx ** 2 + gy ** 2 + 1.0)
    light = np.clip((gx * 0.6 - gy * 0.45 + 1.0) * nz, 0.1, 1.5) / 1.5
    sl = np.degrees(np.arctan(np.hypot(*np.gradient(Hc, SCALE))))
    rgb = np.zeros(Hc.shape + (3,))
    base = np.stack([light * 0.85, light * 0.8, light * 0.72], axis=-1)
    rgb[:] = base
    rgb[Hc < 0] = [0.1, 0.3, 0.5]
    # slope tint: yellow >30, red >45
    warn = (sl > 30) & (Hc > 0)
    rgb[warn] = rgb[warn] * 0.4 + np.array([0.9, 0.8, 0.1]) * 0.6
    hot = (sl > 45) & (Hc > 0)
    rgb[hot] = rgb[hot] * 0.3 + np.array([0.9, 0.15, 0.1]) * 0.7
    # 5 m contours
    lvl = np.floor(Hc / 500.0)
    cont = (np.abs(np.diff(lvl, axis=0, prepend=lvl[:1])) +
            np.abs(np.diff(lvl, axis=1, prepend=lvl[:, :1]))) > 0
    rgb[cont & (Hc > 0)] *= 0.6
    img = np.rot90((np.clip(rgb, 0, 1) * 255).astype(np.uint8), k=1)
    im = Image.fromarray(img)
    sc = max(1, int(900 / img.shape[0]))
    if sc > 1:
        im = im.resize((img.shape[1] * sc, img.shape[0] * sc), Image.NEAREST)
    out = os.path.join(out_dir, "crop_{}.png".format(label))
    im.save(out)
    print("written:", out, " slope>45 px:", int(hot.sum()))


def main():
    hm = os.path.join(TOOLS_DIR, "Island", "biome1_heightmap_2017.png")
    args = sys.argv[1:]
    if args and args[0].endswith(".png"):
        hm, args = args[0], args[1:]
    H = load_height(hm)
    out_dir = os.path.join(TOOLS_DIR, "Island")
    for i in range(0, len(args), 4):
        cx, cy, half = float(args[i]), float(args[i + 1]), float(args[i + 2])
        render(H, cx, cy, half, args[i + 3], out_dir)


if __name__ == "__main__":
    main()
