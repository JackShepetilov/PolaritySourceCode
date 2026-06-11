# Authors the REAL Biome1 island heightmap (2017x2017, ~2x2 km) OFFLINE from
# Island/biome1_island_layout.json (v5). PREVIEW WORKFLOW: review the shaded
# preview before any import - nothing touches the editor here.
#
# v4 terrain rules (author review 2026-06-11):
#  - GENTLE everywhere: wide beach aprons all around, soft shield dome rising
#    toward the EAST cape (citadel at the FAR end from the NW landing)
#  - maldive chain on the WEST side (CCW in the UE top view: south->west->north)
#  - guard ring at ~equal radius around the citadel, pads sit nearly flush
#    (wide blends, no raised rims, no empty discs)
#  - A5 (G3) bowl cuts INTO the upslope - amphitheater, not a pit
#
# Output:
#   Island/biome1_heightmap_2017.png     (16-bit, px = 32768 + (z+2000)*1.28)
#   Island/biome1_heightmap_preview.png  (shaded, UE TOP VIEW: +X up, +Y right)
#
# Run locally:  python make_biome1_heightmap.py

import json
import os

import numpy as np
from PIL import Image

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
LAYOUT = os.path.join(TOOLS_DIR, "Island", "biome1_island_layout.json")
OUT_DIR = os.path.join(TOOLS_DIR, "Island")
SEED = 20260613

