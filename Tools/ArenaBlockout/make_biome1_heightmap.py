# Authors the REAL Biome1 island heightmap (2017x2017, ~2x2 km) OFFLINE from
# Island/biome1_island_layout.json (v7+). PREVIEW WORKFLOW: review the shaded
# preview (UE TOP VIEW orientation) before any import - nothing touches the editor.
#
# Terrain recipe (researched 2026-06-11; smoothness rework 2026-06-11/2):
#   1. NATURE FIRST: ridged multifractal spine (octave weight feedback) over
#      domain-warped coordinates + gameplay dome toward the east cape
#   2. thermal erosion (talus-angle relaxation) -> scree slopes, soft ridgelines
#   3. exponential slope weighting on walkable land (softens knife edges,
#      keeps the cape cliff sector sharp)
#   4. maldive micro-islands + start reef, then a C2 shore-gradient relax
#      (NO constant-height beach shelf - that made an angular terrace)
#   5. curvature-targeted relax of walkable land (kills pleats/creases the
#      author flagged; cliff sector excluded)
#   6. GAMEPLAY STAMPS LAST, all C2 (smootherstep) and footprint-exact:
#      - ROADS: the serpentine is a graded ribbon (slope-limited profile,
#        cut AND fill) - replaces the old min()-cone carving that left
#        stair-step rubble next to G2
#      - PADS: G1/G2 aprons hug the ROTATED ARENA FOOTPRINT at the arena's
#        GROUND PLATE level (A7 keeps its sunken outer_ground: -500), the
#        citadel keeps its wide disc
#      - G3 BOWL: exact local-frame carve under A5's terraces (clearance
#        caps), entry/side contact strips, exit bench behind bort_s toward
#        the citadel-stairs landing, <=32 deg funnel walls outside
#   7. author's citadel staircase: applied LAST (unchanged, verified)
#
# Variations (roguelike groundwork): --seed N re-rolls the NATURE layers
# (spine, warp, coast wobble, maldive wobble) while every gameplay stamp,
# route guarantee and verification stays pinned to the layout JSON. The
# pillars (cape citadel, spawn axis, maldive chain, guard arc) live in the
# layout, not in the seed.
#
# Output:
#   Island/biome1_heightmap_2017.png     (16-bit, px = 32768 + (z+2000)*1.28)
#   Island/biome1_heightmap_preview.png  (shaded, UE TOP VIEW: +X up, +Y right)
#   (--seed N != default appends _seed<N> to both names)
#
# Run locally:  python make_biome1_heightmap.py [--seed N]

import argparse
import json
import math
import os

import numpy as np
from PIL import Image

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
LAYOUT = os.path.join(TOOLS_DIR, "Island", "biome1_island_layout.json")
ARENAS_DIR = os.path.join(TOOLS_DIR, "Arenas")
OUT_DIR = os.path.join(TOOLS_DIR, "Island")
DEFAULT_SEED = 20260614

EXTENT = 201600.0          # world size is FIXED; resolution is not
N = 2017
SCALE = 100.0
HALF = EXTENT * 0.5


def set_resolution(n):
    """Lower-res fast previews for the layout editor (same world extent).
    Cell-relative tunables (erosion talus, despike threshold, relax knees)
    are scaled at their call sites via SCALE/100."""
    global N, SCALE, HALF
    N = int(n)
    SCALE = EXTENT / (N - 1)


SEA_FLOOR = -2000.0
PAD_RIM = 900.0
# Maldive SAND-LENS profile (2026-06-12): islets fall from the arena
# footprint edge as a convex C2 shoulder, run = z * K + BASE (wobbled).
MALDIVE_RUN_K = 2.8
MALDIVE_RUN_BASE = 2000.0
# DEPRECATED (pre-lens flat-disc model): only make_biome1_weightmaps.py still
# reads this - its maldive zones/palms assume the old radial shore and need a
# resync to the lens (see Handoff_BiomeIsland.md, maldive section).
MALDIVE_SHORE_RUN = 2.4
UNDERWATER_SKIRT = 3600.0
DOME_REACH = 62000.0
DOME_BASE = 2000.0
DOME_AMP = 14200.0
ARENA_LIFT = 10.0          # build_biome_island.py floats arenas +10 uu
PLATE_GAP = 30.0           # terrain sits 30 uu under contact plates
ROAD_GRADE = 0.50          # ~26.6 deg profile cap (margin under the 30 rule;
                           # 0.42 was INFEASIBLE for G2-apron -> A5-entry
                           # (0.433 needed) and the anchor cones silently
                           # lifted the apron edge +160)
ROAD_CORE = 380.0          # full-weight half-width of the road ribbon
ROAD_FADE = 2300.0         # fade-out half-width
BOWL_CLEAR = 150.0         # clearance under A5 terrace floors
FUNNEL_GRADE = 0.62        # ~31.8 deg bowl walls outside the footprint


# ---------------------------------------------------------------- noise ----
def grid_noise(n, cells, seed, angle):
    """Value noise on a lattice ROTATED by `angle` with smoothstep fade -
    kills the axis-aligned banding the author flagged ('С‡С‚Рѕ Р·Р° РїРѕР»РѕСЃС‹')."""
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
            g[v0 + 1, u0] * fv * (1 - fu) + g[v0 + 1, u0 + 1] * fv * fu
            ).astype(np.float32)


def fbm(n, octaves, base_cells, seed, persistence=0.55):
    rng = np.random.default_rng(seed + 999)
    out = np.zeros((n, n), dtype=np.float32)
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
    out = np.zeros((n, n), dtype=np.float32)
    amp, total, cells = 0.5, 0.0, base_cells
    weight = np.ones((n, n), dtype=np.float32)
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


def thermal_erode(H, iters=45, talus=62.0, k=0.22):
    """Talus-angle relaxation over 8 NEIGHBOURS (4-neighbour version builds
    axis-aligned terrace blocks on steep slopes - caught 2026-06-11). Diagonal
    neighbours use sqrt(2)-scaled talus/РќР• flow share."""
    nbrs = (((1, 0), 1.0), ((-1, 0), 1.0), ((0, 1), 1.0), ((0, -1), 1.0),
            ((1, 1), 1.4142), ((1, -1), 1.4142), ((-1, 1), 1.4142), ((-1, -1), 1.4142))
    for _ in range(iters):
        delta = np.zeros_like(H)
        for (dy, dx), dist in nbrs:
            nb = np.roll(np.roll(H, dy, 0), dx, 1)
            m = np.clip((H - nb - talus * dist) * (k * 0.125), 0.0, None)
            delta -= m
            delta += np.roll(np.roll(m, -dy, 0), -dx, 1)
        H = H + delta
    return H


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def smootherstep(e0, e1, x):
    """C2 fade (zero 1st AND 2nd derivative at both ends) - the stamp blend
    that does not leave ring-creases."""
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0)


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


