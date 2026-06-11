# Authors the IslandSpike heightmap OFFLINE (numpy+PIL, no unreal) and a shaded
# preview. MALDIVE MODEL (author 2026-06-11): each arena gets its OWN micro-island
# barely bigger than the arena, arena dead-center on a flat top, open water
# between islands is mandatory. No big landmass in this spike.
#
# Output:
#   Island/spike_heightmap_1009.png  (16-bit, px = 32768 + (worldZ+2000)*1.28)
#   Island/spike_heightmap_preview.png (shaded, north up - for review)
#
# Keep island constants in sync with build_island_spike.py.
# Run locally:  python make_island_heightmap.py

import os

import numpy as np
from PIL import Image

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "Island")
N = 1009
HALF = (N - 1) * 100 * 0.5          # 50400: world extent +-half, 100 uu/px
SEA_FLOOR = -2000.0
SEED = 20260611

# Micro-islands: (cx, cy, top_r, top_z). Flat top = arena pad (EXACT z, no noise),
# top_r covers the arena bounding circle + a small walkable rim even at max rim
# wobble (-10%): A1 bound 4401 -> 5300*0.9=4770; A2 bound 5050 -> 6100*0.9=5490.
ISLANDS = [
    (0.0, -24000.0, 5300.0, 450.0),    # A1_Pier maldive (arena centered, deck 500)
    (0.0, -7600.0, 6100.0, 1200.0),    # A2_Courtyard maldive (centered, yaw 45)
]
SHORE_SLOPE_RUN = 1.4    # horizontal run per 1 uu of height (~35 deg above water)
UNDERWATER_SKIRT = 3200.0


def bilerp_grid(g, n):
    c = g.shape[0] - 1
    t = np.linspace(0.0, c, n)
    i0 = np.clip(np.floor(t).astype(int), 0, c - 1)
    f = t - i0
    a = g[np.ix_(i0, i0)]
    b = g[np.ix_(i0, i0 + 1)]
    cc = g[np.ix_(i0 + 1, i0)]
    d = g[np.ix_(i0 + 1, i0 + 1)]
    fy, fx = f[:, None], f[None, :]
    return a * (1 - fy) * (1 - fx) + b * (1 - fy) * fx + cc * fy * (1 - fx) + d * fy * fx


def fbm(n, octaves, base_cells, seed, persistence=0.55):
    rng = np.random.default_rng(seed)
    out = np.zeros((n, n))
    amp, total, cells = 1.0, 0.0, base_cells
    for _ in range(octaves):
        out += amp * bilerp_grid(rng.random((cells + 1, cells + 1)), n)
        total += amp
        amp *= persistence
        cells *= 2
    return out / total


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def main():
    # row 0 = world SOUTH (UE heightmap vertex row 0 sits at the landscape min Y)
    coord = np.linspace(-HALF, HALF, N)
    Xu, Yu = np.meshgrid(coord, coord)
    rng = np.random.default_rng(SEED)
    detail = fbm(N, 5, 10, SEED + 37)

    # sea floor with mild relief
    H = SEA_FLOOR + (fbm(N, 4, 12, SEED + 51) - 0.5) * 240.0

    for cx, cy, top_r, top_z in ISLANDS:
        d = np.hypot(Xu - cx, Yu - cy)
        th = np.arctan2(Yu - cy, Xu - cx)
        f1, f2 = rng.uniform(0, 2 * np.pi, 2)
        # organic rim: top edge wobbles +-10%, never exposing the arena bound
        wob = 1.0 + 0.05 * np.sin(3 * th + f1) + 0.03 * np.sin(5 * th + f2) \
            + 0.02 * (detail - 0.5) * 2.0
        de = d / wob
        shore_r = top_r + top_z * SHORE_SLOPE_RUN
        base_r = shore_r + UNDERWATER_SKIRT
        # piecewise profile: flat top -> beach slope to waterline -> skirt to floor
        prof = np.where(
            de <= top_r, top_z,
            np.where(de <= shore_r,
                     top_z * smoothstep(shore_r, top_r, de),
                     SEA_FLOOR + (0.0 - SEA_FLOOR) * smoothstep(base_r, shore_r, de)))
        # mild relief on the beach slope only (NOT on the flat arena top)
        slope_band = smoothstep(top_r, top_r + 600.0, de) * smoothstep(base_r, shore_r, de)
        prof += (detail - 0.5) * 2.0 * 60.0 * slope_band
        H = np.maximum(H, prof)

    # 16-bit heightmap: px = 32768 + (worldZ - landscapeZ) * 128/zscale
    px16 = np.clip(32768.0 + (H - SEA_FLOOR) * 1.28, 0, 65535).astype(np.uint16)
    os.makedirs(OUT_DIR, exist_ok=True)
    out_png = os.path.join(OUT_DIR, "spike_heightmap_1009.png")
    Image.fromarray(px16).save(out_png)

    # shaded preview, north up (row 0 is south -> flip for viewing)
    gy, gx = np.gradient(H, 100.0)
    nz = 1.0 / np.sqrt(gx * gx + gy * gy + 1.0)
    light = np.clip((-gx * -0.55 + -gy * 0.35 + 1.2) * nz, 0.0, 1.6) / 1.6
    rgb = np.zeros((N, N, 3))
    sea = H < 0
    depth = np.clip(-H / 2200.0, 0, 1)
    rgb[sea] = np.stack([0.10 + 0.0 * depth[sea],
                         0.35 - 0.18 * depth[sea],
                         0.55 - 0.25 * depth[sea]], axis=-1)
    hgt = np.clip(H / 1500.0, 0, 1)
    land_col = np.stack([0.45 + 0.2 * hgt, 0.42 + 0.15 * hgt, 0.32 + 0.18 * hgt],
                        axis=-1) * light[..., None]
    rgb[~sea] = land_col[~sea]
    rgb[(H > -40) & (H < 40)] = [0.85, 0.8, 0.6]
    img = (np.clip(rgb, 0, 1) * 255).astype(np.uint8)[::-1]
    Image.fromarray(img).save(os.path.join(OUT_DIR, "spike_heightmap_preview.png"))

    def probe(label, x, y, want=None):
        i = int(round((y + HALF) / 100.0))
        j = int(round((x + HALF) / 100.0))
        print("%-14s H=%7.0f%s" % (label, H[i, j],
              "" if want is None else "  (want %d)" % want))

    probe("A1 top", 0, -24000, 450)
    probe("A2 top", 0, -8000, 1200)
    probe("strait mid", 0, -16700)
    probe("open sea", 25000, -30000)
    print("max H %.0f  min H %.0f" % (H.max(), H.min()))
    print("written:", out_png)


if __name__ == "__main__":
    main()