N = 2017
SCALE = 100.0
HALF = (N - 1) * SCALE * 0.5        # 100800
SEA_FLOOR = -2000.0
PAD_RIM = 900.0
MALDIVE_SHORE_RUN = 1.7              # ~30 deg maldive beach slope
UNDERWATER_SKIRT = 3600.0
DOME_REACH = 60000.0                 # dome falloff distance from the cape
DOME_BASE = 2200.0
DOME_AMP = 14000.0


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
    detail = fbm(N, 5, 8, SEED + 37)

    H = SEA_FLOOR + (fbm(N, 4, 14, SEED + 51) - 0.5) * 220.0

    # ---------------- big island: gentle shield toward the SW cape -----------
    ICX, ICY = isl["center"]
    R0 = isl["land_radius"]
    CPX, CPY = isl["peak"]
    p1, p2 = rng.uniform(0, 2 * np.pi, 2)
    dx, dy = Xu - ICX, Yu - ICY
    theta = np.arctan2(dy, dx)
    deg = np.degrees(theta) % 360.0
    r_theta = R0 * (1.0 + 0.07 * np.sin(2 * theta + p1)
                    + 0.05 * np.sin(3 * theta + p2)
                    + 0.05 * (detail - 0.5) * 2.0)
    edge = np.hypot(dx, dy) / r_theta
    # the cape cliff sector: tight falloff around the citadel bearing (wrap-safe)
    cape_bearing = np.degrees(np.arctan2(CPY - ICY, CPX - ICX)) % 360.0
    ddeg = np.abs(((deg - cape_bearing + 180.0) % 360.0) - 180.0)
    in_cliff = smoothstep(26.0, 13.0, ddeg)
    inner = 0.78 - 0.20 * in_cliff      # wide gentle coast band, tight at the cape
    M = smoothstep(1.04, inner, edge)
    # headland: solid ground out to the citadel pad (never an offshore pillar)
    pk_v = np.array([CPX - ICX, CPY - ICY], dtype=float)
    tt = np.clip(((Xu - ICX) * pk_v[0] + (Yu - ICY) * pk_v[1]) / (pk_v @ pk_v), 0.0, 1.0)
    dcap = np.hypot(Xu - (ICX + tt * pk_v[0]), Yu - (ICY + tt * pk_v[1]))
    M = np.maximum(M, smoothstep(13000.0, 8000.0, dcap))

    # wide beach shelf everywhere EXCEPT under the cape cliff
    shelf = smoothstep(1.55, 1.0, edge) * (1.0 - 0.9 * in_cliff)
    H = np.maximum(H, SEA_FLOOR + shelf * 1850.0)

    # shield dome: height grows toward the cape; gentle relief on top
    d_cit = np.hypot(Xu - CPX, Yu - CPY)
    dome = DOME_BASE + DOME_AMP * np.clip(1.0 - d_cit / DOME_REACH, 0.0, 1.0) ** 1.3
    relief = (detail - 0.5) * 2.0 * (130.0 + 380.0 * M ** 2)
    land = dome * (M ** 1.5) + relief * M
    H = np.where(M > 0.001, np.maximum(H, land), H)

    # ---------------- maldive micro-islands (west chain, arena-sized) --------
    for s in layout["slots"]:
        x, y, z = s["pos"]
        if s["kind"] == "arena" and s.get("tier") in ("S", "M"):
            top_r = s["r"] + PAD_RIM
            d = np.hypot(Xu - x, Yu - y)
            th = np.arctan2(Yu - y, Xu - x)
            f1, f2 = rng.uniform(0, 2 * np.pi, 2)
            wob = 1.0 + 0.05 * np.sin(3 * th + f1) + 0.03 * np.sin(5 * th + f2) \
                + 0.02 * (detail - 0.5) * 2.0
            de = d / wob
            shore_r = top_r + z * MALDIVE_SHORE_RUN
            base_r = shore_r + UNDERWATER_SKIRT
            prof = np.where(
                de <= top_r, z,
                np.where(de <= shore_r, z * smoothstep(shore_r, top_r, de),
                         SEA_FLOOR - SEA_FLOOR * smoothstep(base_r, shore_r, de)))
            band = smoothstep(top_r, top_r + 600.0, de) * smoothstep(base_r, shore_r, de)
            prof += (detail - 0.5) * 2.0 * 55.0 * band
            H = np.maximum(H, prof)

    # start reef: a bare rock at the toss origin
    sx, sy, sz = slots["start_reef"]["pos"]
    d = np.hypot(Xu - sx, Yu - sy)
    rock = np.where(d < 1500.0, sz * smoothstep(1500.0, 300.0, d),
                    SEA_FLOOR - SEA_FLOOR * smoothstep(5200.0, 1500.0, d))
    H = np.maximum(H, np.where(d < 5200.0, rock, H))

    # ---------------- subtle flat pads on the big island (LAST) --------------
    # wide blends, NO rim wobble: pads read as natural local flats, not discs
    for sid in ("G1", "G2", "G3", "Citadel"):
        s = slots[sid]
        x, y, z = s["pos"]
        rf = s["r"] + PAD_RIM
        bl = 3800.0
        d = np.hypot(Xu - x, Yu - y)
        wp = smoothstep(rf + bl, rf, d)
        H = H * (1 - wp) + z * wp

    # A5 (G3) bowl: terraces go to -900, cut INTO the upslope along local +Y
    g3 = slots["G3"]
    gx_, gy_, gz_ = g3["pos"]
    yaw_rad = np.radians(g3.get("yaw", 0.0))
    deep = (-np.sin(yaw_rad), np.cos(yaw_rad))
    d_along = (Xu - gx_) * deep[0] + (Yu - gy_) * deep[1]
    d_rad = np.hypot(Xu - gx_, Yu - gy_)
    depth = (500.0 * smoothstep(-3300.0, -1500.0, d_along)
             + 250.0 * smoothstep(-700.0, 700.0, d_along)
             + 180.0 * smoothstep(1500.0, 3100.0, d_along))
    w_in = smoothstep(g3["r"] + PAD_RIM, g3["r"] - 1500.0, d_rad)
    H = H - depth * w_in

    px16 = np.clip(32768.0 + (H - SEA_FLOOR) * 1.28, 0, 65535).astype(np.uint16)
    out_png = os.path.join(OUT_DIR, "biome1_heightmap_2017.png")
    Image.fromarray(px16).save(out_png)

    # shaded preview, UE top view orientation
    gy_g, gx_g = np.gradient(H, SCALE)
    light = np.clip((-gx_g * -0.55 + -gy_g * 0.35 + 1.2) /
                    np.sqrt(gx_g ** 2 + gy_g ** 2 + 1.0), 0.0, 1.6) / 1.6
    rgb = np.zeros((N, N, 3))
    sea = H < 0
    depth_c = np.clip(-H / 2200.0, 0, 1)
    rgb[sea] = np.stack([0.10 + 0.0 * depth_c[sea], 0.35 - 0.18 * depth_c[sea],
                         0.55 - 0.25 * depth_c[sea]], axis=-1)
    hgt = np.clip(H / 17000.0, 0, 1)
    land_col = np.stack([0.42 + 0.3 * hgt, 0.40 + 0.22 * hgt, 0.30 + 0.26 * hgt],
                        axis=-1) * light[..., None]
    rgb[~sea] = land_col[~sea]
    rgb[(H > -40) & (H < 40)] = [0.85, 0.8, 0.6]
    img = np.rot90((np.clip(rgb, 0, 1) * 255).astype(np.uint8), k=1)
    pim = Image.fromarray(img)
    try:
        from PIL import ImageDraw
        dr = ImageDraw.Draw(pim)
        dr.text((14, 10), "UE TOP VIEW: +X up, +Y right", fill=(255, 255, 255))
        dr.line((30, 90, 30, 40), fill=(255, 255, 255), width=3)
        dr.line((30, 40, 22, 52), fill=(255, 255, 255), width=3)
        dr.line((30, 40, 38, 52), fill=(255, 255, 255), width=3)
        dr.text((24, 96), "+X", fill=(255, 255, 255))
    except Exception:
        pass
    pim.save(os.path.join(OUT_DIR, "biome1_heightmap_preview.png"))

    def probe(label, x, y, want=None):
        i = int(round((y + HALF) / SCALE))
        j = int(round((x + HALF) / SCALE))
        print("%-12s H=%7.0f%s" % (label, H[i, j],
              "" if want is None else "  (want %d)" % want))

    for sid in ("M1", "M2", "M3", "M4", "G1", "G2", "Citadel"):
        s = slots[sid]
        probe(sid, s["pos"][0], s["pos"][1], s["pos"][2])
    probe("G3 bowl ctr", *slots["G3"]["pos"][:2], slots["G3"]["pos"][2] - 625)
    probe("shoulder", *slots["shoulder"]["pos"][:2], 1950)
    probe("cliff slope", 44000, -1000)
    probe("open sea", 70000, -70000)
    print("max H %.0f" % H.max())
    print("written:", out_png)


if __name__ == "__main__":
    main()
