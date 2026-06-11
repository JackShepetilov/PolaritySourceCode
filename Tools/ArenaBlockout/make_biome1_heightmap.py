# Authors the REAL Biome1 island heightmap (2017x2017, ~2x2 km) OFFLINE from
# Island/biome1_island_layout.json (v6). PREVIEW WORKFLOW: review the shaded
# preview (UE TOP VIEW orientation) before any import - nothing touches the editor.
#
# Terrain recipe (researched 2026-06-11, not guessed):
#   1. NATURE FIRST: ridged multifractal spine (octave weight feedback) over
#      domain-warped coordinates + gameplay dome toward the east cape
#   2. thermal erosion (talus-angle relaxation) -> scree slopes, soft ridgelines
#   3. exponential slope weighting on walkable land (softens knife edges,
#      keeps the cape cliff sector sharp)
#   4. GAMEPLAY STAMPS LAST: maldive micro-islands, guard-arc pads, citadel pad,
#      A5 bowl, route-corridor softening, then a seam pass around pad skirts
#   5. beach band flattening near the waterline (readable sand aprons)
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
SEED = 20260614

N = 2017
SCALE = 100.0
HALF = (N - 1) * SCALE * 0.5
SEA_FLOOR = -2000.0
PAD_RIM = 900.0
MALDIVE_SHORE_RUN = 2.4
UNDERWATER_SKIRT = 3600.0
DOME_REACH = 62000.0
DOME_BASE = 2000.0
DOME_AMP = 14200.0


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


def grid_noise(n, cells, seed, angle):
    """Value noise on a lattice ROTATED by `angle` with smoothstep fade -
    kills the axis-aligned banding the author flagged ('что за полосы')."""
    rng = np.random.default_rng(seed)
    gsize = int(cells * 1.5) + 3
    g = rng.random((gsize, gsize))
    t = np.linspace(0.0, 1.0, n)
    Yp, Xp = np.meshgrid(t, t, indexing="ij")
    ca, sa = np.cos(angle), np.sin(angle)
    u = (Xp - 0.5) * ca - (Yp - 0.5) * sa + 0.5
    v = (Xp - 0.5) * sa + (Yp - 0.5) * ca + 0.5
    uu = u * cells + 0.25 * cells + 1.0
    vv = v * cells + 0.25 * cells + 1.0
    u0 = np.clip(np.floor(uu).astype(int), 0, gsize - 2)
    v0 = np.clip(np.floor(vv).astype(int), 0, gsize - 2)
    fu = np.clip(uu - u0, 0.0, 1.0)
    fv = np.clip(vv - v0, 0.0, 1.0)
    fu = fu * fu * (3.0 - 2.0 * fu)
    fv = fv * fv * (3.0 - 2.0 * fv)
    return (g[v0, u0] * (1 - fv) * (1 - fu) + g[v0, u0 + 1] * (1 - fv) * fu +
            g[v0 + 1, u0] * fv * (1 - fu) + g[v0 + 1, u0 + 1] * fv * fu)


def fbm(n, octaves, base_cells, seed, persistence=0.55):
    rng = np.random.default_rng(seed + 999)
    out = np.zeros((n, n))
    amp, total, cells = 1.0, 0.0, base_cells
    for o in range(octaves):
        out += amp * grid_noise(n, cells, seed + o * 17, rng.uniform(0, np.pi))
        total += amp
        amp *= persistence
        cells *= 2
    return out / total


def sample(arr, ys, xs):
    """Bilinear sample at float pixel coords (domain warping)."""
    ys = np.clip(ys, 0, N - 1.001)
    xs = np.clip(xs, 0, N - 1.001)
    y0 = np.floor(ys).astype(int)
    x0 = np.floor(xs).astype(int)
    fy, fx = ys - y0, xs - x0
    return (arr[y0, x0] * (1 - fy) * (1 - fx) + arr[y0, x0 + 1] * (1 - fy) * fx +
            arr[y0 + 1, x0] * fy * (1 - fx) + arr[y0 + 1, x0 + 1] * fy * fx)


def ridged(n, octaves, base_cells, seed, gain=0.55, offset=1.0):
    """Ridged multifractal with octave weight feedback (research formula:
    r = (offset - |noise|)^2, weight_next = clamp(r*1.5, 0, 1)^0.8).
    Octave lattices are rotated (no axis-aligned stripes)."""
    rng = np.random.default_rng(seed + 999)
    out = np.zeros((n, n))
    amp, total, cells = 0.5, 0.0, base_cells
    weight = np.ones((n, n))
    for o in range(octaves):
        v = grid_noise(n, cells, seed + o * 23, rng.uniform(0, np.pi)) * 2.0 - 1.0
        r = offset - np.abs(v)
        r = r * r * weight
        weight = np.clip(r * 1.5, 0.0, 1.0) ** 0.8
        out += r * amp
        total += amp
        amp *= gain
        cells *= 2
    return out / total