def curvature_relax(H, passes, mask, strength=0.85, knee=45.0):
    """Blur weighted by |discrete Laplacian| inside `mask`: creases and
    pleats relax, broad smooth forms stay untouched."""
    for _ in range(passes):
        L = (np.roll(H, 1, 0) + np.roll(H, -1, 0) + np.roll(H, 1, 1) +
             np.roll(H, -1, 1) - 4.0 * H)
        w = np.clip(np.abs(L) / knee, 0.0, 1.0) * strength * mask
        H = gauss3(H, passes=1, w=w)
    return H


def route_samples(pts, step=150.0):
    out = []
    for a, b in zip(pts, pts[1:]):
        a = np.array(a[:2], dtype=float)
        b = np.array(b[:2], dtype=float)
        L = max(np.hypot(*(b - a)), 1.0)
        k = max(2, int(L / step) + 1)
        for i in range(k):
            out.append(a + (b - a) * (i / (k - 1.0)))
    return np.array(out)


def world_to_idx(p):
    return int(round((p[1] + HALF) / SCALE)), int(round((p[0] + HALF) / SCALE))


# ----------------------------------------------------- arena footprints ----
def load_footprint(arena_name):
    """Rotated-rect bounds (LOCAL frame) + ground-plate level of an arena
    spec: every solid piece counts, ground = lowest floor top <= 100 uu
    (the entry/outer plates; spiral pads etc. sit higher)."""
    with open(os.path.join(ARENAS_DIR, arena_name + ".json"), encoding="utf-8") as f:
        spec = json.load(f)
    x0 = y0 = 1e9
    x1 = y1 = -1e9
    ground = 1e9
    for p in spec.get("pieces", []):
        if p.get("shape") == "ramp":
            fx, fy, fz = p["from"]
            tx, ty, tz = p["to"]
            w = p.get("width", 400.0) * 0.5
            px0, px1 = min(fx, tx) - w, max(fx, tx) + w
            py0, py1 = min(fy, ty) - w, max(fy, ty) + w
            top = min(fz, tz)
        else:
            cx, cy, cz = p["pos"]
            sx, sy, sz = [v * 0.5 for v in p["size"]]
            px0, px1, py0, py1 = cx - sx, cx + sx, cy - sy, cy + sy
            top = cz + sz
        x0, x1 = min(x0, px0), max(x1, px1)
        y0, y1 = min(y0, py0), max(y1, py1)
        if p.get("mat") == "floor" and top <= 100.0:
            ground = min(ground, top)
    if ground > 1e8:
        ground = 0.0
    return {"x0": x0, "x1": x1, "y0": y0, "y1": y1, "ground": ground}


def local_frame(Xu, Yu, slot):
    """World grid -> arena-local coords for a slot (pos + yaw)."""
    yaw = math.radians(float(slot.get("yaw", 0.0)))
    c, s = math.cos(yaw), math.sin(yaw)
    dx, dy = Xu - slot["pos"][0], Yu - slot["pos"][1]
    return dx * c + dy * s, -dx * s + dy * c


def slot_world(slot, lx, ly):
    yaw = math.radians(float(slot.get("yaw", 0.0)))
    c, s = math.cos(yaw), math.sin(yaw)
    return (slot["pos"][0] + lx * c - ly * s,
            slot["pos"][1] + lx * s + ly * c)


def rect_dist(lx, ly, x0, x1, y0, y1):
    """Distance OUTSIDE an axis-aligned (local) rect; 0 inside. Rounded
    corners by construction."""
    dx = np.maximum(np.maximum(x0 - lx, lx - x1), 0.0)
    dy = np.maximum(np.maximum(y0 - ly, ly - y1), 0.0)
    return np.hypot(dx, dy)


def apron_edge_point(slot, f, toward, margin=500.0, step=100.0):
    """March from the slot center toward `toward`: the last point still on
    the apron rect(+margin) = where the road crosses the apron edge. Roads
    get an anchor here so their profile is FLAT across the whole apron
    (a center-only anchor let the pad blend drag the climb into bumps)."""
    cx, cy = slot["pos"][0], slot["pos"][1]
    ux, uy = toward[0] - cx, toward[1] - cy
    L = math.hypot(ux, uy)
    ux, uy = ux / L, uy / L
    yaw = math.radians(float(slot.get("yaw", 0.0)))
    c, s = math.cos(yaw), math.sin(yaw)
    t, last = 0.0, (cx, cy)
    while t < L:
        x, y = cx + ux * t, cy + uy * t
        dx, dy = x - cx, y - cy
        lx = dx * c + dy * s
        ly = -dx * s + dy * c
        ddx = max(max((f["x0"] - margin) - lx, lx - (f["x1"] + margin)), 0.0)
        ddy = max(max((f["y0"] - margin) - ly, ly - (f["y1"] + margin)), 0.0)
        if math.hypot(ddx, ddy) > 1.0:
            break
        last = (x, y)
        t += step
    return last


def derive_route(layout, slots):
    """Everything the road/bowl stamps and the analyzer share, computed from
    slots + arena specs + citadel_stairs. route_corridor verts are the author
    gesture: verts within 300 uu of G1/G2 get apron anchors (+ edge anchors
    marching toward their polyline neighbours), other verts are free-form
    waypoints; legacy verts on G3/landing are dropped (the A5 entry approach
    and the bench->stairs leg are appended automatically)."""
    fp = {sid: load_footprint(slots[sid]["default"]) for sid in ("G1", "G2", "G3")}
    g3, cit = slots["G3"], slots["Citadel"]
    apron_z = {sid: slots[sid]["pos"][2] + ARENA_LIFT + fp[sid]["ground"] - PLATE_GAP
               for sid in ("G1", "G2")}
    a5 = fp["G3"]
    appr_front = slot_world(g3, 0.0, a5["y0"] - 100.0)
    appr_z = g3["pos"][2] + ARENA_LIFT - PLATE_GAP            # approach top = 0
    bench_exit = slot_world(g3, 1103.0, 5300.0)   # where the bench strip ends
    bench_z = g3["pos"][2] + ARENA_LIFT - 450.0 - PLATE_GAP   # bort_s top = -450
    st = layout["rules"].get("citadel_stairs")
    cy = math.radians(float(cit.get("yaw", 0.0)))
    ca_, sa_ = math.cos(cy), math.sin(cy)
    ll = st["landing_local"]
    landing = (cit["pos"][0] + ll[0] * ca_ - ll[1] * sa_,
               cit["pos"][1] + ll[0] * sa_ + ll[1] * ca_)
    landing_z = cit["pos"][2] + ll[2] - 30.0                  # stairs flatten target

    verts = [v for v in layout["rules"]["route_corridor"]
             if math.hypot(v[0] - g3["pos"][0], v[1] - g3["pos"][1]) > 300.0
             and math.hypot(v[0] - landing[0], v[1] - landing[1]) > 300.0]
    legA = []
    for i, v in enumerate(verts):
        sid = next((s for s in ("G1", "G2")
                    if math.hypot(v[0] - slots[s]["pos"][0],
                                  v[1] - slots[s]["pos"][1]) <= 300.0), None)
        if sid:
            s_, f = slots[sid], fp[sid]
            nxt = verts[i + 1] if i + 1 < len(verts) else appr_front
            if i > 0:
                e = apron_edge_point(s_, f, verts[i - 1])
                legA.append((e[0], e[1], apron_z[sid]))
            legA.append((s_["pos"][0], s_["pos"][1], apron_z[sid]))
            e = apron_edge_point(s_, f, nxt)
            legA.append((e[0], e[1], apron_z[sid]))
        else:
            legA.append((float(v[0]), float(v[1])))
    # flat-finish anchor: the last ~1.1k before the A5 entry stays at plate
    # level, so the entry contact strip merges into an already-level road
    last = legA[-1]
    ad = max(math.hypot(appr_front[0] - last[0], appr_front[1] - last[1]), 1.0)
    pre = (appr_front[0] - (appr_front[0] - last[0]) / ad * 1100.0,
           appr_front[1] - (appr_front[1] - last[1]) / ad * 1100.0)
    legA.append((pre[0], pre[1], appr_z))
    legA.append((appr_front[0], appr_front[1], appr_z))
    legB = [(bench_exit[0], bench_exit[1], bench_z),
            (landing[0], landing[1], landing_z)]
    return {"fp": fp, "apron_z": apron_z, "a5": a5, "legA": legA, "legB": legB,
            "appr_front": appr_front, "appr_z": appr_z, "bench_exit": bench_exit,
            "bench_z": bench_z, "landing": landing, "landing_z": landing_z}


