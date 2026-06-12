# Biome1 island ART PASS source: bakes the 4 landscape weight masks
# (Sand_01 / Rockwall / Grass / Grass_Clovers - the exact layer set the
# author painted on Lvl_DemoForPublisher2) + a deterministic foliage plan,
# all OFFLINE from the already-authored heightmap PNG. PREVIEW WORKFLOW:
# review biome1_artpass_preview.png before apply_biome1_art_pass.py touches
# the editor.
#
# Art rules (agreed with the author 2026-06-12):
#   - sand: waterline band + the whole seabed + maldive/reef shore slopes
#     + the serpentine ribbon (readable route, kills procedural grass on it)
#   - rock: PURELY slope-driven (28->38 deg; the cape cliff sector bites
#     earlier at 20->30) - plateaus stay green all the way to the citadel
#   - clovers: low-frequency noise patches inside the grass
#   - vegetation niches: coastal palms -> lowland jungle -> thinning upland;
#     groves from noise + Poisson spacing, understory clustered AROUND trees
#     (plants live in families - the main "natural" reader), flowers on
#     clover meadows and road shoulders, driftwood/seaweed along the sand
#   - hard keep-outs: road ribbon, G1/G2 aprons, G3 bowl, citadel disc +
#     stairs corridors, maldive arena circles (palms only on the rim ring),
#     start reef (author's launch point!), bridge corridors, water
#
# Outputs (Island/):
#   biome1_weight_Sand_01.png / _Rockwall.png / _Grass.png / _Grass_Clovers.png
#     (8-bit grayscale, 2017x2017, same array orientation as the heightmap -
#      per-pixel byte sum == 255)
#   biome1_foliage_plan.json   (per-foliage-type instance lists, world XY+Z)
#   biome1_artpass_preview.png (shaded, UE TOP VIEW: +X up, +Y right)
#
# Run locally:  python make_biome1_weightmaps.py [--seed N]
# Keep zone math in sync with make_biome1_heightmap.py (imports its helpers).

import argparse
import json
import math
import os

import numpy as np
from PIL import Image

import make_biome1_heightmap as mh

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(TOOLS_DIR, "Island")
LAYOUT = os.path.join(TOOLS_DIR, "Island", "biome1_island_layout.json")
HEIGHTMAP = os.path.join(TOOLS_DIR, "Island", "biome1_heightmap_2017.png")
DEFAULT_SEED = 20260612

N, SCALE, HALF, SEA_FLOOR, PAD_RIM = mh.N, mh.SCALE, mh.HALF, mh.SEA_FLOOR, mh.PAD_RIM
ENV = "/Game/PolygonNatureBiomes/Models/Environment/"

# foliage type assets (verified in-editor 2026-06-12: "1"/"2"/"3" wrap the
# Forest trees 03/02/01)
FT_PALM = [ENV + "SM_Env_Tree_Palm_0{}_FoliageType".format(i) for i in (1, 2, 3, 4)]
FT_BANANA = [ENV + "SM_Env_Tree_Banana_0{}_FoliageType".format(i) for i in (1, 2, 3)]
FT_FOREST = [ENV + "3", ENV + "2", ENV + "1"]          # Forest_01, _02, _03
FT_POHUT = [ENV + "SM_Env_Tree_Pohutukawa_0{}_FoliageType".format(i) for i in (1, 2, 3, 4)]
FT_BUSH_T = [ENV + "SM_Env_Bush_Tropical_0{}_FoliageType".format(i) for i in (1, 2, 3, 4)]
FT_BUSH_P = [ENV + "SM_Env_Bush_Palm_0{}_FoliageType".format(i) for i in (1, 2, 3, 4)]
FT_FERN = [ENV + "SM_Env_Fern_0{}_FoliageType".format(i) for i in (1, 2, 3, 4)]
FT_TALLGRASS = [ENV + "SM_Env_Grass_Tall_Clump_0{}_FoliageType".format(i) for i in (1, 2, 3)]
FT_FLOWERS = ([ENV + "SM_Env_Flowers_0{}_FoliageType".format(i) for i in (1, 2, 3, 4, 5)]
              + [ENV + "SM_Env_Flower_0{}_FoliageType".format(i) for i in (1, 2, 3)])
FT_DRIFT = [ENV + "SM_Env_DriftWood_0{}_FoliageType".format(i) for i in (1, 2, 3, 4, 5)]
FT_SEAWEED = [ENV + "SM_Env_Seaweed_Beach_01_FoliageType"]

# per-type placement params (align trees upright; small stuff hugs the ground)
TYPE_PARAMS = {}
for p in FT_PALM:
    TYPE_PARAMS[p] = dict(min_scale=0.9, max_scale=1.3, align_to_normal=False)
