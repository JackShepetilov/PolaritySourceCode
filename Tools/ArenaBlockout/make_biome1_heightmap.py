# Authors the REAL Biome1 heightmap (2017x2017, ~2x2 km) OFFLINE from
# Island/biome1_island_layout.json (v9+). PREVIEW WORKFLOW: review the shaded
# preview (UE TOP VIEW orientation) before any import - nothing touches the editor.
#
# ISLAND PIVOT (author, 2026-06-12): the big island is GONE. The whole run is
# a chain of small maldive islets + ONE final island (~1/4 the old land
# radius) with the final arena dead-center. Every islet - maldive or final -
# is the SAME sand-lens stamp, the final one just has a bigger footprint and
# a stretched run (slot lens.run_scale). Dead with the big island: dome/cape/
# cliff+strata, thermal erosion, serpentine roads, G1/G2 pads, the G3 bowl,
# citadel stairs, coast cuts.
#
# Terrain recipe:
#   1. noise base: gentle sea-floor undulation + domain-warped fields that
#      drive islet coastline raggedness (spine/detail)
#   2. islet stamps for EVERY arena slot: SAND-LENS profile - the arena
#      footprint zone stays glass-flat (plates float 10 uu over it), from its
#      edge the islet falls as a CONVEX C2 shoulder straight under the
#      waterline (no flat ring, no rim), dune noise scales with the drop
#   3. start reef (wide flat top - the author's launch structure stands here)
#   4. C2 shore-gradient relax (NO constant-height beach shelf)
#   5. curvature-targeted relax of walkable land + despike passes
#
# Variations (roguelike groundwork): --seed N re-rolls the NATURE layers
# (warp, coast wobble, islet wobble) while the pillars (slot positions, flat
# footprints, route) stay pinned to the layout JSON.
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
    Cell-relative tunables (despike threshold, relax knees) are scaled at
    their call sites via SCALE/100."""
    global N, SCALE, HALF
    N = int(n)
    SCALE = EXTENT / (N - 1)


SEA_FLOOR = -2000.0
PAD_RIM = 900.0
# Islet SAND-LENS profile (2026-06-12): the island falls from the arena
# footprint edge as a convex C2 shoulder, run = z * K + BASE (wobbled).
# The final island stretches its run via slot lens.run_scale (gentler, bigger).
MALDIVE_RUN_K = 2.8
MALDIVE_RUN_BASE = 2000.0
UNDERWATER_SKIRT = 3600.0
ARENA_LIFT = 10.0          # build_biome_island.py floats arenas +10 uu


# ---------------------------------------------------------------- noise ----
def grid_noise(n, cells, seed, angle):
    """Value noise on a lattice ROTATED by `angle` with smoothstep fade -
    kills the axis-aligned banding the author flagged."""
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


# ------------------------------------------------------------------ main ----
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=None,
                    help="nature-layer seed (flat footprints and slot "
                         "positions are seed-independent); default = "
                         "layout's 'seed' field")
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

    coord = np.linspace(-HALF, HALF, N, dtype=np.float32)   # row 0 = world SOUTH
    Xu, Yu = np.meshgrid(coord, coord)
    rng = np.random.default_rng(seed)
    idx = np.arange(N, dtype=np.float32)

    # fields: detail fbm + two-channel domain warp + warped ridged spine
    # (the spine survives the pivot as the islet-coastline raggedness field)
    detail = fbm(N, 5, 8, seed + 37)
    wx = (fbm(N, 4, 4, seed + 11) - 0.5) * 160.0   # broad warp, up to ~16k uu
    wy = (fbm(N, 4, 4, seed + 23) - 0.5) * 160.0
    ys = idx[:, None] + wy
    xs = idx[None, :] + wx
    spine = sample(ridged(N, 6, 6, seed + 71), ys, xs)
    Dw = sample(detail, ys, xs)

    H = SEA_FLOOR + (fbm(N, 4, 14, seed + 51) - 0.5) * 220.0

    # rng-stream compat with v8: the big-island block used to draw two coast
    # phases here. Keep the draw so the per-islet wobble phases below stay
    # IDENTICAL - M1-M4 keep the exact coastlines the author has already seen.
    rng.uniform(0, 2 * np.pi, 2)

    spine_n = (spine - spine.min()) / (spine.max() - spine.min())

    # ---------------- islet stamps: SAND-LENS profile ------------------------
    # Every arena slot gets its own micro-island (maldive model, author
    # 2026-06-11/12): the arena footprint zone stays glass-flat (plates float
    # 10 uu over it), and from its edge the islet falls as a CONVEX C2
    # shoulder straight under the waterline - no flat ring, no rim, the
    # horizon just rolls away. Dune noise scales with the local drop, so the
    # crown is serene and nothing pokes the floors. The FINAL island is the
    # same stamp with a bigger footprint (A6_Villa) and lens.run_scale.
    for s_ in layout["slots"]:
        x, y, z = s_["pos"]
        if s_["kind"] == "arena":
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
            # upper clip 1.40: lobes any larger can land-bridge the straits
            # (open water between islands is a layout rule; the analyzer
            # water-gap gate enforces it for every seed)
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
    beach_w = smootherstep(420.0, 60.0, np.abs(H - 60.0))
    H = H - (H - 60.0) * 0.45 * beach_w
    H = gauss3(H, passes=2, w=0.5 * smootherstep(420.0, 300.0, np.abs(H - 60.0)))

    # ---------------- crease cleanup on walkable land -----------------------
    land_mask = smoothstep(60.0, 160.0, H)
    H = curvature_relax(H, passes=3, mask=land_mask, knee=45.0 * cell)

    # despike: lone texels vs the 8-neighbour median render as pyramids -
    # replace them (stamp-interaction residue). TWO+ passes: a LINE of spikes
    # hides its members from a single median pass.
    for _ in range(4):
        nb = [np.roll(np.roll(H, dy, 0), dx, 1)
              for dy in (-1, 0, 1) for dx in (-1, 0, 1) if (dy, dx) != (0, 0)]
        med8 = np.median(np.stack(nb), axis=0).astype(np.float32)
        del nb
        spike = np.abs(H - med8) > 45.0 * cell
        n_sp = int(spike.sum())
        if not n_sp:
            break
        H = np.where(spike, med8, H)
        print("despiked %d lone texel(s)" % n_sp)

    px16 = np.clip(32768.0 + (H - SEA_FLOOR) * 1.28, 0, 65535).astype(np.uint16)
    out_png = args.out or os.path.join(
        OUT_DIR, "biome1_heightmap_2017{}.png".format(suffix))
    if args.res != 2017 and args.out is None:
        out_png = os.path.join(OUT_DIR, "biome1_heightmap_fast.png")
    Image.fromarray(px16).save(out_png)

    # shaded preview, UE top view orientation: hypsometric tint + hillshade
    # with 3x vertical exaggeration + 5 m contour lines
    EXAG = 3.0
    gy_g, gx_g = np.gradient(H * EXAG, SCALE)
    nz = 1.0 / np.sqrt(gx_g ** 2 + gy_g ** 2 + 1.0)
    light = np.clip((gx_g * 0.5 - gy_g * 0.35 + 1.0) * nz, 0.15, 1.4) / 1.4
    rgb = np.zeros((N, N, 3))
    sea = H < 0
    depth_c = np.clip(-H / 2200.0, 0, 1)
    rgb[sea] = np.stack([0.08 + 0.0 * depth_c[sea], 0.34 - 0.20 * depth_c[sea],
                         0.55 - 0.28 * depth_c[sea]], axis=-1)
    h01 = np.clip(H / 3200.0, 0, 1)
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
    lvl = np.floor(H / 500.0)
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

    for s_ in layout["slots"]:
        if s_["kind"] == "arena":
            probe(s_["id"], s_["pos"][0], s_["pos"][1], s_["pos"][2])
    probe("start reef", sx, sy, sz)
    probe("open sea", 70000, -70000)
    print("max H %.0f" % H.max())
    print("written:", out_png)


if __name__ == "__main__":
    main()
