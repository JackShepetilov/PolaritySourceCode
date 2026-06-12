# Sea-view elevation render of the cape cliff straight from a heightmap PNG:
# orthographic raymarch toward the island along the cape bearing, lambert
# shading from screen-space normals. Lets the author A/B strata variants
# WITHOUT touching the editor.
# Usage: python render_cliff_view.py <heightmap.png> <out.png> [label]
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
LAYOUT = os.path.join(TOOLS_DIR, "Island", "biome1_island_layout.json")
N = 2017
SCALE = 100.0
HALF = (N - 1) * SCALE * 0.5
SEA_FLOOR = -2000.0
W_PX, H_PX = 900, 460          # screen px
SPAN_U = 30000.0               # along-coast halfwidth covered
Z_MIN, Z_MAX = -200.0, 17000.0
STEP = 60.0                    # march step
T_MAX = 42000.0


def hmap(path):
    px = np.asarray(Image.open(path), dtype=np.float32)
    return SEA_FLOOR + (px - 32768.0) / 1.28


def sample(Harr, x, y):
    """Bilinear sample, world coords -> height (vectorized)."""
    c = np.clip((x + HALF) / SCALE, 0, N - 1.001)
    r = np.clip((y + HALF) / SCALE, 0, N - 1.001)
    r0 = np.floor(r).astype(np.int32)
    c0 = np.floor(c).astype(np.int32)
    fr, fc = r - r0, c - c0
    return (Harr[r0, c0] * (1 - fr) * (1 - fc) + Harr[r0, c0 + 1] * (1 - fr) * fc
            + Harr[r0 + 1, c0] * fr * (1 - fc) + Harr[r0 + 1, c0 + 1] * fr * fc)


def main():
    hm_path, out_path = sys.argv[1], sys.argv[2]
    label = sys.argv[3] if len(sys.argv) > 3 else os.path.basename(hm_path)
    H = hmap(hm_path)
    with open(LAYOUT, encoding="utf-8") as f:
        layout = json.load(f)
    ICX, ICY = layout["island"]["center"]
    CPX, CPY = layout["island"]["peak"]
    dx, dy = CPX - ICX, CPY - ICY
    dl = math.hypot(dx, dy)
    ox, oy = dx / dl, dy / dl          # outward (center -> peak -> sea)
    px_, py_ = -oy, ox                 # along-coast axis
    # camera plane sits out at sea past the cape, rays fly INWARD (-outward)
    base_x = CPX + ox * 26000.0
    base_y = CPY + oy * 26000.0

    us = np.linspace(-SPAN_U, SPAN_U, W_PX, dtype=np.float32)
    zs = np.linspace(Z_MAX, Z_MIN, H_PX, dtype=np.float32)
    U, Z = np.meshgrid(us, zs)
    sx = base_x + U * px_
    sy = base_y + U * py_
    depth = np.full(U.shape, np.inf, dtype=np.float32)
    todo = np.ones(U.shape, dtype=bool)
    t = 0.0
    while t < T_MAX and todo.any():
        hx = sx - ox * t
        hy = sy - oy * t
        hh = sample(H, hx, hy)
        hit = todo & (hh >= Z)
        depth[hit] = t
        todo &= ~hit
        t += STEP

    img = np.zeros((H_PX, W_PX, 3), dtype=np.float32)
    sky = np.array([0.55, 0.74, 0.92])
    sea = np.array([0.16, 0.55, 0.62])
    img[:] = sky
    img[Z < 150.0] = sea               # waterline plane
    solid = np.isfinite(depth)
    dd = np.where(solid, depth, np.nan)
    gu = np.nan_to_num(np.gradient(dd, axis=1), nan=0.0)
    gz = np.nan_to_num(np.gradient(dd, axis=0), nan=0.0)
    du = SPAN_U * 2 / W_PX
    dzpx = (Z_MAX - Z_MIN) / H_PX
    nx = -gu / du
    nz_ = gz / dzpx
    norm = 1.0 / np.sqrt(nx * nx + nz_ * nz_ + 1.0)
    lam = np.clip((0.55 * nx + 0.35 * nz_ + 0.85) * norm, 0.12, 1.25) / 1.25
    rock = np.array([0.78, 0.70, 0.55])
    depth_fade = np.clip(1.0 - (depth - 8000.0) / 60000.0, 0.55, 1.0)
    for ch in range(3):
        img[..., ch] = np.where(solid, rock[ch] * lam * depth_fade, img[..., ch])
    under = solid & (Z < 150.0)
    img[under] = img[under] * 0.45 + sea * 0.55
    pim = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8))
    ImageDraw.Draw(pim).text((12, 8), label, fill=(20, 20, 20))
    pim.save(out_path)
    print("rendered:", out_path)


if __name__ == "__main__":
    main()