def thermal_erode(H, iters=55, talus=62.0, k=0.22):
    """Talus-angle relaxation: material slides to lower neighbours where the
    local drop exceeds the repose angle (~32 deg at 100 uu cells)."""
    for _ in range(iters):
        delta = np.zeros_like(H)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nb = np.roll(np.roll(H, dy, 0), dx, 1)
            m = np.clip((H - nb - talus) * (k * 0.25), 0.0, None)
            delta -= m
            delta += np.roll(np.roll(m, -dy, 0), -dx, 1)
        H = H + delta
    return H


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def gauss3(H, passes=1, w=None):
    """Cheap 3x3 blur, optionally mask-weighted."""
    for _ in range(passes):
        acc = H * 4.0
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            acc += np.roll(np.roll(H, dy, 0), dx, 1) * 2.0
        for dy, dx in ((1, 1), (1, -1), (-1, 1), (-1, -1)):
            acc += np.roll(np.roll(H, dy, 0), dx, 1)
        B = acc / 16.0
        H = B if w is None else H * (1 - w) + B * w
    return H


def route_samples(pts, step=150.0):
    out = []
    for a, b in zip(pts, pts[1:]):
        a = np.array(a, dtype=float)
        b = np.array(b, dtype=float)
        L = max(np.hypot(*(b - a)), 1.0)
        k = max(2, int(L / step) + 1)
        for i in range(k):
            out.append(a + (b - a) * (i / (k - 1.0)))
    return np.array(out)


def world_to_idx(p):
    return int(round((p[1] + HALF) / SCALE)), int(round((p[0] + HALF) / SCALE))


def enforce_route_slope(H, pts, max_grade=0.50, step=100.0):
    """Walkability guarantee (author: <= 30 deg everywhere on the route).
    Forward/backward envelope along the serpentine caps the climb rate at
    max_grade (0.50 ~ 26.6 deg, margin under 30); excess terrain is carved
    down with a small local brush around each offending sample."""
    P = route_samples(pts, step=step)
    hs = np.empty(len(P))
    for i, p in enumerate(P):
        r, c = world_to_idx(p)
        hs[i] = H[r, c]
    seg = np.hypot(*(np.diff(P, axis=0).T))
    env = hs.copy()
    for i in range(1, len(env)):
        env[i] = min(env[i], env[i - 1] + seg[i - 1] * max_grade)
    for i in range(len(env) - 2, -1, -1):
        env[i] = min(env[i], env[i + 1] + seg[i] * max_grade)
    # cone caps: H = min(H, e + max_grade*dist). Overlaps take the MIN (no
    # stacking, no runaway) and the carve walls are exactly the max grade.
    carved = 0
    for p, e, h0 in zip(P, env, hs):
        if h0 > e + 10.0:
            r, c = world_to_idx(p)
            R = min(70, int((h0 - e) / (max_grade * SCALE)) + 14)
            r0, r1 = max(0, r - R), min(N, r + R + 1)
            c0, c1 = max(0, c - R), min(N, c + R + 1)
            yy, xx = np.mgrid[r0 - r:r1 - r, c0 - c:c1 - c]
            cap = e + np.hypot(yy, xx) * SCALE * max_grade
            H[r0:r1, c0:c1] = np.minimum(H[r0:r1, c0:c1], cap)
            carved += 1
    return H, carved


def report_route_slopes(H, pts, verbose=True):
    P = route_samples(pts, step=150.0)
    cells = [world_to_idx(p) for p in P]
    uniq = [cells[0]]
    for cc in cells[1:]:
        if cc != uniq[-1]:
            uniq.append(cc)
    hs = np.array([H[r, c] for r, c in uniq], dtype=float)
    pos = np.array([[c * SCALE - HALF, r * SCALE - HALF] for r, c in uniq], dtype=float)
    P = pos
    seg = np.hypot(*(np.diff(pos, axis=0).T))
    grade = np.abs(np.diff(hs)) / np.maximum(seg, 1.0)
    deg = np.degrees(np.arctan(grade))
    over = int((deg > 30.0).sum())
    if verbose:
        print("route slope: max %.1f deg, p95 %.1f deg, over 30 deg: %d/%d"
              % (deg.max(), np.percentile(deg, 95), over, len(deg)))
        for k in np.argsort(deg)[-3:][::-1]:
            if deg[k] > 30.0:
                print("  hotspot %.1f deg at world (%.0f, %.0f) h %.0f->%.0f"
                      % (deg[k], P[k][0], P[k][1], hs[k], hs[k + 1]))
    return deg.max(), over