# ----------------------------------------------------------------- roads ----
def moving_average(v, k):
    if k <= 1:
        return v.copy()
    pad = np.concatenate([np.full(k, v[0]), v, np.full(k, v[-1])])
    ker = np.ones(2 * k + 1) / (2 * k + 1.0)
    return np.convolve(pad, ker, mode="same")[k:-k]


def build_road(H, pts, label, grade=ROAD_GRADE, step=120.0):
    """Stamp one graded road ribbon. pts: [(x, y[, z_anchor]), ...].
    Profile = smoothed terrain heights, hard-pinned at anchors, then a
    forward/backward grade envelope (cuts); the ribbon lerp both cuts and
    FILLS, so dips become embankments. Cut/fill walls beyond the core are
    grade-clamped to <=32 deg (deep cuts otherwise left 45+ deg fade walls).
    Returns (H, core_mask) - the mask keeps later relax passes OFF the road."""
    # --- sample the polyline, carry anchor flags
    P = []
    anchors = []
    for a, b in zip(pts, pts[1:]):
        ax, ay = a[0], a[1]
        bx, by = b[0], b[1]
        L = max(math.hypot(bx - ax, by - ay), 1.0)
        k = max(2, int(L / step) + 1)
        start = 0 if not P else 1     # avoid duplicating shared vertices
        for i in range(start, k):
            t = i / (k - 1.0)
            P.append((ax + (bx - ax) * t, ay + (by - ay) * t))
    P = np.array(P)
    # anchor indices: nearest sample to each anchored vertex
    for v in pts:
        if len(v) > 2:
            d = np.hypot(P[:, 0] - v[0], P[:, 1] - v[1])
            anchors.append((int(np.argmin(d)), float(v[2])))

    hs = np.array([H[world_to_idx(p)] for p in P])
    hs = moving_average(hs, max(1, int(1200.0 / step)))
    for idx, z in anchors:
        hs[idx] = z
    seg = np.hypot(*(np.diff(P, axis=0).T))
    env = hs.copy()
    for i in range(1, len(env)):
        env[i] = min(env[i], env[i - 1] + seg[i - 1] * grade)
    for i in range(len(env) - 2, -1, -1):
        env[i] = min(env[i], env[i + 1] + seg[i] * grade)
    # anchors are hard targets: enforce |env - z| <= grade * arc_dist around
    # each anchor (double cone). A post-envelope hard set would leave a cliff
    # between the anchor sample and its neighbours - caught on first run.
    arc = np.concatenate([[0.0], np.cumsum(seg)])
    for (i1, z1), (i2, z2) in zip(anchors, anchors[1:]):
        need = abs(z2 - z1) / max(abs(arc[i2] - arc[i1]), 1.0)
        if need > grade:
            print("road %s: **INFEASIBLE** anchor pair (%.0f->%.0f over %.0f uu "
                  "= grade %.2f > %.2f) - anchors WILL drift, fix the layout"
                  % (label, z1, z2, abs(arc[i2] - arc[i1]), need, grade))
    for idx, z in anchors:
        da = np.abs(arc - arc[idx])
        env = np.minimum(env, z + da * grade)
        env = np.maximum(env, z - da * grade)
    # chord-fill between consecutive anchors: terrain dips under the straight
    # anchor-to-anchor ramp get BRIDGED (causeway), not followed - the dip in
    # front of the A5 entry left a 37 deg step at the door
    for (i1, z1), (i2, z2) in zip(anchors, anchors[1:]):
        if i2 <= i1:
            continue
        t = (arc[i1:i2 + 1] - arc[i1]) / max(arc[i2] - arc[i1], 1.0)
        env[i1:i2 + 1] = np.maximum(env[i1:i2 + 1], z1 + (z2 - z1) * t)
    if os.environ.get("ROAD_DEBUG"):
        for i in range(max(0, len(P) - 26), len(P)):
            print("  dbg %-5s arc %7.0f  hs %7.0f  env %7.0f  at (%.0f,%.0f)"
                  % (label, arc[i], hs[i], env[i], P[i][0], P[i][1]))

    # --- rasterize: windowed per micro-segment, closest-segment wins
    best_d = np.full_like(H, 1e9)
    best_h = np.zeros_like(H)
    reach = 3900.0   # fade + the longest 32deg cut wall (deep cuts ~2000 uu)
    Rc = int(reach / SCALE) + 2
    for i in range(len(P) - 1):
        ax, ay = P[i]
        bx, by = P[i + 1]
        r0, c0 = world_to_idx(((ax + bx) * 0.5, (ay + by) * 0.5))
        rr0, rr1 = max(0, r0 - Rc), min(N, r0 + Rc + 1)
        cc0, cc1 = max(0, c0 - Rc), min(N, c0 + Rc + 1)
        if rr0 >= rr1 or cc0 >= cc1:
            continue
        xs = np.arange(cc0, cc1) * SCALE - HALF
        ys = np.arange(rr0, rr1) * SCALE - HALF
        Xw, Yw = np.meshgrid(xs, ys)
        sx, sy = bx - ax, by - ay
        ss = max(sx * sx + sy * sy, 1.0)
        t = np.clip(((Xw - ax) * sx + (Yw - ay) * sy) / ss, 0.0, 1.0)
        d = np.hypot(Xw - (ax + t * sx), Yw - (ay + t * sy))
        h = env[i] + (env[i + 1] - env[i]) * t
        win_d = best_d[rr0:rr1, cc0:cc1]
        win_h = best_h[rr0:rr1, cc0:cc1]
        upd = d < win_d
        win_d[upd] = d[upd]
        win_h[upd] = h[upd]
    # Wall construction: C2 fade for SHALLOW edges, pure 32-deg terrace wall
    # for DEEP cuts/fills, switched smoothly by |terrain - road|. A C2 fade
    # against a 2000-uu cut peaks at ~63 deg mid-fade (1.875*dh/width) while
    # staying pointwise UNDER the grade cone - so a clamp alone never bites.
    near = best_d < 1e8
    bh = np.where(near, best_h, H)
    w = smootherstep(ROAD_FADE, ROAD_CORE, best_d)
    blend = H * (1.0 - w) + bh * w
    wall = np.maximum(np.where(near, best_d, 0.0) - ROAD_CORE, 0.0) * FUNNEL_GRADE
    terrace = np.minimum(np.maximum(H, bh - wall), bh + wall)
    steep = smootherstep(700.0, 1400.0, np.abs(H - bh))
    tgt = blend * (1.0 - steep) + terrace * steep
    feather = smootherstep(3800.0, 3100.0, np.where(near, best_d, 1e9))
    H = H + (tgt - H) * feather

    # report
    g = np.abs(np.diff(env)) / np.maximum(seg, 1.0)
    print("road %-10s len %6.0f uu, profile max %.1f deg, fill/cut anchored at %d pts"
          % (label, seg.sum(), math.degrees(math.atan(g.max())), len(anchors)))
    return H, best_d