for p in FT_BANANA + FT_FOREST + FT_POHUT:
    TYPE_PARAMS[p] = dict(min_scale=0.85, max_scale=1.2, align_to_normal=False)
for p in FT_BUSH_T + FT_BUSH_P:
    TYPE_PARAMS[p] = dict(min_scale=0.7, max_scale=1.3, align_to_normal=True)
for p in FT_FERN:
    TYPE_PARAMS[p] = dict(min_scale=0.6, max_scale=1.2, align_to_normal=True)
for p in FT_TALLGRASS:
    TYPE_PARAMS[p] = dict(min_scale=0.8, max_scale=1.3, align_to_normal=True)
for p in FT_FLOWERS:
    TYPE_PARAMS[p] = dict(min_scale=0.8, max_scale=1.2, align_to_normal=True)
for p in FT_DRIFT + FT_SEAWEED:
    TYPE_PARAMS[p] = dict(min_scale=0.7, max_scale=1.25, align_to_normal=True)


def smoothstep(e0, e1, x):
    return mh.smoothstep(e0, e1, x)


def seg_dist_field(pts, reach):
    """Min distance (uu) to a polyline, rasterized in windows (same approach
    as build_road); cells beyond `reach` stay at 1e9."""
    D = np.full((N, N), 1e9, dtype=np.float32)
    Rc = int(reach / SCALE) + 2
    for a, b in zip(pts, pts[1:]):
        ax, ay = float(a[0]), float(a[1])
        bx, by = float(b[0]), float(b[1])
        steps = max(1, int(math.hypot(bx - ax, by - ay) / (Rc * SCALE * 0.9)) + 1)
        for s in range(steps):
            t0, t1 = s / steps, (s + 1) / steps
            sx0, sy0 = ax + (bx - ax) * t0, ay + (by - ay) * t0
            sx1, sy1 = ax + (bx - ax) * t1, ay + (by - ay) * t1
            r0, c0 = mh.world_to_idx(((sx0 + sx1) * 0.5, (sy0 + sy1) * 0.5))
            ext = int(math.hypot(sx1 - sx0, sy1 - sy0) / SCALE * 0.5) + Rc
            rr0, rr1 = max(0, r0 - ext), min(N, r0 + ext + 1)
            cc0, cc1 = max(0, c0 - ext), min(N, c0 + ext + 1)
            if rr0 >= rr1 or cc0 >= cc1:
                continue
            xs = np.arange(cc0, cc1, dtype=np.float32) * SCALE - HALF
            ys = np.arange(rr0, rr1, dtype=np.float32) * SCALE - HALF
            Xw, Yw = np.meshgrid(xs, ys)
            vx, vy = sx1 - sx0, sy1 - sy0
            ss = max(vx * vx + vy * vy, 1.0)
            t = np.clip(((Xw - sx0) * vx + (Yw - sy0) * vy) / ss, 0.0, 1.0)
            d = np.hypot(Xw - (sx0 + t * vx), Yw - (sy0 + t * vy))
            win = D[rr0:rr1, cc0:cc1]
            np.minimum(win, d, out=win)
    return D


def build_road_legs(slots, layout):
    """Replicate the legA/legB polylines of make_biome1_heightmap (geometry
    only - no height anchors needed for painting). Keep in sync."""
    fp = {sid: mh.load_footprint(slots[sid]["default"]) for sid in ("G1", "G2", "G3")}
    g1, g2, g3, cit = slots["G1"], slots["G2"], slots["G3"], slots["Citadel"]
    corr_pts = list(layout["rules"]["route_corridor"])
    shoulder = corr_pts[0]
    a5 = fp["G3"]
    appr_front = mh.slot_world(g3, 0.0, a5["y0"] - 100.0)
    e_g1_sh = mh.apron_edge_point(g1, fp["G1"], shoulder)
    e_g1_g2 = mh.apron_edge_point(g1, fp["G1"], g2["pos"][:2])
    e_g2_g1 = mh.apron_edge_point(g2, fp["G2"], g1["pos"][:2])
    e_g2_ap = mh.apron_edge_point(g2, fp["G2"], appr_front)
    legA = [tuple(shoulder), e_g1_sh, (g1["pos"][0], g1["pos"][1]), e_g1_g2,
            e_g2_g1, (g2["pos"][0], g2["pos"][1]), e_g2_ap,
            (appr_front[0], appr_front[1])]
    st = layout["rules"]["citadel_stairs"]
    cy = math.radians(float(cit.get("yaw", 0.0)))
    ca, sa = math.cos(cy), math.sin(cy)
    ll = st["landing_local"]
    landing = (cit["pos"][0] + ll[0] * ca - ll[1] * sa,
               cit["pos"][1] + ll[0] * sa + ll[1] * ca)
    bench_exit = mh.slot_world(g3, 1103.0, 5300.0)
    legB = [bench_exit, landing]
    stair_lines = []
    for line in st["lines_top_local"]:
        pts = []
        for loc in line:
            pts.append((cit["pos"][0] + loc[0] * ca - loc[1] * sa,
                        cit["pos"][1] + loc[0] * sa + loc[1] * ca))
        stair_lines.append(pts)
    return legA, legB, stair_lines, fp


