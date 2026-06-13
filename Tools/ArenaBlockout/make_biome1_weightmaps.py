# Biome1 ART PASS source: bakes the 4 landscape weight masks
# (Sand_01 / Rockwall / Grass / Grass_Clovers - the exact layer set the
# author painted on Lvl_DemoForPublisher2) + a deterministic foliage plan,
# all OFFLINE from the already-authored heightmap PNG. PREVIEW WORKFLOW:
# review biome1_artpass_preview.png before apply_biome1_art_pass.py touches
# the editor.
#
# ISLAND PIVOT (layout v9, 2026-06-12): no big island - the map is maldive
# islets M1-M4 + ONE final island (A6_Villa). Roads/aprons/bowl/citadel/
# stairs zones are gone with it.
#
# Art rules (agreed with the author 2026-06-12):
#   - sand: waterline band (7-10 m horizontal ribbon) + the whole seabed +
#     maldive dune slopes above the beach; arena footprints = sand strictly
#     under the plates
#   - the FINAL island is LUSH: grass above its beach ribbon (it is the
#     destination, not another sand dune), green clearing around the villa
#   - rock: PURELY slope-driven (29->36 deg) - the gentle lens shoulders
#     stay rock-free by construction
#   - clovers: dead (author round 1) - zero mask keeps erasing the layer
#   - vegetation niches: coastal palms, jungle/groves + understory families
#     on the final island; maldives get their dedicated per-islet palm ring;
#     flowers on meadow noise, driftwood/seaweed along the sand
#   - hard keep-outs: arena footprints, bridge corridors, start reef
#     (author's launch point!), water
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

# ---- rock scatter (author round 4: "скалистые места заполнены скалами") ----
# Synty rock meshes get OWN FoliageType assets (FT_<mesh>) created by the
# apply script in the island folder; cheapest possible runtime = ISM foliage.
ROCK_DIR = ENV + "Rocks/"
FT_ISLAND_DIR = "/Game/Variant_Shooter/Arenas/Biome1/Island"
ROCK_GROUPS = {
    # group: (meshes, min_scale, max_scale, align_max_deg, cull, spacing, count)
    "boulder": (["SM_Env_Rock_01", "SM_Env_Rock_02", "SM_Env_Rock_03",
                 "SM_Env_Rock_04", "SM_Env_Rock_05", "SM_Env_Rock_06",
                 "SM_Env_Rock_Round_01"],
                0.8, 1.8, 60.0, 200000.0, 1500.0, 280),
    "debris": (["SM_Env_Rock_Pile_01", "SM_Env_Rock_Pile_02", "SM_Env_Rock_Pile_03",
                "SM_Env_Rock_Pile_04", "SM_Env_Rock_Pile_05", "SM_Env_Rock_Pile_06",
                "SM_Env_Rock_Pile_07", "SM_Env_Rock_Ground_01",
                "SM_Env_Rock_Ground_02", "SM_Env_Rock_Small_01"],
               0.6, 1.2, 45.0, 80000.0, 800.0, 380),
}
ROCK_SINK = {"boulder": (50.0, 140.0), "debris": (20.0, 70.0)}

# CLIFF WALL (author round 5: continuous coverage, oriented INTO the face -
# scattered upright boulders looked ridiculous). Real coastal cliffs read as
# horizontal COURSES of rock with a crown lip and talus feet; Synty style
# builds that from cliff meshes laid in masonry rows along the face. Placed
# via an ISM container (foliage cannot control per-instance rotation).
# mesh: (width@1, weight) - widths probed in-engine 2026-06-12
CLIFF_MESHES = {"SM_Env_Rock_Cliff_01": (500.0, 0.2),
                "SM_Env_Rock_Cliff_02": (1185.0, 0.4),
                "SM_Env_Rock_Cliff_03": (2227.0, 0.4)}
CLIFF_TAG = "ARTPASS_CliffWall"
COURSE_H = 650.0          # vertical course step (mesh height ~800-1000 scaled)
COURSE_SPACING = 1150.0   # along-face spacing inside a course (overlap)
CROWN_SPACING = 950.0


