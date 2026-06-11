# Authors the REAL Biome1 island heightmap (2017x2017, ~2x2 km) OFFLINE from
# Island/biome1_island_layout.json. PREVIEW WORKFLOW: review the shaded preview
# before any import - nothing touches the editor here.
#
# Models (author 2026-06-11):
#  - maldive slots (M1-M4): micro-island sized to its arena, arena DEAD-CENTER
#    on an exact flat top, organic rim, open water between islands
#  - big island: only the guards (G1-G3) + shoulder + citadel(villa) live on it;
#    trapezoid profile - long climb from the east shoulder to the SW peak,
#    near-vertical CLIFF on the south/south-west face under the citadel
#
# Output:
#   Island/biome1_heightmap_2017.png     (16-bit, px = 32768 + (z+2000)*1.28)
#   Island/biome1_heightmap_preview.png  (shaded, north up)
#
# Run locally:  python make_biome1_heightmap.py

import json
import os

import numpy as np
from PIL import Image

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
LAYOUT = os.path.join(TOOLS_DIR, "Island", "biome1_island_layout.json")
OUT_DIR = os.path.join(TOOLS_DIR, "Island")
SEED = 20260612

N = 2017
SCALE = 100.0
HALF = (N - 1) * SCALE * 0.5        # 100800
SEA_FLOOR = -2000.0
PAD_RIM = 900.0                      # walkable rim around an arena's flat top
SHORE_RUN = 1.4                      # maldive beach slope (~35 deg)
UNDERWATER_SKIRT = 3600.0
CLIFF_SECTOR = (185.0, 285.0)        # deg, math convention, around island center


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
    with open(LAYOUT, encoding="utf-8") as f:
        layout = json.load(f)
    slots = {s["id"]: s for s in layout["slots"]}
    isl = layout["island"]

    coord = np.linspace(-HALF, HALF, N)   # row 0 = world SOUTH
    Xu, Yu = np.meshgrid(coord, coord)
    rng = np.random.default_rng(SEED)
    detail = fbm(N, 6, 9, SEED + 37)
    wx = (fbm(N, 4, 5, SEED + 11) - 0.5) * 70.0
    wy = (fbm(N, 4, 5, SEED + 23) - 0.5) * 70.0
    idx = np.arange(N, dtype=float)

    def warp_sample(arr):
        ys = np.clip(idx[:, None] + wy, 0, N - 1.001)
        xs = np.clip(idx[None, :] + wx, 0, N - 1.001)
        y0 = np.floor(ys).astype(int)
        x0 = np.floor(xs).astype(int)
        fy, fx = ys - y0, xs - x0
        return (arr[y0, x0] * (1 - fy) * (1 - fx) + arr[y0, x0 + 1] * (1 - fy) * fx +
                arr[y0 + 1, x0] * fy * (1 - fx) + arr[y0 + 1, x0 + 1] * fy * fx)

    Dw = warp_sample(detail)

    H = SEA_FLOOR + (fbm(N, 4, 14, SEED + 51) - 0.5) * 240.0

    # ---------------- big island (guards + shoulder + citadel) ----------------
    ICX, ICY = isl["center"]
    R0 = isl["land_radius"]
    p1, p2, p3 = rng.uniform(0, 2 * np.pi, 3)
    dx, dy = Xu - ICX, Yu - ICY
    theta = np.arctan2(dy, dx)
    deg = np.degrees(theta) % 360.0
    r_theta = R0 * (1.0 + 0.12 * np.sin(2 * theta + p1)
                    + 0.08 * np.sin(3 * theta + p2)
                    + 0.05 * np.sin(5 * theta + p3))
    r_theta *= 1.0 + 0.11 * (Dw - 0.5) * 2.0
    edge = np.hypot(dx, dy) / r_theta
    # cliff sector: tighten the coastal falloff band so the rim drops sheer
    in_cliff = smoothstep(CLIFF_SECTOR[0] - 18, CLIFF_SECTOR[0], deg) * \
        smoothstep(CLIFF_SECTOR[1] + 18, CLIFF_SECTOR[1], deg)
    inner = 0.90 - 0.55 * in_cliff       # mask reaches 1 much closer to the rim
    M = smoothstep(1.05, inner, edge)    # 1 inland, 0 at sea

    # shelf (none under the cliff - deep water at its foot)
    shelf = smoothstep(1.6, 1.02, edge) * (1.0 - 0.85 * in_cliff)
    H = np.maximum(H, SEA_FLOOR + shelf * 1800.0)

    # trapezoid profile: climb from the east shoulder toward the SW peak
    sh = np.array(slots["shoulder"]["pos"][:2], dtype=float)
    pk = np.array(isl["peak"], dtype=float)
    axis = pk - sh
    t_ax = np.clip(((Xu - sh[0]) * axis[0] + (Yu - sh[1]) * axis[1]) / (axis @ axis),
                   -0.15, 1.1)
    base = 2600.0 + 13200.0 * np.clip(t_ax, 0.0, 1.0) ** 1.25
    peak_bump = 1800.0 * np.exp(-((Xu - pk[0]) ** 2 + (Yu - pk[1]) ** 2) / (2 * 6500.0 ** 2))
    relief = (Dw - 0.5) * 2.0 * (250.0 + 1100.0 * M ** 2)
    land = (base + peak_bump) * (M ** 0.85) + relief * M
    H = np.where(M > 0.001, np.maximum(H, land), H)

    # ---------------- maldive micro-islands (arena-sized, centered) ----------
    pads = []
    for s in layout["slots"]:
        x, y, z = s["pos"]
        if s["kind"] == "arena" and s.get("tier") in ("S", "M"):
            top_r = s["r"] + PAD_RIM
            d = np.hypot(Xu - x, Yu - y)
            th = np.arctan2(Yu - y, Xu - x)
            f1, f2 = rng.uniform(0, 2 * np.pi, 2)
            wob = 1.0 + 0.05 * np.sin(3 * th + f1) + 0.03 * np.sin(5 * th + f2) \
                + 0.02 * (Dw - 0.5) * 2.0
            de = d / wob
            shore_r = top_r + z * SHORE_RUN
            base_r = shore_r + UNDERWATER_SKIRT
            prof = np.where(
                de <= top_r, z,
                np.where(de <= shore_r, z * smoothstep(shore_r, top_r, de),
                         SEA_FLOOR - SEA_FLOOR * smoothstep(base_r, shore_r, de)))
            band = smoothstep(top_r, top_r + 600.0, de) * smoothstep(base_r, shore_r, de)
            prof += (detail - 0.5) * 2.0 * 60.0 * band
            H = np.maximum(H, prof)
        elif s["kind"] in ("arena", "waypoint") and s.get("tier") in ("guard", "citadel", None):
            pads.append((x, y, s["r"] + PAD_RIM, 2400.0, z))

    # start reef: a bare rock at the toss origin
    sx, sy, sz = slots["start_reef"]["pos"]
    d = np.hypot(Xu - sx, Yu - sy)
    rock = np.where(d < 1500.0, sz * smoothstep(1500.0, 300.0, d),
                    SEA_FLOOR - SEA_FLOOR * smoothstep(5200.0, 1500.0, d))
    H = np.maximum(H, np.where(d < 5200.0, rock, H))

    # ---------------- exact flat pads on the big island (LAST) ---------------
    for px_, py_, rf, bl, z in pads:
        d = np.hypot(Xu - px_, Yu - py_)
        th = np.arctan2(Yu - py_, Xu - px_)
        f1, f2 = rng.uniform(0, 2 * np.pi, 2)
        wob = 1.0 + 0.05 * np.sin(3 * th + f1) + 0.03 * np.sin(5 * th + f2) \
            + 0.02 * (Dw - 0.5) * 2.0
        wp = smoothstep(rf + bl, rf, d / wob)
        H = H * (1 - wp) + z * wp

    px16 = np.clip(32768.0 + (H - SEA_FLOOR) * 1.28, 0, 65535).astype(np.uint16)
    out_png = os.path.join(OUT_DIR, "biome1_heightmap_2017.png")
    Image.fromarray(px16).save(out_png)

    # shaded preview, north up
    gy, gx = np.gradient(H, SCALE)
    nz = 1.0 / np.sqrt(gx * gx + gy * gy + 1.0)
    light = np.clip((-gx * -0.55 + -gy * 0.35 + 1.2) * nz, 0.0, 1.6) / 1.6
    rgb = np.zeros((N, N, 3))
    sea = H < 0
    depth = np.clip(-H / 2200.0, 0, 1)
    rgb[sea] = np.stack([0.10 + 0.0 * depth[sea], 0.35 - 0.18 * depth[sea],
                         0.55 - 0.25 * depth[sea]], axis=-1)
    hgt = np.clip(H / 17000.0, 0, 1)
    land_col = np.stack([0.42 + 0.3 * hgt, 0.40 + 0.22 * hgt, 0.30 + 0.26 * hgt],
                        axis=-1) * light[..., None]
    rgb[~sea] = land_col[~sea]
    rgb[(H > -40) & (H < 40)] = [0.85, 0.8, 0.6]
    img = (np.clip(rgb, 0, 1) * 255).astype(np.uint8)[::-1]
    Image.fromarray(img).save(os.path.join(OUT_DIR, "biome1_heightmap_preview.png"))

    def probe(label, x, y, want=None):
        i = int(round((y + HALF) / SCALE))
        j = int(round((x + HALF) / SCALE))
        print("%-12s H=%7.0f%s" % (label, H[i, j],
              "" if want is None else "  (want %d)" % want))

    for sid in ("M1", "M2", "M3", "M4"):
        s = slots[sid]
        probe(sid, s["pos"][0], s["pos"][1], s["pos"][2])
    for sid in ("shoulder", "G1", "G2", "G3", "Citadel"):
        s = slots[sid]
        probe(sid, s["pos"][0], s["pos"][1], s["pos"][2])
    probe("cliff foot", -34000, -22000)
    probe("open sea", 70000, -70000)
    print("max H %.0f" % H.max())
    print("written:", out_png)


if __name__ == "__main__":
    main()