def report_leg(H, pts, label):
    P = route_samples(pts, step=150.0)
    cells = [world_to_idx(p) for p in P]
    uniq = [cells[0]]
    for cc in cells[1:]:
        if cc != uniq[-1]:
            uniq.append(cc)
    hs = np.array([H[r, c] for r, c in uniq], dtype=float)
    pos = np.array([[c * SCALE - HALF, r * SCALE - HALF] for r, c in uniq])
    seg = np.hypot(*(np.diff(pos, axis=0).T))
    deg = np.degrees(np.arctan(np.abs(np.diff(hs)) / np.maximum(seg, 1.0)))
    over = int((deg > 30.0).sum())
    print("route %-10s: max %.1f deg, p95 %.1f, over 30 deg: %d/%d"
          % (label, deg.max(), np.percentile(deg, 95), over, len(deg)))
    for k in np.argsort(deg)[::-1][:4]:
        if deg[k] > 30.0:
            print("    hotspot %.1f deg at (%.0f,%.0f) h %.0f->%.0f"
                  % (deg[k], pos[k][0], pos[k][1], hs[k], hs[k + 1]))
    return deg.max(), over


# ------------------------------------------------------------------ main ----
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=None,
                    help="nature-layer seed (gameplay stamps are seed-"
                         "independent); default = layout's 'seed' field")
    ap.add_argument("--layout", default=LAYOUT,
                    help="layout JSON (default: the active biome1_island_layout.json)")
    ap.add_argument("--res", type=int, default=2017,
                    help="grid resolution; lower = fast preview for the layout "
                         "editor (505 ~ 4 s). ONLY 2017 may be imported to UE")
    ap.add_argument("--out", default=None, help="heightmap PNG path override")
    ap.add_argument("--preview", default=None, help="preview PNG path override")
    args = ap.parse_args()
    set_resolution(args.res)
    cell = SCALE / 100.0    # cell-relative tunables scale (1.0 at full res)

    with open(args.layout, encoding="utf-8") as f:
        layout = json.load(f)
    seed = args.seed if args.seed is not None else int(layout.get("seed", DEFAULT_SEED))
    suffix = "" if seed == DEFAULT_SEED else "_seed{}".format(seed)
    slots = {s["id"]: s for s in layout["slots"]}
    isl = layout["island"]
    # author-tunable knobs (layout editor); defaults = the shipped island
    road_grade = float(layout["rules"].get("road_grade", ROAD_GRADE))
    dome_base = float(isl.get("dome_base", DOME_BASE))
    dome_amp = float(isl.get("dome_amp", DOME_AMP))
    dome_reach = float(isl.get("dome_reach", DOME_REACH))
    dome_pow = float(isl.get("dome_pow", 1.25))
    cliff_half = float(isl.get("cliff_half_deg", 26.0))

    coord = np.linspace(-HALF, HALF, N, dtype=np.float32)   # row 0 = world SOUTH
    Xu, Yu = np.meshgrid(coord, coord)
    rng = np.random.default_rng(seed)
    idx = np.arange(N, dtype=np.float32)

    # fields: detail fbm + two-channel domain warp + warped ridged spine
    detail = fbm(N, 5, 8, seed + 37)
    wx = (fbm(N, 4, 4, seed + 11) - 0.5) * 160.0   # broad warp, up to ~16k uu
    wy = (fbm(N, 4, 4, seed + 23) - 0.5) * 160.0
    ys = idx[:, None] + wy
    xs = idx[None, :] + wx
    spine = sample(ridged(N, 6, 6, seed + 71), ys, xs)
    Dw = sample(detail, ys, xs)

    H = SEA_FLOOR + (fbm(N, 4, 14, seed + 51) - 0.5) * 220.0

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
    # author coast cuts (2026-06-11): pull the coastline in over given bearing
    # windows - the lobes become water with the standard beach transition
    for cut in layout["rules"].get("coast_cuts", []):
        dd_c = np.abs(((deg - (cut["bearing_deg"] % 360.0) + 180.0) % 360.0) - 180.0)
        w_c = smoothstep(cut["halfwidth_deg"], cut["halfwidth_deg"] - 14.0, dd_c)
        r_theta = r_theta * (1.0 - cut["depth"] * w_c)
    edge = np.hypot(dx, dy) / r_theta
    cape_bearing = np.degrees(np.arctan2(CPY - ICY, CPX - ICX)) % 360.0
    ddeg = np.abs(((deg - cape_bearing + 180.0) % 360.0) - 180.0)
    in_cliff = smoothstep(cliff_half, cliff_half * 0.5, ddeg)
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
    dome = dome_base + dome_amp * np.clip(1.0 - d_cit / dome_reach, 0.0, 1.0) ** dome_pow
    coast_fade = smoothstep(1.04, 0.86, edge)
    spine_n = (spine - spine.min()) / (spine.max() - spine.min())
    relief_amp = 700.0 + 2600.0 * (dome / 16000.0) + 1800.0 * in_cliff

    # damp natural relief along the WHOLE serpentine (roads come later)
    corr_pts = list(layout["rules"]["route_corridor"])
    corr_w = np.zeros_like(H)
    for a_, b_ in zip(corr_pts, corr_pts[1:]):
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
    H = thermal_erode(H, talus=62.0 * cell)
    H = H_pre * (0.35 * in_cliff) + H * (1.0 - 0.35 * in_cliff)

    # curvature relax on walkable land (keeps the cliff sharp). The old
    # SLOPE-thresholded soften (smoothstep 0.55..1.1) was the terracette
    # machine: smoothing dropped a slope below the threshold, stopped, the
    # neighbour stayed steep -> banded steps all over steep dunes. Curvature
    # weighting has no threshold feedback: planar steep faces are untouched,
    # step edges melt.
    H = curvature_relax(H, passes=5,
                        mask=(1.0 - in_cliff) * smoothstep(120.0, 250.0, H),
                        strength=0.85, knee=30.0 * cell)

    # ---------------- maldive micro-islands: SAND-LENS profile ---------------
    # Flat-top discs (with or without a berm lip) read as poker chips at eye
    # level (author 2026-06-12, twice). Lens model: the arena footprint zone
    # stays glass-flat (plates float 10 uu over it), and from its edge the
    # islet falls as a CONVEX C2 shoulder straight under the waterline - no
    # flat ring, no rim, the horizon just rolls away. Dune noise scales with
    # the local drop, so the crown is serene and nothing pokes the floors.
    for s_ in layout["slots"]:
        x, y, z = s_["pos"]
        if s_["kind"] == "arena" and s_.get("tier") in ("S", "M"):
            f_m = load_footprint(s_["default"])
            lxm, lym = local_frame(Xu, Yu, s_)
            fp_d = rect_dist(lxm, lym, f_m["x0"] - 250.0, f_m["x1"] + 250.0,
                             f_m["y0"] - 250.0, f_m["y1"] + 250.0)
            th = np.arctan2(Yu - y, Xu - x)
            ph = rng.uniform(0, 2 * np.pi, 5)   # ALWAYS drawn: stable rng stream
            # COASTLINE NOISE is the point (author 2026-06-12: weak wobble left
            # the islets reading as programmatic rounded squares). The 1-theta
            # harmonic drags the shoreline to one side - the arena ends up at
            # the EDGE of an asymmetric sandbar, not centered on a stamp; the
            # higher harmonics + the field term cut bays and capes. Only the
            # first few hundred uu around the arena pad stay rect-flavored
            # (that is the cleared construction pad).
            # Per-islet author overrides (layout editor): slot["lens"] =
            # {tail_deg, tail_amp, ragged, run_scale}; absent keys keep the
            # seed-rolled look.
            lens = s_.get("lens", {})
            tail_amp = float(lens.get("tail_amp", 0.30))
            rag = float(lens.get("ragged", 1.0))
            ph0 = (math.pi / 2.0 - math.radians(float(lens["tail_deg"]))
                   if "tail_deg" in lens else ph[0])
            wob = (1.0 + tail_amp * np.sin(th + ph0)
                   + rag * (0.18 * np.sin(2 * th + ph[1])
                            + 0.10 * np.sin(3 * th + ph[2])
                            + 0.06 * np.sin(5 * th + ph[3])
                            + 0.04 * np.sin(7 * th + ph[4])))
            # upper clip 1.40: lobes any larger can land-bridge the M3-M4 and
            # M4-island straits (open water between islands is a layout rule;
            # the analyzer water-gap gate enforces it for every seed)
            wob = np.clip(wob + 0.30 * rag * (Dw - 0.5) * 2.0, 0.60, 1.40)
            gate = smootherstep(0.0, 700.0, fp_d)
            t = np.maximum(fp_d / wob + (spine_n - 0.45) * 1300.0 * rag * gate, 0.0)
            run = (z * MALDIVE_RUN_K + MALDIVE_RUN_BASE) \
                * float(lens.get("run_scale", 1.0))   # mid-slope < ~30 deg
            dome = z - (z + 250.0) * smootherstep(0.0, run, t)
            namp = np.minimum(170.0, 30.0 + (z - dome) * 0.55) \
                * smootherstep(0.0, 350.0, fp_d)
            dome = dome + (Dw - 0.5) * 2.0 * namp \
                * smoothstep(run + 600.0, run - 600.0, t)
            skirt = -250.0 + (SEA_FLOOR + 250.0) \
                * smoothstep(run, run + UNDERWATER_SKIRT, t)
            H = np.maximum(H, np.where(t <= run, dome, skirt))

    # start reef: WIDE flat top (the author's launch structure stands here -
    # it must not hang over the slope; was flat r<300 only)
    sx, sy, sz = slots["start_reef"]["pos"]
    d = np.hypot(Xu - sx, Yu - sy)
    rock = np.where(d < 2100.0, sz * smootherstep(2100.0, 1000.0, d),
                    SEA_FLOOR - SEA_FLOOR * smoothstep(5200.0, 2100.0, d))
    H = np.maximum(H, np.where(d < 5200.0, rock, H))

    # ---------------- shore: C2 gradient relax (no angular shelf) -----------
    # Pull heights toward the waterline with a C2 weight: derivative is
    # continuous at the band edges, so no terrace lip at |H-60|=band.
    pad_core = np.zeros_like(H)
    for sid in ("G1", "G2", "G3", "Citadel"):
        s_ = slots[sid]
        d = np.hypot(Xu - s_["pos"][0], Yu - s_["pos"][1])
        pad_core = np.maximum(pad_core, smoothstep(s_["r"] + PAD_RIM + 400.0,
                                                   s_["r"] + PAD_RIM, d))
    beach_w = smootherstep(420.0, 60.0, np.abs(H - 60.0)) * (1.0 - in_cliff) \
        * (1.0 - pad_core)
    H = H - (H - 60.0) * 0.45 * beach_w
    H = gauss3(H, passes=2, w=0.5 * smootherstep(420.0, 300.0, np.abs(H - 60.0))
               * (1.0 - in_cliff) * (1.0 - pad_core))

    # ---------------- crease cleanup on walkable land -----------------------
    land_mask = smoothstep(60.0, 160.0, H) * (1.0 - in_cliff)
    H = curvature_relax(H, passes=3, mask=land_mask, knee=45.0 * cell)

    # ---------------- ROADS (replaces min()-cone carving) -------------------
    # Legs/anchors are DERIVED from slots + arena specs + corridor verts (see
    # derive_route): slot drags and corridor edits in the layout editor keep
    # working without touching this code.
    rt = derive_route(layout, slots)
    fp, apron_z, a5 = rt["fp"], rt["apron_z"], rt["a5"]
    g3 = slots["G3"]
    appr_front, appr_z = rt["appr_front"], rt["appr_z"]
    bench_z, landing = rt["bench_z"], rt["landing"]
    st = layout["rules"].get("citadel_stairs")
    cit = slots["Citadel"]
    cy = math.radians(float(cit.get("yaw", 0.0)))
    ca_, sa_ = math.cos(cy), math.sin(cy)
    legA, legB = rt["legA"], rt["legB"]
    # legB (bench -> stairs landing) is built INSIDE the bowl section: built
    # here it would grade a trench through the not-yet-carved mountain
    H, road_d = build_road(H, legA, "legA", grade=road_grade)
    # SMOOTH keep-off-the-road weight (0 on the core, 1 beyond the wall foot).
    # RULE (caught in-engine 2026-06-12): NO BINARY masks on height ops - a
    # binary core mask here alternated cut/no-cut texels along the diagonal
    # core boundary = sawtooth road walls; same class as the strip pyramids.
    road_keep = smoothstep(430.0, 830.0, road_d)

    # ---------------- PADS: footprint-exact aprons (G1, G2) -----------------
    # (the road core is exempt: its profile already equals the apron inside -
    # edge anchors - and keeps its own graded climb through the blend collar)
    collar = np.zeros_like(H)
    for sid in ("G1", "G2"):
        s_ = slots[sid]
        f = fp[sid]
        lx, ly = local_frame(Xu, Yu, s_)
        dist = rect_dist(lx, ly, f["x0"] - 500.0, f["x1"] + 500.0,
                         f["y0"] - 500.0, f["y1"] + 500.0)
        # binary INSIDE term is safe (the stamp weight is uniformly 1 there -
        # nothing to alternate); extending it smoothly OUTSIDE dragged the
        # road core at the apron exits (34.7 deg dip-step)
        keep = np.maximum(road_keep, (dist <= 0.0).astype(float))
        # same shallow-blend / deep-terrace construction as the road walls:
        # the C2 blend alone peaked at 41-48 deg where the hill stands 1600+
        # over the apron (G2 east rim)
        H_pre = H.copy()
        w = smootherstep(3400.0, 0.0, dist) * keep
        H = H * (1 - w) + apron_z[sid] * w
        wall_p = dist * FUNNEL_GRADE
        terrace = np.minimum(np.maximum(H_pre, apron_z[sid] - wall_p),
                             apron_z[sid] + wall_p)
        steep = smootherstep(700.0, 1400.0, np.abs(H_pre - apron_z[sid]))
        tgt = H * (1.0 - steep) + terrace * steep
        fea = smootherstep(6400.0, 5600.0, dist)
        H = H + (tgt - H) * keep * fea
        collar = np.maximum(collar, smoothstep(0.0, 300.0, dist)
                            * smoothstep(4800.0, 4200.0, dist))

    # citadel: keep the proven wide disc
    s_ = slots["Citadel"]
    rf = s_["r"] + PAD_RIM
    d = np.hypot(Xu - s_["pos"][0], Yu - s_["pos"][1])
    wp = smootherstep(rf + 6200.0, rf, d)
    H = H * (1 - wp) + s_["pos"][2] * wp

    # seam relax just outside the aprons (never the cores)
    seam = np.zeros_like(H)
    for sid in ("G1", "G2"):
        s_ = slots[sid]
        f = fp[sid]
        lx, ly = local_frame(Xu, Yu, s_)
        dist = rect_dist(lx, ly, f["x0"] - 500.0, f["x1"] + 500.0,
                         f["y0"] - 500.0, f["y1"] + 500.0)
        seam = np.maximum(seam, smoothstep(4600.0, 800.0, dist)
                          * smoothstep(-100.0, 800.0, dist))
    d = np.hypot(Xu - s_["pos"][0], Yu - s_["pos"][1])
    seam = np.maximum(seam, smoothstep(rf + 4600.0, rf + 800.0, d)
                      * smoothstep(rf - 200.0, rf + 800.0, d))
    H = gauss3(H, passes=2, w=seam * 0.7 * road_keep)

    # ---------------- G3 BOWL: exact carve under A5 --------------------------
    # NO BINARY MASKS on height ops (caught in-engine 2026-06-12): a per-texel
    # inside/outside decision between surfaces 100+ uu apart reads as a row of
    # pyramids along the rotated boundary. Caps are made CONSISTENT with the
    # contact strips instead (max-lift), strips run unmasked, and every strip
    # application is followed by the inside-cap trim.
    g3z = g3["pos"][2] + ARENA_LIFT
    lx, ly = local_frame(Xu, Yu, g3)
    rx0, rx1 = a5["x0"] - 50.0, a5["x1"] + 50.0
    ry0, ry1 = a5["y0"] - 50.0, a5["y1"] + 150.0
    inside = (lx >= rx0) & (lx <= rx1) & (ly >= ry0) & (ly <= ry1)

    # INSIDE cap: terrace floor bands (steps 150 uu before each seam, hidden
    # under the upper slab) + under-plate/under-wall ramps that keep the dirt
    # CONTINUOUS with the outside contacts. The ramps are SMOOTH and live
    # entirely inside slab/wall bodies - hard rect edges here smear through
    # bilinear sampling into false floor protrusions at the lips.
    cap_in = g3z + np.where(ly < -1550.0, 0.0,
                            np.where(ly < 1050.0, -350.0, -700.0)) - BOWL_CLEAR
    # behind the T2 slab edge the dirt climbs to bench level across the
    # bort_s body (slab covers to 4000, the wall 4000..4100)
    cap_in = np.maximum(cap_in, g3z - 850.0
                        + 370.0 * smoothstep(3980.0, 4100.0, ly))
    # entry plate belly (-100): dirt rises to -60 under the plate body.
    # The lift is GATED to its window (its fringe value equals the band cap
    # there, so the gate is seamless) - an ungated max() leaked the baseline
    # over the whole bowl and buried T2 by +550.
    appr_f = (smoothstep(760.0, 640.0, np.abs(lx))
              * smoothstep(-4590.0, -4530.0, ly) * smoothstep(-4350.0, -4410.0, ly))
    cap_in = np.maximum(cap_in, np.where(appr_f > 0.0,
                                         (g3z - 150.0) + 90.0 * appr_f, -1e9))
    # side-door plate belly (-450): dirt rises to -410 under it (T1 band)
    side_f = (smoothstep(a5["x0"] + 250.0, a5["x0"] + 50.0, lx)
              * smoothstep(650.0, 550.0, np.abs(ly - 200.0)))
    cap_in = np.maximum(cap_in, np.where(side_f > 0.0,
                                         (g3z - 500.0) + 90.0 * side_f, -1e9))

    dist = rect_dist(lx, ly, rx0, rx1, ry0, ry1)
    outside = dist > 0.0
    # funnel walls outside: SMOOTH band ramps (the hard 350-steps of the
    # inside bands would make diagonal step-walls along the funnel)
    ly_cl = np.clip(ly, ry0, ry1)
    edge_floor = (-350.0 * smoothstep(-1550.0, -1250.0, ly_cl)
                  - 350.0 * smoothstep(1050.0, 1350.0, ly_cl))
    cap_out = g3z + edge_floor - BOWL_CLEAR + dist * FUNNEL_GRADE

    # contact strips: entry approach (small fade - the road already arrives
    # at plate level), west side door, exit bench behind bort_s
    appr_w = smootherstep(500.0, 150.0, np.hypot(
        np.maximum(np.abs(lx) - 650.0, 0.0),
        np.maximum(np.maximum(a5["y0"] - 350.0 - ly, ly - (a5["y0"] + 350.0)), 0.0)))
    side_w = smootherstep(800.0, 200.0, np.hypot(
        np.maximum(np.maximum((a5["x0"] - 450.0) - lx, lx - (a5["x0"] + 150.0)), 0.0),
        np.maximum(np.abs(ly - 200.0) - 400.0, 0.0)))
    bench_w = smootherstep(1500.0, 500.0, np.hypot(
        np.maximum(np.abs(lx) - 2600.0, 0.0),
        np.maximum(np.maximum(4120.0 - ly, ly - 5300.0), 0.0)))
    strips = ((appr_w, g3z - PLATE_GAP), (side_w, g3z - 350.0 - PLATE_GAP),
              (bench_w, g3z - 450.0 - PLATE_GAP))
    # the funnel cap must never cut a strip: lift it under each strip fade
    cap_out_l = cap_out
    for w_, t_ in strips:
        cap_out_l = np.maximum(cap_out_l, t_ - (1.0 - w_) * 3000.0)

    def assert_bowl(Hv):
        """Caps + contact strips + inward-fade trim, in the safe order."""
        Hv = np.where(inside, np.minimum(Hv, cap_in), Hv)
        Hv = np.where(outside, np.minimum(Hv, cap_out_l), Hv)
        for w_, t_ in strips:
            Hv = Hv * (1.0 - w_) + t_ * w_
        return np.where(inside, np.minimum(Hv, cap_in), Hv)

    H = assert_bowl(H)
    # the exit road to the citadel-stairs landing: built AFTER the bowl has
    # opened the bench (grading it against the raw mountain cut a trench)
    H, road_dB = build_road(H, legB, "legB", grade=road_grade)
    road_d = np.minimum(road_d, road_dB)
    road_keep = smoothstep(430.0, 830.0, road_d)
    # relax the funnel ring, then re-assert (relax must not push dirt back
    # over the terraces); road cores/contacts keep their exact grades
    ring = smoothstep(3200.0, 600.0, dist) * smoothstep(-50.0, 500.0, dist)
    H = gauss3(H, passes=3, w=ring * 0.7 * road_keep)
    H = assert_bowl(H)

    # ---------------- stamp-seam polish ---------------------------------------
    # Curvature-targeted relax over the road walls, pad collars and bowl ring:
    # the piecewise cap/fill surfaces (cut wall x ravine, wall x hillside)
    # intersect in sharp seams - this melts the seams and leaves the planar
    # 32 deg walls alone. Cores/contacts excluded, caps re-asserted after.
    road_band = smoothstep(380.0, 700.0, road_d) * smoothstep(3800.0, 3100.0, road_d)
    seam_mask = (np.maximum(road_band, np.maximum(collar, ring))
                 * road_keep * (1.0 - in_cliff)
                 * (1.0 - np.maximum(appr_w, np.maximum(side_w, bench_w))))
    H = curvature_relax(H, passes=5, mask=seam_mask, strength=0.9, knee=28.0 * cell)
    H = assert_bowl(H)

    # despike: lone texels vs the 8-neighbour median render as pyramids -
    # replace them (stamp-interaction residue). TWO passes: a LINE of spikes
    # hides its members from a single median pass. The innermost road core
    # (anchors/contacts) stays exact; a median preserves planar walls.
    for _ in range(4):
        nb = [np.roll(np.roll(H, dy, 0), dx, 1)
              for dy in (-1, 0, 1) for dx in (-1, 0, 1) if (dy, dx) != (0, 0)]
        med8 = np.median(np.stack(nb), axis=0).astype(np.float32)
        del nb
        spike = (np.abs(H - med8) > 45.0 * cell) & (road_d > 250.0)
        n_sp = int(spike.sum())
        if not n_sp:
            break
        H = np.where(spike, med8, H)
        H = assert_bowl(H)
        print("despiked %d lone texel(s)" % n_sp)

    # ---------------- CLIFF STRATA: terraced rock courses (art, 2026-06-12) --
    # The cape's seaward face reads as a smooth dune; the stylized-game canon
    # for low-poly cliffs is TERRACED STRATA cut into the terrain itself
    # (mesh masonry and material-only both failed art review - silhouette).
    # Quantize H into ~550 uu courses inside the cliff sector: flat tread,
    # smootherstep riser, phase wobbled by the warped detail field so course
    # lines stay organic. All weights SMOOTH (iron rule); the road/stairs
    # corridors keep their grades (road_keep; the stairs block re-flattens
    # its landing and decks right below).
    strata_step = 550.0
    strata_phase = (Dw - 0.5) * 260.0
    q_s = (H + strata_phase) / strata_step
    frac_s = q_s - np.floor(q_s)
    H_strata = (np.floor(q_s) + smootherstep(0.50, 0.92, frac_s)) * strata_step \
        - strata_phase
    gy_s, gx_s = np.gradient(H, SCALE)
    sl_s = np.degrees(np.arctan(np.hypot(gx_s, gy_s)))
    w_strata = (in_cliff * smoothstep(26.0, 36.0, sl_s)
                * smoothstep(250.0, 600.0, H) * road_keep)
    H = H * (1.0 - w_strata) + H_strata * w_strata
    print("cliff strata: %.1f%% of map terraced (max weight %.2f)"
          % (float((w_strata > 0.5).mean()) * 100.0, float(w_strata.max())))

    # ---------------- author's citadel staircase: applied LAST ---------------
    # exact top-surface center-lines from actor transforms; clearance cap under
    # the flights, landing meets the lower tip; numeric verification below
    if st:
        def st_world(loc):
            wx = cit["pos"][0] + loc[0] * ca_ - loc[1] * sa_
            wy = cit["pos"][1] + loc[0] * sa_ + loc[1] * ca_
            return wx, wy, cit["pos"][2] + loc[2]

        def cap_disc(cx_, cy2_, cap_z, R_uu, flat_r=600.0):
            # FLAT-BOTTOM cap: full depth across the deck width + margin, the
            # cone only rises beyond flat_r (terrain must never graze the deck
            # sides - caught by the author 2026-06-11)
            R_ = max(1, int(round(R_uu / SCALE)))
            r_ = int(round((cy2_ + HALF) / SCALE))
            c_ = int(round((cx_ + HALF) / SCALE))
            r0_, r1_ = max(0, r_ - R_), min(N, r_ + R_ + 1)
            c0_, c1_ = max(0, c_ - R_), min(N, c_ + R_ + 1)
            yy_, xx_ = np.mgrid[r0_ - r_:r1_ - r_, c0_ - c_:c1_ - c_]
            dd_ = np.clip(np.hypot(yy_, xx_) * SCALE - flat_r, 0.0, None)
            cap_ = cap_z + dd_ * 0.55
            H[r0_:r1_, c0_:c1_] = np.minimum(H[r0_:r1_, c0_:c1_], cap_)

        for line in st["lines_top_local"]:
            ax_, ay_, az_ = st_world(line[0])
            bx_, by_, bz_ = st_world(line[1])
            L_ = max(np.hypot(bx_ - ax_, by_ - ay_), 1.0)
            kk = int(L_ / 150.0) + 2
            for i_ in range(kk):
                t_ = i_ / (kk - 1.0)
                cap_disc(ax_ + (bx_ - ax_) * t_, ay_ + (by_ - ay_) * t_,
                         az_ + (bz_ - az_) * t_ - 150.0, 2000.0)
        px_, py_, pz_ = st_world(st["platform_local"])
        cap_disc(px_, py_, pz_ - 160.0, 1600.0, flat_r=750.0)
        lx2, ly2, lz = st_world(st["landing_local"])
        d_l = np.hypot(Xu - lx2, Yu - ly2)
        wl = smoothstep(3400.0, 1700.0, d_l)
        H = H * (1 - wl) + (lz - 30.0) * wl
        # soften the carve-trench walls around the flights WITHOUT touching the
        # protected strip near the decks (no re-grazing; gates verify below)
        d_st = np.full_like(H, 1e9)
        for line in st["lines_top_local"]:
            ax_, ay_, _ = st_world(line[0])
            bx_, by_, _ = st_world(line[1])
            seg_ = np.array([bx_ - ax_, by_ - ay_])
            t_ = np.clip(((Xu - ax_) * seg_[0] + (Yu - ay_) * seg_[1]) / (seg_ @ seg_), 0, 1)
            d_st = np.minimum(d_st, np.hypot(Xu - (ax_ + t_ * seg_[0]),
                                             Yu - (ay_ + t_ * seg_[1])))
        w_sm = smoothstep(3800.0, 1400.0, d_st) * smoothstep(750.0, 1250.0, d_st)
        H = gauss3(H, passes=4, w=w_sm * 0.85)
        # numeric verification: clearance along each flight + tip contact
        for li, line in enumerate(st["lines_top_local"]):
            ax_, ay_, az_ = st_world(line[0])
            bx_, by_, bz_ = st_world(line[1])
            worst = -1e9
            lx0, ly0, _ = st_world(st["landing_local"])
            L2_ = max(np.hypot(bx_ - ax_, by_ - ay_), 1.0)
            nx_, ny_ = -(by_ - ay_) / L2_, (bx_ - ax_) / L2_   # lateral unit
            kk = int(L2_ / 200.0) + 2
            for i_ in range(kk):
                t_ = i_ / (kk - 1.0)
                bxx_ = ax_ + (bx_ - ax_) * t_
                byy_ = ay_ + (by_ - ay_) * t_
                if np.hypot(bxx_ - lx0, byy_ - ly0) < 1800.0:
                    continue  # the landing zone MEETS the deck by design
                top_ = az_ + (bz_ - az_) * t_
                for off_ in (-450.0, 0.0, 450.0):   # sides AND center
                    r_ = int(round((byy_ + ny_ * off_ + HALF) / SCALE))
                    c_ = int(round((bxx_ + nx_ * off_ + HALF) / SCALE))
                    worst = max(worst, H[r_, c_] - (top_ - 40.0))
            print("stairs flight %d: max protrusion above deck-40: %.0f uu %s"
                  % (li + 1, worst, "OK" if worst <= 0 else "**BURIED**"))
        r_ = int(round((ly2 + HALF) / SCALE))
        c_ = int(round((lx2 + HALF) / SCALE))
        gap = (lz - 30.0) - H[r_, c_]
        print("stairs landing: terrain-to-tip gap %.0f uu %s"
              % (gap, "OK" if abs(gap) <= 40 else "**MISFIT**"))

    # the stairs-landing flatten bleeds over the bowl's SE corner (they are
    # only ~2.3k apart) - re-assert the A5 caps. Min only, no smoothing:
    # cannot bury the stairs decks (the landing center lies outside every
    # cap; funnel there is ~9.4k, well above). Then LIFT the contact strips
    # back (max only: never pull DOWN - the bench far end riding up into the
    # landing flatten IS the designed climb, re-setting it left a 33 deg
    # seam), and trim the inward fades with the inside cap.
    H = np.where(inside, np.minimum(H, cap_in), H)
    H = np.where(outside, np.minimum(H, cap_out_l), H)
    for w_, t_ in strips:
        H = np.maximum(H, H * (1 - w_) + t_ * w_)
    H = np.where(inside, np.minimum(H, cap_in), H)

    # ---------------- verification on the FINAL surface ----------------------
    report_leg(H, legA, "legA")
    report_leg(H, legB, "legB")
    # sampling windows stay clear of the under-plate ramps and the wall body
    # (the ramps deliberately rise above the neighbouring band there)
    bands = (("T0", -4340.0, -1560.0, 0.0), ("T1", -1240.0, 1040.0, -350.0),
             ("T2", 1360.0, 3940.0, -700.0))
    for name, b0, b1, ftop in bands:
        worst = -1e9
        for lyy in np.arange(b0 + 120.0, b1 - 60.0, 240.0):
            for lxx in np.arange(a5["x0"] + 60.0, a5["x1"] - 50.0, 240.0):
                wxx, wyy = slot_world(g3, lxx, lyy)
                r_, c_ = world_to_idx((wxx, wyy))
                worst = max(worst, H[r_, c_] - (g3z + ftop))
        # -30 = designed contact gap at plate lips; anything above -20 is real
        print("A5 %s: terrain vs floor top %+.0f uu %s"
              % (name, worst, "OK" if worst <= -20.0 else "**BURIED**"))
    for sid in ("G1", "G2"):
        s_ = slots[sid]
        r_, c_ = world_to_idx((s_["pos"][0], s_["pos"][1]))
        print("%s apron: H=%.0f (target %.0f)" % (sid, H[r_, c_], apron_z[sid]))

    px16 = np.clip(32768.0 + (H - SEA_FLOOR) * 1.28, 0, 65535).astype(np.uint16)
    out_png = args.out or os.path.join(
        OUT_DIR, "biome1_heightmap_2017{}.png".format(suffix))
    if args.res != 2017 and args.out is None:
        out_png = os.path.join(OUT_DIR, "biome1_heightmap_fast.png")
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
    pim.save(args.preview or os.path.join(
        OUT_DIR, "biome1_heightmap_preview{}.png".format(suffix)))

    def probe(label, x, y, want=None):
        i = int(round((y + HALF) / SCALE))
        j = int(round((x + HALF) / SCALE))
        print("%-12s H=%7.0f%s" % (label, H[i, j],
              "" if want is None else "  (want %d)" % want))

    for sid in ("M1", "M2", "M3", "M4", "Citadel"):
        s_ = slots[sid]
        probe(sid, s_["pos"][0], s_["pos"][1], s_["pos"][2])
    probe("G1 apron", slots["G1"]["pos"][0], slots["G1"]["pos"][1], apron_z["G1"])
    probe("G2 apron", slots["G2"]["pos"][0], slots["G2"]["pos"][1], apron_z["G2"])
    probe("G3 bowl ctr", g3["pos"][0], g3["pos"][1],
          g3z - 350.0 - BOWL_CLEAR)
    probe("A5 approach", appr_front[0], appr_front[1], appr_z)
    bench_xy = slot_world(g3, 0.0, 4500.0)
    probe("A5 bench", bench_xy[0], bench_xy[1], bench_z)
    probe("shoulder", *slots["shoulder"]["pos"][:2])
    probe("cliff slope", 44000, -1000)
    probe("open sea", 70000, -70000)
    print("max H %.0f" % H.max())
    print("written:", out_png)


if __name__ == "__main__":
    main()