def rock_ft_path(mesh_name):
    return "{}/FT_{}".format(FT_ISLAND_DIR, mesh_name)

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

    px = np.asarray(Image.open(HEIGHTMAP), dtype=np.float32)
    assert px.shape == (N, N), "heightmap is {}".format(px.shape)
    H = SEA_FLOOR + (px - 32768.0) / 1.28

    coord = np.linspace(-HALF, HALF, N, dtype=np.float32)
    Xu, Yu = np.meshgrid(coord, coord)

    gy, gx = np.gradient(H, SCALE)
    slope = np.degrees(np.arctan(np.hypot(gx, gy))).astype(np.float32)

    # ---------------- layer masks ----------------
    # rock threshold 29->36: >=30 deg is unwalkable by project rules, and the
    # author wants "пляж вместо обрыва" - the 28-30 deg coastal aprons stay
    # green (first run painted a black ring around every shore). With the
    # gentle lens shoulders (v9) this catches only dune-noise pockets.
    w_rock = smoothstep(29.0, 36.0, slope)

    # ISLET SAND-LENS resync (terrain 2026-06-12, Handoff maldive section):
    # replicate the EXACT lens field t/run per islet - footprint rect dist
    # warped by the 5-harmonic coast wobble + spine term, per-slot lens
    # overrides honored (run_scale on the Final island!). Wobble phases come
    # from the same rng stream the heightmap consumed (TERRAIN seed: 2 legacy
    # island draws, then 5 per ARENA in slot order - matches the v9
    # generator), spine_n/Dw rebuilt with mh's own noise stack.
    lens_rng = np.random.default_rng(args.terrain_seed)
    lens_rng.uniform(0.0, 2.0 * np.pi, 2)            # legacy island draws
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

    maldive_zone = np.zeros_like(H)   # S/M islets: sandy dunes, ring planting
    final_zone = np.zeros_like(H)     # final island: lush, global planting
    lens = {}                                        # sid -> (t, run)
    for s in layout["slots"]:
        if s["kind"] != "arena":
            continue
        x, y, z = s["pos"]
        f_m = mh.load_footprint(s["default"])
        lxm, lym = mh.local_frame(Xu, Yu, s)
        fp_d = mh.rect_dist(lxm, lym, f_m["x0"] - 250.0, f_m["x1"] + 250.0,
                            f_m["y0"] - 250.0, f_m["y1"] + 250.0)
        th = np.arctan2(Yu - y, Xu - x)
        ph = lens_rng.uniform(0.0, 2.0 * np.pi, 5)
        lcfg = s.get("lens", {})
        tail_amp = float(lcfg.get("tail_amp", 0.30))
        rag = float(lcfg.get("ragged", 1.0))
        ph0 = (math.pi / 2.0 - math.radians(float(lcfg["tail_deg"]))
               if "tail_deg" in lcfg else ph[0])
        wob = (1.0 + tail_amp * np.sin(th + ph0)
               + rag * (0.18 * np.sin(2 * th + ph[1])
                        + 0.10 * np.sin(3 * th + ph[2])
                        + 0.06 * np.sin(5 * th + ph[3])
                        + 0.04 * np.sin(7 * th + ph[4])))
        wob = np.clip(wob + 0.30 * rag * (Dw - 0.5) * 2.0, 0.60, 1.40)
        gate = mh.smootherstep(0.0, 700.0, fp_d)
        t = np.maximum(fp_d / wob + (spine_n - 0.45) * 1300.0 * rag * gate, 0.0)
        run = (z * mh.MALDIVE_RUN_K + mh.MALDIVE_RUN_BASE) \
            * float(lcfg.get("run_scale", 1.0))
        lens[s["id"]] = (t.astype(np.float32), run)
        zone = smoothstep(run + 700.0, run + 100.0, t)
        if s.get("tier") == "final":
            final_zone = np.maximum(final_zone, zone)
        else:
            maldive_zone = np.maximum(maldive_zone, zone)
    sx_, sy_, sz_ = slots["start_reef"]["pos"]
    reef_zone = smoothstep(3100.0, 2500.0, np.hypot(Xu - sx_, Yu - sy_))
    # no rock ANYWHERE on the islet world ('умеренно полого, без скал'):
    # dune-noise pockets over 29 deg would read as random stone patches on
    # the final island's grass shoulder. The Rockwall mask stays imported as
    # an all-zero eraser (same treatment as the dead clover layer).
    w_rock = (w_rock * (1.0 - maldive_zone) * (1.0 - final_zone)
              * (1.0 - reef_zone))

    # BEACH BY DISTANCE (author round 3: "пляж везде шириной 7-10 м") - the
    # height-band beach vanished on steep coasts and ate whole flats. Sand
    # ribbon = horizontal (Chebyshev) distance to the waterline, 1 px=100 uu,
    # width jittered 7..10 px. The author ocean sits at Z=150.
    water = H < 150.0
    dist_px = np.where(water, 0.0, 99.0).astype(np.float32)
    dil = water.copy()
    for k in range(1, 12):
        nd = dil.copy()
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1),
                       (1, 1), (1, -1), (-1, 1), (-1, -1)):
            nd |= np.roll(np.roll(dil, dy, 0), dx, 1)
        ring = nd & (dist_px > 98.0)
        dist_px[ring] = float(k)
        dil = nd
    beach_w = 7.0 + 3.0 * mh.fbm(N, 4, 18, args.seed + 301)   # 7..10 px
    beach = (~water) & (dist_px <= beach_w)
    reef_bin = reef_zone > 0.5
    # S/M islets stay sandy dunes above the beach; the FINAL island is lush -
    # its land above the beach ribbon is grass (it is the destination)
    lens_dune = (maldive_zone > 0.5) & (final_zone <= 0.5) & ~water

    # ARENA FOOTPRINTS = SAND strictly UNDER the plates (author round 2: the
    # +300/800 halo read as a washed-out ring - grass must ADJOIN the arenas)
    pad_core = np.zeros_like(H, dtype=bool)
    for s in layout["slots"]:
        if s["kind"] != "arena":
            continue
        f = mh.load_footprint(s["default"])
        lx, ly = mh.local_frame(Xu, Yu, s)
        d = mh.rect_dist(lx, ly, f["x0"], f["x1"], f["y0"], f["y1"])
        pad_core |= (d <= 60.0)

    # islet GRASS APRON, round 3: MUCH bigger ("вокруг арен травы очень
    # мало") - a wide green clearing from the plate edge to a noisy boundary;
    # dunes survive between the apron and the beach ribbon on bigger islets
    apron_noise = mh.fbm(N, 4, 90, args.seed + 631)   # ~2k features: lively edge
    grass_apron = np.zeros_like(H, dtype=bool)
    for s in layout["slots"]:
        if s["kind"] != "arena":
            continue
        t, run = lens[s["id"]]
        f_m = mh.load_footprint(s["default"])
        lxm, lym = mh.local_frame(Xu, Yu, s)
        fpd = mh.rect_dist(lxm, lym, f_m["x0"], f_m["x1"], f_m["y0"], f_m["y1"])
        outer = 2000.0 + (apron_noise - 0.5) * 1400.0   # organic apron edge
        grass_apron |= ((fpd > 60.0) & (fpd < outer) & (H > 220.0)
                        & (t < run * 0.62))

    # BINARY layer resolve (author round 2: "бленда травы быть не должно,
    # всегда вес 1") - every pixel is exactly ONE layer; the texture
    # transition at boundaries is the material's own HeightBlend job.
    # Priorities: rock > plates > beach/water > apron > dunes > grass.
    rock_bin = w_rock > 0.5
    sand_bin = (water | beach | reef_bin | lens_dune) & ~rock_bin
    sand_bin |= pad_core                       # plates: sand even over rock
    carve = (grass_apron & ~pad_core & ~rock_bin & ~beach & ~water)
    sand_bin &= ~carve
    grass_bin = ~rock_bin & ~sand_bin

    # cells 28 (was 7): the noise was tuned for the 40k island - on a ~9k
    # final island a 7-cell field is one value over the whole land (flowers
    # came out 0 on the first v9 run)
    meadow_noise = smoothstep(0.52, 0.66, mh.fbm(N, 4, 28, args.seed + 113))

    # binary value is 229, NOT 255: the per-pixel deficit (26 = 10%) lands in
    # 'Base' on import (the importer dumps it into the FIRST landscape layer
    # = Base by our assignment order). With Base at exactly 0 the Synty
    # master renders BLACK again (regressed live 2026-06-12 on the first
    # binary import; the 0.898/0.102 main/Base ratio is the proven-good
    # render state). 10% Base admixture is visually negligible.
    BINV = 229.0
    # 1-texel boundary anti-aliasing (author round 4: "ландшафт с серьёзным
    # джиттером"): hard 0/255 texel edges render as a 1 m staircase. One 3x3
    # blur pass + renormalize keeps zone interiors at weight 1 (the binary
    # rule holds) and gives ONLY the boundary texels a sub-texel ramp that
    # the material's height-blend turns into a crisp organic line.
    f_rock = mh.gauss3(np.where(rock_bin & ~sand_bin, 1.0, 0.0).astype(np.float32),
                       passes=1)
    f_sand = mh.gauss3(np.where(sand_bin, 1.0, 0.0).astype(np.float32), passes=1)
    f_grass = mh.gauss3(np.where(grass_bin, 1.0, 0.0).astype(np.float32), passes=1)
    tot = np.maximum(f_rock + f_sand + f_grass, 1e-6)
    b_rock = np.round(BINV * f_rock / tot)
    b_sand = np.round(BINV * f_sand / tot)
    b_clov = np.zeros_like(H)                  # clovers stay dead (round 1)
    b_grass = np.clip(BINV - b_rock - b_sand, 0.0, None)
    masks = {"Sand_01": b_sand, "Rockwall": b_rock,
             "Grass": b_grass, "Grass_Clovers": b_clov}
    for name, arr in masks.items():
        Image.fromarray(arr.astype(np.uint8), mode="L").save(
            os.path.join(OUT_DIR, "biome1_weight_{}.png".format(name)))
        print("mask %-13s coverage %5.1f%% (mean weight)" %
              (name, float(arr.mean()) / 2.55))
    # 'Base' (LB_AlphaBlend) MUST be allocated on the landscape or the Synty
    # master renders the whole landscape BLACK (caught live 2026-06-12).
    # Constant low weight; the apply script imports it FIRST, the four real
    # masks then overwrite per-pixel weights (sum 255 -> Base ends ~0 but
    # stays allocated).
    Image.fromarray(np.full((N, N), 26, dtype=np.uint8), mode="L").save(
        os.path.join(OUT_DIR, "biome1_weight_Base.png"))

    # ---------------- keep-out fields for foliage ----------------
    # pad_d: distance to the NEAREST arena footprint (+500 collar), all slots
    pad_d = np.full_like(H, 1e9)
    for s in layout["slots"]:
        if s["kind"] != "arena":
            continue
        f = mh.load_footprint(s["default"])
        lx, ly = mh.local_frame(Xu, Yu, s)
        pad_d = np.minimum(pad_d, mh.rect_dist(
            lx, ly, f["x0"] - 500.0, f["x1"] + 500.0, f["y0"] - 500.0, f["y1"] + 500.0))

    # bridge corridors: islet hops where the line runs over water/beach
    # (consecutive ARENA entries of the route, incl. M4->Final)
    ids = [r for r in layout["route"]
           if r in slots and slots[r]["kind"] == "arena"]
    bridge_d = np.full_like(H, 1e9)
    for a_id, b_id in zip(ids, ids[1:]):
        bridge_d = np.minimum(bridge_d, seg_dist_field(
            [slots[a_id]["pos"][:2], slots[b_id]["pos"][:2]], 1400.0))
    bridge_keep = (bridge_d < 1000.0) & (H < 500.0)   # only near the water

    def keepout(tree):
        """1 = plantable. `tree` widens pad margins for canopies.
        S/M maldives + reef are FULLY excluded - their planting is the
        dedicated per-slot ring pass (global fields flooded the rims on
        first run). The FINAL island stays in: it gets the full vegetation."""
        ok = np.ones_like(H)
        ok *= (H > 80.0)
        ok *= (pad_d > (1100.0 if tree else 350.0))
        ok *= ~bridge_keep
        ok *= (maldive_zone < 0.5)
        ok *= (reef_zone < 0.5)
        return ok

    keep_tree = keepout(True)
    keep_small = keepout(False)

    # ---------------- vegetation fields ----------------
    # (global fields now effectively cover the FINAL island only - maldives
    # and the reef are keep-out and get their dedicated ring pass below)
    grove = smoothstep(0.46, 0.62, mh.fbm(N, 4, 20, args.seed + 211))
    grass_w = masks["Grass"] / 255.0 + masks["Grass_Clovers"] / 255.0

    jungle_f = (smoothstep(26.0, 16.0, slope) * grove
                * smoothstep(250.0, 500.0, H) * grass_w * keep_tree)
    # lone trees OUTSIDE the groves: thin savanna fill so the open lawn
    # is not sterile (the grove noise left it empty on first run)
    lone_f = ((1.0 - grove) * smoothstep(24.0, 14.0, slope)
              * smoothstep(250.0, 500.0, H) * grass_w * keep_tree)
    # palm GROVES, not a bead string: noise gates which coast stretches get
    # palms (cells 20 ~ 10k features: several grove/gap alternations around
    # the ~57k final-island perimeter)
    palm_grove = smoothstep(0.40, 0.54, mh.fbm(N, 4, 20, args.seed + 421))
    # palms NEVER touch water (author review 2026-06-12): the AUTHOR ocean on
    # this map sits at Z=150 and Gerstner crests reach ~410 (audited live via
    # verify_artpass_water.py) - dry-foot floor H>=500. The band reaches
    # 1100-1500 and the slope gate is palm-real 16-26 deg (the <12 deg gate
    # left 14 palms on the whole island)
    palm_f = ((1.0 - smoothstep(1100.0, 1500.0, H)) * smoothstep(500.0, 580.0, H)
              * smoothstep(26.0, 16.0, slope) * palm_grove * keep_tree)
    # flower meadows keep the old clover-noise SHAPE (the painted layer is
    # gone), gated to actual grass so they never sit on sand pads
    flower_f = (meadow_noise * smoothstep(20.0, 12.0, slope)
                * (masks["Grass"] / 255.0) * keep_small)
    wrack_noise = smoothstep(0.42, 0.56, mh.fbm(N, 4, 8, args.seed + 521))
    # driftwood/seaweed live ON the 7-10 m beach ribbon, above the mean
    # waterline (ocean Z=150) so the surf washes through them
    beach_f = ((beach & (H > 165.0)).astype(np.float32)
               * smoothstep(24.0, 14.0, slope) * wrack_noise * keep_small)

    # ---------------- sample the plan ----------------
    plan = {}     # ft_path -> list[[x, y, z_exact_ground]]

    def hsample(x, y):
        """Bilinear ground height - instances plant at EXACT Z with the
        surface trace OFF (author round 2: traces hit the canopy collision
        of instances not yet removed -> floating trees)."""
        r = np.clip((y + HALF) / SCALE, 0.0, N - 1.001)
        c = np.clip((x + HALF) / SCALE, 0.0, N - 1.001)
        return float(mh.sample(H, np.array([r]), np.array([c]))[0])

    def put(ft, x, y):
        plan.setdefault(ft, []).append(
            [round(float(x), 1), round(float(y), 1), round(hsample(x, y), 1)])

    def pick(paths, weights=None):
        return paths[rng.choice(len(paths), p=weights)]

    jungle_pts = sample_field(jungle_f, 450, rng, spacing=680.0)
    lone_pts = sample_field(lone_f, 60, rng, spacing=1300.0)
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

    palm_pts = sample_field(palm_f, 90, rng, spacing=560.0)
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
                             * (H > 120.0) * 0.10, 120, rng, spacing=900.0):
        put(pick(FT_BUSH_T + FT_FERN), x, y)

    flower_pts = sample_field(flower_f, 200, rng, spacing=560.0)
    for x, y in flower_pts:
        put(pick(FT_FLOWERS), x, y)

    beach_pts = sample_field(beach_f, 60, rng, spacing=1700.0)
    for x, y in beach_pts:
        put(pick(FT_DRIFT + FT_SEAWEED, None), x, y)

    # maldive palms ON the lens (resync 2026-06-12): scattered between the
    # construction pad edge and the mid-slope, never under water (palms by
    # the old radii would land on the dune shoulder or in the sea - Handoff);
    # driftwood sits lower on the shoulder. S/M islets only - the final
    # island is covered by the global fields above.
    for s in layout["slots"]:
        if s["kind"] != "arena" or s.get("tier") not in ("S", "M"):
            continue
        sid = s["id"]
        t, run = lens[sid]
        palm_field = ((t > 200.0) & (t < run * 0.7) & (H > 500.0)
                      & (bridge_d > 1000.0)) * smoothstep(26.0, 16.0, slope)
        n_palm = int(rng.integers(3, 6) if s.get("tier") == "M"
                     else rng.integers(2, 5))
        for x, y in sample_field(palm_field, n_palm, rng, spacing=620.0):
            put(pick(FT_PALM if rng.random() < 0.8 else FT_BANANA), x, y)
        drift_field = ((dist_px <= beach_w + 2.0) & ~water & (H > 165.0)
                       & (t < run + 400.0)
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

    # ---------------- rock scatter: DEAD (and the cliffs with it) ----------
    # AUTHOR ROUND 6: foliage rocks are OUT entirely ("убери уебанские камни"),
    # and the v9 pivot removed the cliffs the masonry wall was built for.
    # The FT defs stay registered in TYPE_PARAMS and the empty cliff_wall
    # instance list stays in the plan so the apply-side sweeps keep CLEARING
    # the old instances and DESTROY the ARTPASS_CliffWall container.
    wall = []
    rock_counts = {grp: 0 for grp in ROCK_GROUPS}
    print("rocks: wall %d, boulder %d, debris %d (all dead by author rule)" % (
        len(wall), rock_counts.get("boulder", 0), rock_counts.get("debris", 0)))

    # rule check (author 2026-06-12): no palm may touch water - report the
    # lowest palm foot over the heightmap
    pmin, pn = 1e9, 0
    for ftp in FT_PALM:
        for q in plan.get(ftp, ()):
            pmin = min(pmin, at(H, q[0], q[1]))
            pn += 1
    print("palms: %d total, lowest foot H=%.0f uu %s" % (
        pn, pmin, "OK (>=500, crest 410)" if pmin >= 500.0 else "**TOO WET**"))

    # live-terrain probes: apply_biome1_art_pass.py refuses to paint when the
    # open landscape does not match THIS heightmap (stale import guard)
    height_probes = [{"label": s["id"], "x": s["pos"][0],
                      "y": s["pos"][1], "want": s["pos"][2]}
                     for s in layout["slots"] if s["kind"] == "arena"]
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

    # rock foliage types: the apply script creates these FT assets (island
    # folder) if missing, instances below reference them by path
    rock_type_defs = []
    for grp, (meshes, mn, mx, amax, cull, spacing, count) in ROCK_GROUPS.items():
        for m in meshes:
            rock_type_defs.append({
                "asset_name": "FT_{}".format(m),
                "mesh": ROCK_DIR + m,
                "min_scale": mn, "max_scale": mx,
                "align_to_normal": True,
                "align_to_normal_max_angle": amax,
                "cull_distance_max": cull,
            })
            TYPE_PARAMS[rock_ft_path(m)] = dict(min_scale=mn, max_scale=mx,
                                                align_to_normal=True)

    total = sum(len(v) for v in plan.values())
    plan_doc = {
        "seed": args.seed,
        "terrain_seed": args.terrain_seed,
        "foliage_type_defs": rock_type_defs,
        "cliff_wall": {"tag": CLIFF_TAG, "mesh_dir": ROCK_DIR,
                       "instances": wall},
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

    rocks_all = tuple(rock_ft_path(m)
                      for grp in ROCK_GROUPS.values() for m in grp[0])
    DOT = {tuple(FT_FOREST + FT_POHUT + FT_BANANA): ((10, 64, 22), 3),
           tuple(FT_PALM): ((242, 142, 36), 3),
           tuple(FT_BUSH_T + FT_BUSH_P + FT_FERN + FT_TALLGRASS): ((52, 120, 46), 1),
           tuple(FT_FLOWERS): ((232, 70, 160), 1),
           tuple(FT_DRIFT): ((132, 84, 44), 2),
           tuple(FT_SEAWEED): ((36, 110, 108), 2),
           rocks_all: ((96, 96, 100), 2)}
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