def main():
    with open(LAYOUT, encoding="utf-8") as f:
        layout = json.load(f)
    slots = {s["id"]: s for s in layout["slots"]}
    isl = layout["island"]

    coord = np.linspace(-HALF, HALF, N)   # row 0 = world SOUTH
    Xu, Yu = np.meshgrid(coord, coord)
    rng = np.random.default_rng(SEED)
    idx = np.arange(N, dtype=float)

    # fields: detail fbm + two-channel domain warp + warped ridged spine
    detail = fbm(N, 5, 8, SEED + 37)
    wx = (fbm(N, 4, 4, SEED + 11) - 0.5) * 160.0   # broad warp, up to ~16k uu
    wy = (fbm(N, 4, 4, SEED + 23) - 0.5) * 160.0
    ys = idx[:, None] + wy
    xs = idx[None, :] + wx
    spine = sample(ridged(N, 6, 6, SEED + 71), ys, xs)
    Dw = sample(detail, ys, xs)

    H = SEA_FLOOR + (fbm(N, 4, 14, SEED + 51) - 0.5) * 220.0

    # ---------------- big island: nature first ----------------
    ICX, ICY = isl["center"]
    R0 = isl["land_radius"]
    CPX, CPY = isl["peak"]
    p1, p2 = rng.uniform(0, 2 * np.pi, 2)
    dx, dy = Xu - ICX, Yu - ICY
    theta = np.arctan2(dy, dx)
    deg = np.degrees(theta) % 360.0
    r_theta = R0 * (1.0 + 0.07 * np.sin(2 * theta + p1)
                    + 0.05 * np.sin(3 * theta + p2)
                    + 0.06 * (Dw - 0.5) * 2.0)
    edge = np.hypot(dx, dy) / r_theta
    cape_bearing = np.degrees(np.arctan2(CPY - ICY, CPX - ICX)) % 360.0
    ddeg = np.abs(((deg - cape_bearing + 180.0) % 360.0) - 180.0)
    in_cliff = smoothstep(26.0, 13.0, ddeg)
    inner = 0.78 - 0.20 * in_cliff
    M = smoothstep(1.04, inner, edge)
    pk_v = np.array([CPX - ICX, CPY - ICY], dtype=float)
    tt = np.clip(((Xu - ICX) * pk_v[0] + (Yu - ICY) * pk_v[1]) / (pk_v @ pk_v), 0.0, 1.0)
    dcap = np.hypot(Xu - (ICX + tt * pk_v[0]), Yu - (ICY + tt * pk_v[1]))
    M = np.maximum(M, smoothstep(13000.0, 8000.0, dcap))

    shelf = smoothstep(1.55, 1.0, edge) * (1.0 - 0.9 * in_cliff)
    H = np.maximum(H, SEA_FLOOR + shelf * 1850.0)

    # gameplay dome IS the gradient (NW landing ~20 m -> SE cape 160 m);
    # the coast fade only acts in a narrow band at the waterline, so the
    # interior has no mesa walls and the cape drops sheer into the sea.
    d_cit = np.hypot(Xu - CPX, Yu - CPY)
    dome = DOME_BASE + DOME_AMP * np.clip(1.0 - d_cit / DOME_REACH, 0.0, 1.0) ** 1.25
    coast_fade = smoothstep(1.04, 0.86, edge)
    spine_n = (spine - spine.min()) / (spine.max() - spine.min())
    relief_amp = 700.0 + 2600.0 * (dome / 16000.0) + 1800.0 * in_cliff
    corr_w = np.zeros_like(H)
    for a_, b_ in zip(layout["rules"]["route_corridor"], layout["rules"]["route_corridor"][1:]):
        a_ = np.array(a_, dtype=float)
        b_ = np.array(b_, dtype=float)
        seg_ = b_ - a_
        t__ = np.clip(((Xu - a_[0]) * seg_[0] + (Yu - a_[1]) * seg_[1]) / (seg_ @ seg_), 0, 1)
        d__ = np.hypot(Xu - (a_[0] + t__ * seg_[0]), Yu - (a_[1] + t__ * seg_[1]))
        corr_w = np.maximum(corr_w, smoothstep(3600.0, 1500.0, d__))
    relief_amp = relief_amp * (1.0 - 0.65 * corr_w)
    noise_part = (spine_n - 0.35) * relief_amp + (Dw - 0.5) * 180.0
    land = (dome + noise_part) * coast_fade
    H = np.where(M > 0.001, np.maximum(H, land), H)

    # thermal erosion shapes scree and ridgelines; the cape keeps its bite
    H_pre = H.copy()
    H = thermal_erode(H)
    H = H_pre * (0.35 * in_cliff) + H * (1.0 - 0.35 * in_cliff)

    # slope-weighted softening on walkable land (keeps the cliff sharp)
    gy_s, gx_s = np.gradient(H, SCALE)
    s = np.hypot(gx_s, gy_s)
    soften = smoothstep(0.55, 1.1, s) * (1.0 - in_cliff) * (H > 150)
    H = gauss3(H, passes=2, w=np.clip(soften, 0, 1) * 0.8)

    # route corridor: smoothing band along the SERPENTINE polyline from the
    # layout (landing -> switchbacks -> east flank -> citadel)
    route_pts = layout["rules"]["route_corridor"]
    corr = np.zeros_like(H)
    for a, b in zip(route_pts, route_pts[1:]):
        a = np.array(a, dtype=float)
        b = np.array(b, dtype=float)
        seg = b - a
        t_ = np.clip(((Xu - a[0]) * seg[0] + (Yu - a[1]) * seg[1]) / (seg @ seg), 0, 1)
        d_seg = np.hypot(Xu - (a[0] + t_ * seg[0]), Yu - (a[1] + t_ * seg[1]))
        corr = np.maximum(corr, smoothstep(3000.0, 1400.0, d_seg))
    H = gauss3(H, passes=3, w=corr * 0.6)

    # ---------------- maldive micro-islands (unchanged model) ----------------
    for s_ in layout["slots"]:
        x, y, z = s_["pos"]
        if s_["kind"] == "arena" and s_.get("tier") in ("S", "M"):
            top_r = s_["r"] + PAD_RIM
            d = np.hypot(Xu - x, Yu - y)
            th = np.arctan2(Yu - y, Xu - x)
            f1, f2 = rng.uniform(0, 2 * np.pi, 2)
            wob = 1.0 + 0.05 * np.sin(3 * th + f1) + 0.03 * np.sin(5 * th + f2) \
                + 0.03 * (Dw - 0.5) * 2.0
            de = d / wob
            shore_r = top_r + z * MALDIVE_SHORE_RUN
            base_r = shore_r + UNDERWATER_SKIRT
            prof = np.where(
                de <= top_r, z,
                np.where(de <= shore_r, z * smoothstep(shore_r, top_r, de),
                         SEA_FLOOR - SEA_FLOOR * smoothstep(base_r, shore_r, de)))
            band = smoothstep(top_r, top_r + 600.0, de) * smoothstep(base_r, shore_r, de)
            prof += (Dw - 0.5) * 2.0 * 70.0 * band
            H = np.maximum(H, prof)

    sx, sy, sz = slots["start_reef"]["pos"]
    d = np.hypot(Xu - sx, Yu - sy)
    rock = np.where(d < 1500.0, sz * smoothstep(1500.0, 300.0, d),
                    SEA_FLOOR - SEA_FLOOR * smoothstep(5200.0, 1500.0, d))
    H = np.maximum(H, np.where(d < 5200.0, rock, H))

    # ---------------- gameplay pads stamped LAST ----------------
    pad_core = np.zeros_like(H)
    for sid in ("G1", "G2", "G3", "Citadel"):
        s_ = slots[sid]
        x, y, z = s_["pos"]
        rf = s_["r"] + PAD_RIM
        bl = 6200.0 if sid == "Citadel" else 3800.0
        d = np.hypot(Xu - x, Yu - y)
        wp = smoothstep(rf + bl, rf, d)
        H = H * (1 - wp) + z * wp
        pad_core = np.maximum(pad_core, smoothstep(rf + 400.0, rf, d))

    g3 = slots["G3"]
    gx_, gy_ = g3["pos"][0], g3["pos"][1]
    yaw_rad = np.radians(g3.get("yaw", 0.0))
    deep = (-np.sin(yaw_rad), np.cos(yaw_rad))
    d_along = (Xu - gx_) * deep[0] + (Yu - gy_) * deep[1]
    d_rad = np.hypot(Xu - gx_, Yu - gy_)
    depth = (500.0 * smoothstep(-3300.0, -1500.0, d_along)
             + 250.0 * smoothstep(-700.0, 700.0, d_along)
             + 180.0 * smoothstep(1500.0, 3100.0, d_along))
    H = H - depth * smoothstep(g3["r"] + PAD_RIM, g3["r"] - 1500.0, d_rad)

    # seam pass: soften the ring just outside the pads, never the cores
    seam = np.zeros_like(H)
    for sid in ("G1", "G2", "G3", "Citadel"):
        s_ = slots[sid]
        d = np.hypot(Xu - s_["pos"][0], Yu - s_["pos"][1])
        rf = s_["r"] + PAD_RIM
        seam = np.maximum(seam, smoothstep(rf + 4600.0, rf + 800.0, d)
                          * smoothstep(rf - 200.0, rf + 800.0, d))
    H = gauss3(H, passes=2, w=seam * 0.7)

    # beach band: flatten the tidal strip for readable sand aprons
    beach_w = smoothstep(260.0, 40.0, np.abs(H - 60.0)) * (1.0 - in_cliff) * (1.0 - pad_core)
    H = H * (1 - beach_w * 0.45) + 60.0 * (beach_w * 0.45)

    # walkability guarantee along the serpentine (author: <= 30 deg).
    # Iterate: envelope-carve, then re-smooth the corridor band so the carve
    # walls themselves get softened; stop when the route is clean.
    route_pts = layout["rules"]["route_corridor"]
    for it in range(3):
        H, carved = enforce_route_slope(H, route_pts, max_grade=0.40)
        print("slope pass %d (0.40 cones): carved %d" % (it + 1, carved))
        if carved == 0:
            break
    H = gauss3(H, passes=1, w=corr * 0.45)   # soften cone rims (no scoop artifacts)
    H, carved = enforce_route_slope(H, route_pts, max_grade=0.50)
    print("slope guard pass (0.50): carved %d" % carved)
    report_route_slopes(H, route_pts)

    px16 = np.clip(32768.0 + (H - SEA_FLOOR) * 1.28, 0, 65535).astype(np.uint16)
    out_png = os.path.join(OUT_DIR, "biome1_heightmap_2017.png")
    Image.fromarray(px16).save(out_png)

    # shaded preview, UE top view orientation: hypsometric tint + hillshade
    # with 3x vertical exaggeration + 10 m contour lines
    EXAG = 3.0
    gy_g, gx_g = np.gradient(H * EXAG, SCALE)
    nz = 1.0 / np.sqrt(gx_g ** 2 + gy_g ** 2 + 1.0)
    light = np.clip((gx_g * 0.5 - gy_g * 0.35 + 1.0) * nz, 0.15, 1.4) / 1.4
    rgb = np.zeros((N, N, 3))
    sea = H < 0
    depth_c = np.clip(-H / 2200.0, 0, 1)
    rgb[sea] = np.stack([0.08 + 0.0 * depth_c[sea], 0.34 - 0.20 * depth_c[sea],
                         0.55 - 0.28 * depth_c[sea]], axis=-1)
    h01 = np.clip(H / 16500.0, 0, 1)
    stops = np.array([
        [0.36, 0.50, 0.28],   # lowland green
        [0.55, 0.55, 0.30],   # dry grass
        [0.62, 0.51, 0.36],   # upland
        [0.58, 0.50, 0.44],   # rock
        [0.85, 0.83, 0.80]])  # summit
    pos = np.array([0.0, 0.18, 0.45, 0.75, 1.0])
    land_col = np.zeros((N, N, 3))
    for c in range(3):
        land_col[..., c] = np.interp(h01, pos, stops[:, c])
    land_col *= light[..., None]
    rgb[~sea] = land_col[~sea]
    rgb[(H > -40) & (H < 40)] = [0.85, 0.8, 0.6]
    lvl = np.floor(H / 1000.0)
    contour = (np.abs(np.diff(lvl, axis=0, prepend=lvl[:1])) +
               np.abs(np.diff(lvl, axis=1, prepend=lvl[:, :1]))) > 0
    contour &= ~sea
    rgb[contour] *= 0.55
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
        s_ = slots[sid]
        probe(sid, s_["pos"][0], s_["pos"][1], s_["pos"][2])
    probe("G3 bowl ctr", *slots["G3"]["pos"][:2], slots["G3"]["pos"][2] - 625)
    probe("shoulder", *slots["shoulder"]["pos"][:2])
    probe("cliff slope", 44000, -1000)
    probe("open sea", 70000, -70000)
    print("max H %.0f" % H.max())
    print("written:", out_png)


if __name__ == "__main__":
    main()