def thin_by_spacing(pts, spacing, rng):
    """Random-order greedy thinning on a grid hash -> Poisson-ish layout."""
    if not len(pts):
        return np.zeros((0, 2))
    pts = np.asarray(pts, dtype=np.float64)
    order = rng.permutation(len(pts))
    cell = spacing
    taken = {}
    keep = []
    for i in order:
        x, y = pts[i]
        cx, cy = int(x // cell), int(y // cell)
        ok = True
        for gx in (cx - 1, cx, cx + 1):
            for gy in (cy - 1, cy, cy + 1):
                for j in taken.get((gx, gy), ()):
                    if (x - pts[j][0]) ** 2 + (y - pts[j][1]) ** 2 < spacing * spacing:
                        ok = False
                        break
                if not ok:
                    break
            if not ok:
                break
        if ok:
            taken.setdefault((cx, cy), []).append(i)
            keep.append(i)
    return pts[keep]


def sample_field(field, count, rng, spacing):
    """Draw candidate pixels WEIGHTED by the density field (uniform rejection
    over a 2x2 km map starves narrow zones - caught on first run), jitter
    inside the pixel, then Poisson-thin by `spacing`."""
    flat = field.astype(np.float64).ravel()
    s = flat.sum()
    if s <= 0.0:
        return np.zeros((0, 2))
    n_draw = count * 6
    idx = rng.choice(flat.size, size=n_draw, replace=True, p=flat / s)
    r, c = np.unravel_index(idx, field.shape)
    x = c * SCALE - HALF + rng.uniform(-SCALE * 0.5, SCALE * 0.5, n_draw)
    y = r * SCALE - HALF + rng.uniform(-SCALE * 0.5, SCALE * 0.5, n_draw)
    pts = thin_by_spacing(np.stack([x, y], axis=1), spacing, rng)
    return pts[:count]


def at(field, x, y):
    r = int(round((y + HALF) / SCALE))
    c = int(round((x + HALF) / SCALE))
    if 0 <= r < N and 0 <= c < N:
        return float(field[r, c])
    return 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=DEFAULT_SEED,
                    help="ART seed (noise patches, vegetation sampling)")
    ap.add_argument("--terrain-seed", type=int, default=mh.DEFAULT_SEED,
                    help="seed the heightmap PNG was generated with - the "
                         "maldive lens fields are replicated from it")
    args = ap.parse_args()
    rng = np.random.default_rng(args.seed)

    with open(LAYOUT, encoding="utf-8") as f:
        layout = json.load(f)
    slots = {s["id"]: s for s in layout["slots"]}
    isl = layout["island"]

    px = np.asarray(Image.open(HEIGHTMAP), dtype=np.float32)
    assert px.shape == (N, N), "heightmap is {}".format(px.shape)
    H = SEA_FLOOR + (px - 32768.0) / 1.28

    coord = np.linspace(-HALF, HALF, N, dtype=np.float32)
    Xu, Yu = np.meshgrid(coord, coord)

    gy, gx = np.gradient(H, SCALE)
    slope = np.degrees(np.arctan(np.hypot(gx, gy))).astype(np.float32)

    # cape cliff sector (same construction as the heightmap generator)
    ICX, ICY = isl["center"]
    CPX, CPY = isl["peak"]
    deg = np.degrees(np.arctan2(Yu - ICY, Xu - ICX)) % 360.0
    cape_bearing = math.degrees(math.atan2(CPY - ICY, CPX - ICX)) % 360.0
    ddeg = np.abs(((deg - cape_bearing + 180.0) % 360.0) - 180.0)
    in_cliff = smoothstep(26.0, 13.0, ddeg)

    legA, legB, stair_lines, fp = build_road_legs(slots, layout)
    road_d = np.minimum(seg_dist_field(legA, 3000.0), seg_dist_field(legB, 3000.0))
    stair_d = np.full_like(road_d, 1e9)
    for line in stair_lines:
        stair_d = np.minimum(stair_d, seg_dist_field(line, 2200.0))

    # ---------------- layer masks ----------------
    # rock threshold 29->36: >=30 deg is unwalkable by project rules, and the
    # author wants "пляж вместо обрыва" - the 28-30 deg coastal aprons stay
    # green (first run painted a black ring around every shore). The cape
    # cliff sector bites earlier so the seaward wall reads stone.
    w_rock = np.maximum(smoothstep(29.0, 36.0, slope),
                        smoothstep(20.0, 30.0, slope) * in_cliff)

    # MALDIVE SAND-LENS resync (terrain 2026-06-12, Handoff maldive section):
    # replicate the EXACT lens field t/run per islet - footprint rect dist
    # warped by the 5-harmonic coast wobble + spine term. Wobble phases come
    # from the same rng stream the heightmap consumed (TERRAIN seed: 2 island
    # draws, then 5 per S/M arena in slot order - verified against the
    # generator 2026-06-12), spine_n/Dw rebuilt with mh's own noise stack.
    lens_rng = np.random.default_rng(args.terrain_seed)
    lens_rng.uniform(0.0, 2.0 * np.pi, 2)            # island p1/p2 (consumed)
    idx = np.arange(N, dtype=np.float32)
    detail = mh.fbm(N, 5, 8, args.terrain_seed + 37)
    wx = (mh.fbm(N, 4, 4, args.terrain_seed + 11) - 0.5) * 160.0
    wy = (mh.fbm(N, 4, 4, args.terrain_seed + 23) - 0.5) * 160.0
    ys_w = idx[:, None] + wy
    xs_w = idx[None, :] + wx
    spine = mh.sample(mh.ridged(N, 6, 6, args.terrain_seed + 71), ys_w, xs_w)
    spine_n = (spine - spine.min()) / (spine.max() - spine.min())
    Dw = mh.sample(detail, ys_w, xs_w)
    del detail, wx, wy, ys_w, xs_w, spine

    maldive_zone = np.zeros_like(H)
    lens = {}                                        # sid -> (t, run)
    for s in layout["slots"]:
        if s["kind"] != "arena" or s.get("tier") not in ("S", "M"):
            continue
        x, y, z = s["pos"]
        f_m = mh.load_footprint(s["default"])
        lxm, lym = mh.local_frame(Xu, Yu, s)
        fp_d = mh.rect_dist(lxm, lym, f_m["x0"] - 250.0, f_m["x1"] + 250.0,
                            f_m["y0"] - 250.0, f_m["y1"] + 250.0)
        th = np.arctan2(Yu - y, Xu - x)
        ph = lens_rng.uniform(0.0, 2.0 * np.pi, 5)
        wob = (1.0 + 0.30 * np.sin(th + ph[0])
               + 0.18 * np.sin(2 * th + ph[1]) + 0.10 * np.sin(3 * th + ph[2])
               + 0.06 * np.sin(5 * th + ph[3]) + 0.04 * np.sin(7 * th + ph[4]))
        wob = np.clip(wob + 0.30 * (Dw - 0.5) * 2.0, 0.60, 1.40)
        gate = mh.smootherstep(0.0, 700.0, fp_d)
        t = np.maximum(fp_d / wob + (spine_n - 0.45) * 1300.0 * gate, 0.0)
        run = z * mh.MALDIVE_RUN_K + mh.MALDIVE_RUN_BASE
        lens[s["id"]] = (t.astype(np.float32), run)
        maldive_zone = np.maximum(maldive_zone,
                                  smoothstep(run + 700.0, run + 100.0, t))
    sx_, sy_, sz_ = slots["start_reef"]["pos"]
    reef_zone = smoothstep(3100.0, 2500.0, np.hypot(Xu - sx_, Yu - sy_))
    w_rock = w_rock * (1.0 - maldive_zone) * (1.0 - reef_zone)

    shoreline_jitter = (mh.fbm(N, 4, 9, args.seed + 301) - 0.5) * 170.0
    beach = 1.0 - smoothstep(140.0 + shoreline_jitter, 330.0 + shoreline_jitter, H)
    w_sand = beach.copy()
    w_sand = np.maximum(w_sand, smoothstep(950.0, 430.0, road_d))     # road ribbon
    # ARENA FOOTPRINTS = SAND, all of them (author review 2026-06-12: no
    # grass under any arena - blades poke through the floor plates).
    # Footprint bbox + 300 full, fade by +800.
    for sid in ("G1", "G2", "G3", "Citadel"):
        f = fp.get(sid) or mh.load_footprint(slots[sid]["default"])
        lx, ly = mh.local_frame(Xu, Yu, slots[sid])
        d = mh.rect_dist(lx, ly, f["x0"], f["x1"], f["y0"], f["y1"])
        w_sand = np.maximum(w_sand, smoothstep(800.0, 300.0, d))
    # the whole lens above water is SAND - these islets are sandbars now
    # (the old grass-top + sandy-ring split died with the flat-disc model)
    w_sand = np.maximum(w_sand, maldive_zone * smoothstep(-250.0, -120.0, H))
    w_sand = np.maximum(w_sand, reef_zone)                             # reef = all sand
    # author rule 4: islets are NOT pure sand - small grass patches around
    # the arenas (never under them: gated to footprint+350, dry crown only)
    patch_noise = smoothstep(0.46, 0.60, mh.fbm(N, 5, 34, args.seed + 631))
    grass_patch = np.zeros_like(H)
    for sid in ("M1", "M2", "M3", "M4"):
        s = slots[sid]
        t, run = lens[sid]
        f_m = mh.load_footprint(s["default"])
        lxm, lym = mh.local_frame(Xu, Yu, s)
        fpd = mh.rect_dist(lxm, lym, f_m["x0"], f_m["x1"], f_m["y0"], f_m["y1"])
        grass_patch = np.maximum(
            grass_patch,
            patch_noise * smoothstep(350.0, 700.0, fpd)
            * smoothstep(220.0, 320.0, H)
            * smoothstep(run * 0.62, run * 0.45, t))
    w_sand = w_sand * (1.0 - 0.97 * grass_patch)
    w_sand = w_sand * (1.0 - w_rock)

    # Grass_Clovers KILLED (author review 2026-06-12: "трава это просто
    # grass"). The layer stays in the material/assignment, but the mask is
    # all-zero - importing it ERASES the clover weights already painted.
    meadow_noise = smoothstep(0.52, 0.66, mh.fbm(N, 4, 7, args.seed + 113))
    w_clov = np.zeros_like(H)

    w_grass = np.clip(1.0 - w_rock - w_sand - w_clov, 0.0, 1.0)

    # byte-exact normalization (sum == 255, grass takes the remainder)
    b_rock = np.round(w_rock * 255.0)
    b_sand = np.round(np.minimum(w_sand * 255.0, 255.0 - b_rock))
    b_clov = np.round(np.minimum(w_clov * 255.0, 255.0 - b_rock - b_sand))
    b_grass = 255.0 - b_rock - b_sand - b_clov
    masks = {"Sand_01": b_sand, "Rockwall": b_rock,
             "Grass": b_grass, "Grass_Clovers": b_clov}
    for name, arr in masks.items():
        Image.fromarray(arr.astype(np.uint8), mode="L").save(
            os.path.join(OUT_DIR, "biome1_weight_{}.png".format(name)))
        print("mask %-13s coverage %5.1f%% (mean weight)" %
              (name, float(arr.mean()) / 2.55))

    # ---------------- keep-out fields for foliage ----------------
    g1, g2, g3, cit = slots["G1"], slots["G2"], slots["G3"], slots["Citadel"]
    pad_d = np.full_like(H, 1e9)
    for sid in ("G1", "G2"):
        f = fp[sid]
        lx, ly = mh.local_frame(Xu, Yu, slots[sid])
        pad_d = np.minimum(pad_d, mh.rect_dist(
            lx, ly, f["x0"] - 500.0, f["x1"] + 500.0, f["y0"] - 500.0, f["y1"] + 500.0))
    a5 = fp["G3"]
    lx, ly = mh.local_frame(Xu, Yu, g3)
    bowl_d = mh.rect_dist(lx, ly, a5["x0"] - 50.0, a5["x1"] + 50.0,
                          a5["y0"] - 50.0, a5["y1"] + 150.0)
    cit_d = np.hypot(Xu - cit["pos"][0], Yu - cit["pos"][1]) - (cit["r"] + PAD_RIM)
    shoulder_d = np.hypot(Xu - slots["shoulder"]["pos"][0],
                          Yu - slots["shoulder"]["pos"][1])

    # bridge corridors: maldive hops where the line runs over water/beach
    hops = [("M1", "M2"), ("M2", "M3"), ("M3", "M4")]
    bridge_d = np.full_like(H, 1e9)
    for a_id, b_id in hops:
        bridge_d = np.minimum(bridge_d, seg_dist_field(
            [slots[a_id]["pos"][:2], slots[b_id]["pos"][:2]], 1400.0))
    bridge_d = np.minimum(bridge_d, seg_dist_field(
        [slots["M4"]["pos"][:2], (ICX, ICY)], 1400.0))
    bridge_keep = (bridge_d < 1000.0) & (H < 500.0)   # only near the water

    def keepout(tree):
        """1 = plantable. `tree` widens road/pad margins for canopies.
        Maldives + reef are FULLY excluded - their planting is the dedicated
        per-slot ring pass (global fields flooded the rims on first run)."""
        ok = np.ones_like(H)
        ok *= (H > 80.0)
        ok *= (road_d > (1100.0 if tree else 600.0))
        ok *= (pad_d > (1100.0 if tree else 350.0))
        ok *= (bowl_d > (2200.0 if tree else 700.0))
        ok *= (cit_d > (1600.0 if tree else 700.0))
        ok *= (stair_d > 1300.0)
        ok *= ~bridge_keep
        ok *= (maldive_zone < 0.5)
        ok *= (reef_zone < 0.5)
        if tree:
            ok *= (shoulder_d > 2500.0)
        return ok

    keep_tree = keepout(True)
    keep_small = keepout(False)

    # ---------------- vegetation fields ----------------
    grove = smoothstep(0.46, 0.62, mh.fbm(N, 4, 6, args.seed + 211))
    alt_thin = 1.0 - 0.45 * smoothstep(6000.0, 12000.0, H)   # plateaus stay green
    grass_w = masks["Grass"] / 255.0 + masks["Grass_Clovers"] / 255.0

    jungle_f = (smoothstep(26.0, 16.0, slope) * grove * alt_thin
                * smoothstep(250.0, 500.0, H) * grass_w * keep_tree)
    # lone trees OUTSIDE the groves: thin savanna fill so the open east lawn
    # is not sterile (the grove noise left it empty on first run)
    lone_f = ((1.0 - grove) * smoothstep(24.0, 14.0, slope) * alt_thin
              * smoothstep(250.0, 500.0, H) * grass_w * keep_tree)
    # palm GROVES, not a bead string: noise gates which coast stretches get
    # palms (cells 8 ~ 25k features: a dozen grove/gap alternations around
    # the perimeter; cells 5 left 12 palms total - whole-ring misses)
    palm_grove = smoothstep(0.40, 0.54, mh.fbm(N, 4, 8, args.seed + 421))
    # palms NEVER touch water (author review 2026-06-12): Gerstner waves ride
    # well above Z=0, so the dry-foot floor is H>=250 (was 90 - wet). The
    # band reaches 1100-1500 (the apron under 250 is gone) and the slope gate
    # is palm-real 16-26 deg: the 250+ zone IS the rising coast shoulder, the
    # old <12 deg gate left 14 palms on the whole island
    palm_f = ((1.0 - smoothstep(1100.0, 1500.0, H)) * smoothstep(250.0, 330.0, H)
              * smoothstep(26.0, 16.0, slope) * palm_grove * keep_tree)
    # flower meadows keep the old clover-noise SHAPE (the painted layer is
    # gone), gated to actual grass so they never sit on sand pads or road
    flower_f = (meadow_noise * smoothstep(20.0, 12.0, slope)
                * (masks["Grass"] / 255.0) * keep_small)
    shoulder_band = smoothstep(1500.0, 1100.0, road_d) * smoothstep(650.0, 800.0, road_d)
    flower_f = np.maximum(flower_f, shoulder_band * smoothstep(18.0, 10.0, slope)
                          * (masks["Grass"] / 255.0) * keep_small * (H > 80.0))
    wrack_noise = smoothstep(0.42, 0.56, mh.fbm(N, 4, 8, args.seed + 521))
    beach_f = ((1.0 - smoothstep(100.0, 180.0, H)) * smoothstep(15.0, 45.0, H)
               * smoothstep(16.0, 9.0, slope) * wrack_noise * keep_small)

    # ---------------- sample the plan ----------------
    plan = {}     # ft_path -> list[[x, y, z_trace]]

    def put(ft, x, y):
        plan.setdefault(ft, []).append(
            [round(float(x), 1), round(float(y), 1), round(at(H, x, y) + 2500.0, 1)])

    def pick(paths, weights=None):
        return paths[rng.choice(len(paths), p=weights)]

    jungle_pts = sample_field(jungle_f, 1100, rng, spacing=680.0)
    lone_pts = sample_field(lone_f, 170, rng, spacing=1300.0)
    jungle_pts = (np.concatenate([jungle_pts, lone_pts])
                  if len(lone_pts) else jungle_pts)
    for x, y in jungle_pts:
        h = at(H, x, y)
        banana_w = 0.30 * (1.0 - smoothstep(2500.0, 5000.0, np.float32(h)))
        wts = np.array([0.42, 0.58 - banana_w, banana_w], dtype=np.float64)
        wts /= wts.sum()
        grp = rng.choice(3, p=wts)
        ft = pick((FT_FOREST, FT_POHUT, FT_BANANA)[grp])
        put(ft, x, y)

    palm_pts = sample_field(palm_f, 170, rng, spacing=560.0)
    for x, y in palm_pts:
        put(pick(FT_PALM), x, y)

    # understory clustered around canopies (the "families" rule)
    canopy = np.concatenate([jungle_pts, palm_pts]) if len(palm_pts) else jungle_pts
    n_sat = 0
    for x, y in canopy:
        for _ in range(int(rng.poisson(2.3))):
            ang = rng.uniform(0.0, 2.0 * math.pi)
            rad = rng.uniform(260.0, 950.0)
            sx2, sy2 = x + math.cos(ang) * rad, y + math.sin(ang) * rad
            if at(keep_small, sx2, sy2) < 0.5 or at(slope, sx2, sy2) > 30.0:
                continue
            if at(H, sx2, sy2) < 80.0:
                continue
            roll = rng.random()
            if at(H, sx2, sy2) < 700.0 and roll < 0.45:
                ft = pick(FT_BUSH_P)
            elif roll < 0.45:
                ft = pick(FT_BUSH_T)
            elif roll < 0.78:
                ft = pick(FT_FERN)
            else:
                ft = pick(FT_TALLGRASS)
            put(ft, sx2, sy2)
            n_sat += 1
    # lone stragglers so clearings are not sterile
    for x, y in sample_field(grass_w * keep_small * smoothstep(26.0, 14.0, slope)
                             * (H > 120.0) * 0.10, 350, rng, spacing=900.0):
        put(pick(FT_BUSH_T + FT_FERN), x, y)

    flower_pts = sample_field(flower_f, 420, rng, spacing=560.0)
    for x, y in flower_pts:
        put(pick(FT_FLOWERS), x, y)

    beach_pts = sample_field(beach_f, 130, rng, spacing=1700.0)
    for x, y in beach_pts:
        put(pick(FT_DRIFT + FT_SEAWEED, None), x, y)

    # maldive palms ON the lens (resync 2026-06-12): scattered between the
    # construction pad edge and the mid-slope, never under water (palms by
    # the old radii would land on the dune shoulder or in the sea - Handoff);
    # driftwood sits lower on the shoulder
    for sid in ("M1", "M2", "M3", "M4"):
        s = slots[sid]
        t, run = lens[sid]
        palm_field = ((t > 200.0) & (t < run * 0.7) & (H > 250.0)
                      & (bridge_d > 1000.0)) * smoothstep(26.0, 16.0, slope)
        n_palm = int(rng.integers(3, 6) if s.get("tier") == "M"
                     else rng.integers(2, 5))
        for x, y in sample_field(palm_field, n_palm, rng, spacing=620.0):
            put(pick(FT_PALM if rng.random() < 0.8 else FT_BANANA), x, y)
        drift_field = ((t > run * 0.35) & (t < run) & (H > 25.0) & (H < 140.0)
                       & (bridge_d > 900.0)).astype(np.float32)
        for x, y in sample_field(drift_field, int(rng.integers(1, 4)), rng,
                                 spacing=1100.0):
            put(pick(FT_DRIFT), x, y)
    # start reef: two driftwood pieces at the rim, nothing else
    for _ in range(2):
        ang = rng.uniform(0.0, 2.0 * math.pi)
        px2, py2 = sx_ + math.cos(ang) * 1900.0, sy_ + math.sin(ang) * 1900.0
        if at(H, px2, py2) > 60.0:
            put(pick(FT_DRIFT), px2, py2)

    # rule check (author 2026-06-12): no palm may touch water - report the
    # lowest palm foot over the heightmap
    pmin, pn = 1e9, 0
    for ftp in FT_PALM:
        for q in plan.get(ftp, ()):
            pmin = min(pmin, at(H, q[0], q[1]))
            pn += 1
    print("palms: %d total, lowest foot H=%.0f uu %s" % (
        pn, pmin, "OK (>=250)" if pmin >= 250.0 else "**TOO WET**"))

    # live-terrain probes: apply_biome1_art_pass.py refuses to paint when the
    # open landscape does not match THIS heightmap (stale import guard)
    height_probes = [{"label": sid, "x": slots[sid]["pos"][0],
                      "y": slots[sid]["pos"][1], "want": slots[sid]["pos"][2]}
                     for sid in ("M1", "M2", "M3", "M4")]
    height_probes.append({"label": "Citadel", "x": slots["Citadel"]["pos"][0],
                          "y": slots["Citadel"]["pos"][1],
                          "want": slots["Citadel"]["pos"][2]})
    height_probes.append({"label": "G2 apron", "x": slots["G2"]["pos"][0],
                          "y": slots["G2"]["pos"][1],
                          "want": slots["G2"]["pos"][2] - 520.0})
    height_probes.append({"label": "open sea", "x": 70000.0, "y": -70000.0,
                          "want": SEA_FLOOR})
    bad = 0
    for p in height_probes:
        got = at(H, p["x"], p["y"])
        tol = max(90.0, abs(p["want"]) * 0.05)
        if abs(got - p["want"]) > tol:
            bad += 1
            print("PROBE %-9s H=%7.0f want %7.0f **MISMATCH**"
                  % (p["label"], got, p["want"]))
    print("height probes vs PNG: {}/{} OK".format(len(height_probes) - bad,
                                                  len(height_probes)))

    total = sum(len(v) for v in plan.values())
    plan_doc = {
        "seed": args.seed,
        "terrain_seed": args.terrain_seed,
        "heightmap": os.path.basename(HEIGHTMAP),
        "comment": "Biome1 island art pass v1 - generated by make_biome1_weightmaps.py; "
                   "apply with apply_biome1_art_pass.py (idempotent per foliage type)",
        "height_probes": height_probes,
        "type_params": TYPE_PARAMS,
        "instances": {ft: pts for ft, pts in sorted(plan.items())},
    }
    plan_path = os.path.join(OUT_DIR, "biome1_foliage_plan.json")
    with open(plan_path, "w", encoding="utf-8") as f:
        json.dump(plan_doc, f, indent=1)
    print("foliage plan: {} instances over {} types -> {}".format(
        total, len(plan), os.path.basename(plan_path)))
    print("  jungle %d, palms %d, understory %d, flowers %d, beach %d" % (
        len(jungle_pts), len(palm_pts), n_sat, len(flower_pts), len(beach_pts)))

    # ---------------- preview (UE TOP VIEW: +X up, +Y right) ----------------
    EXAG = 3.0
    gy2, gx2 = np.gradient(H * EXAG, SCALE)
    nz = 1.0 / np.sqrt(gx2 ** 2 + gy2 ** 2 + 1.0)
    light = np.clip((gx2 * 0.5 - gy2 * 0.35 + 1.0) * nz, 0.15, 1.4) / 1.4
    tint = (np.stack([b_sand] * 3, axis=-1) / 255.0 * np.array([0.86, 0.78, 0.58])
            + np.stack([b_rock] * 3, axis=-1) / 255.0 * np.array([0.52, 0.50, 0.48])
            + np.stack([b_grass] * 3, axis=-1) / 255.0 * np.array([0.36, 0.55, 0.27])
            + np.stack([b_clov] * 3, axis=-1) / 255.0 * np.array([0.55, 0.65, 0.28]))
    rgb = tint * light[..., None]
    sea = H < 0
    depth_c = np.clip(-H / 2200.0, 0, 1)
    rgb[sea] = (np.stack([0.10 + 0.0 * depth_c, 0.36 - 0.20 * depth_c,
                          0.56 - 0.28 * depth_c], axis=-1))[sea]
    shallow = (H >= -160) & (H < 0)
    rgb[shallow] = rgb[shallow] * 0.45 + np.array([0.55, 0.75, 0.72]) * 0.55

    DOT = {tuple(FT_FOREST + FT_POHUT + FT_BANANA): ((10, 64, 22), 3),
           tuple(FT_PALM): ((242, 142, 36), 3),
           tuple(FT_BUSH_T + FT_BUSH_P + FT_FERN + FT_TALLGRASS): ((52, 120, 46), 1),
           tuple(FT_FLOWERS): ((232, 70, 160), 1),
           tuple(FT_DRIFT): ((132, 84, 44), 2),
           tuple(FT_SEAWEED): ((36, 110, 108), 2)}
    img = (np.clip(rgb, 0, 1) * 255).astype(np.uint8)
    for ft, pts in plan.items():
        col, rad = next(v for k, v in DOT.items() if ft in k)
        for x, y, _ in pts:
            r = int(round((y + HALF) / SCALE))
            c = int(round((x + HALF) / SCALE))
            img[max(0, r - rad):r + rad + 1, max(0, c - rad):c + rad + 1] = col
    img = np.rot90(img, k=1)
    pim = Image.fromarray(img)
    try:
        from PIL import ImageDraw
        dr = ImageDraw.Draw(pim)
        dr.text((14, 10), "UE TOP VIEW: +X up, +Y right | dark green=trees "
                          "orange=palms small green=bush/fern pink=flowers "
                          "brown=driftwood", fill=(255, 255, 255))
    except Exception:
        pass
    prev_path = os.path.join(OUT_DIR, "biome1_artpass_preview.png")
    pim.save(prev_path)
    print("preview:", prev_path)


if __name__ == "__main__":
    main()
