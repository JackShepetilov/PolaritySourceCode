# Offline diagnostics for the Biome1 island heightmap (no editor needed).
#
# Checks, per arena slot, the terrain against the ACTUAL arena geometry from
# Arenas/<name>.json (floor tops, perimeter walls, antenna plates, rotated by
# the slot yaw): max protrusion above walkable tops = "terrain buried the
# arena". Also maps angularity (curvature hotspots) OUTSIDE the pads - the
# ugly pleats/creases the author flagged - and route-corridor slope comfort.
#
# Usage: python analyze_island_terrain.py [heightmap.png]

import json
import math
import os
import sys

import numpy as np
from PIL import Image

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
LAYOUT = os.path.join(TOOLS_DIR, "Island", "biome1_island_layout.json")
ARENAS_DIR = os.path.join(TOOLS_DIR, "Arenas")

N = 2017
SCALE = 100.0
HALF = (N - 1) * SCALE * 0.5
SEA_FLOOR = -2000.0
ARENA_LIFT = 10.0


def load_height(path):
    px = np.asarray(Image.open(path), dtype=np.float64)
    return (px - 32768.0) / 1.28 + SEA_FLOOR


def world_to_idx_f(x, y):
    return (y + HALF) / SCALE, (x + HALF) / SCALE


def sample_h(H, x, y):
    r, c = world_to_idx_f(x, y)
    r0, c0 = int(np.floor(r)), int(np.floor(c))
    r0 = max(0, min(N - 2, r0))
    c0 = max(0, min(N - 2, c0))
    fr, fc = r - r0, c - c0
    return (H[r0, c0] * (1 - fr) * (1 - fc) + H[r0, c0 + 1] * (1 - fr) * fc +
            H[r0 + 1, c0] * fr * (1 - fc) + H[r0 + 1, c0 + 1] * fr * fc)


def arena_floor_model(spec):
    """Walkable tops the terrain must NEVER poke through: floor pieces +
    deco plates (antenna/tower) + perimeter walls (their TOP edge is the
    arena silhouette; terrain above it means the arena is buried)."""
    boxes = []
    for p in spec.get("pieces", []):
        mat = p.get("mat", "")
        shape = p.get("shape", "box")
        if mat not in ("floor", "deco", "wall"):
            continue
        if shape == "ramp":
            # ramps: treat the LOWER half as the box (terrain must stay
            # under the low tip; the high tip is over other floors anyway)
            fx, fy, fz = p["from"]
            tx, ty, tz = p["to"]
            w = p.get("width", 400.0) * 0.5
            lo = (fx, fy, fz) if fz <= tz else (tx, ty, tz)
            boxes.append({"id": p["id"], "mat": mat, "shape": shape,
                          "x0": lo[0] - w, "x1": lo[0] + w,
                          "y0": lo[1] - w, "y1": lo[1] + w, "top": lo[2]})
            continue
        cx, cy, cz = p["pos"]
        sx, sy, sz = [v * 0.5 for v in p["size"]]
        top = cz + sz
        boxes.append({"id": p["id"], "mat": mat, "shape": shape,
                      "x0": cx - sx, "x1": cx + sx,
                      "y0": cy - sy, "y1": cy + sy, "top": top})
    return boxes


def check_arena_fit(H, slot, spec, step=100.0):
    """Sample the terrain across each walkable box (in WORLD space, slot yaw
    applied); report worst protrusion above the box top per piece."""
    yaw = math.radians(float(slot.get("yaw", 0.0)))
    ca, sa = math.cos(yaw), math.sin(yaw)
    bx, by, bz = slot["pos"]
    bz += ARENA_LIFT
    rows = []
    for b in arena_floor_model(spec):
        worst = -1e9
        wx_at = wy_at = None
        nx = max(2, int((b["x1"] - b["x0"]) / step) + 1)
        ny = max(2, int((b["y1"] - b["y0"]) / step) + 1)
        for i in range(nx):
            lx = b["x0"] + (b["x1"] - b["x0"]) * i / (nx - 1.0)
            for j in range(ny):
                ly = b["y0"] + (b["y1"] - b["y0"]) * j / (ny - 1.0)
                wx = bx + lx * ca - ly * sa
                wy = by + lx * sa + ly * ca
                h = sample_h(H, wx, wy)
                prot = h - (bz + b["top"])
                if prot > worst:
                    worst, wx_at, wy_at = prot, wx, wy
        rows.append((b["id"], b["mat"], worst, wx_at, wy_at))
    return rows


def slope_deg(H):
    gy, gx = np.gradient(H, SCALE)
    return np.degrees(np.arctan(np.hypot(gx, gy)))


def curvature(H):
    """Mean absolute discrete Laplacian (uu per cell) - crease detector."""
    L = (np.roll(H, 1, 0) + np.roll(H, -1, 0) + np.roll(H, 1, 1) +
         np.roll(H, -1, 1) - 4.0 * H)
    return np.abs(L)


def main():
    hm = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        TOOLS_DIR, "Island", "biome1_heightmap_2017.png")
    H = load_height(hm)
    with open(LAYOUT, encoding="utf-8") as f:
        layout = json.load(f)
    slots = {s["id"]: s for s in layout["slots"]}

    print("=== ARENA FIT (terrain vs walkable tops; >0 = BURIED) ===")
    for s in layout["slots"]:
        if s["kind"] != "arena":
            continue
        spec_path = os.path.join(ARENAS_DIR, s["default"] + ".json")
        with open(spec_path, encoding="utf-8") as f:
            spec = json.load(f)
        rows = check_arena_fit(H, s, spec)
        bad = [r for r in rows if r[2] > 0.0]
        print("\n[{}] {} at ({:.0f},{:.0f},{:.0f}) yaw {}".format(
            s["id"], s["default"], *s["pos"], s.get("yaw", "?")))
        if not bad:
            wmax = max(r[2] for r in rows)
            print("  CLEAN (worst margin {:.0f} uu below tops)".format(-wmax))
        for rid, mat, prot, wx, wy in sorted(bad, key=lambda r: -r[2]):
            print("  BURIED {:<12s} ({}): terrain +{:.0f} uu above top at "
                  "world ({:.0f},{:.0f})".format(rid, mat, prot, wx, wy))

    # --- angularity hotspots outside pads ---
    print("\n=== ANGULARITY (creases outside arena pads) ===")
    curv = curvature(H)
    sl = slope_deg(H)
    Xu, Yu = np.meshgrid(np.linspace(-HALF, HALF, N),
                         np.linspace(-HALF, HALF, N))
    # mask out water, pads (+blend ring) and the citadel cliff sector
    land = H > 80.0
    pad_mask = np.zeros((N, N), dtype=bool)
    for s in layout["slots"]:
        if s["kind"] != "arena":
            continue
        d = np.hypot(Xu - s["pos"][0], Yu - s["pos"][1])
        pad_mask |= d < (s["r"] + 900.0 + 500.0)
    isl = layout["island"]
    CPX, CPY = isl["peak"]
    ICX, ICY = isl["center"]
    deg_b = np.degrees(np.arctan2(Yu - ICY, Xu - ICX)) % 360.0
    cape_b = math.degrees(math.atan2(CPY - ICY, CPX - ICX)) % 360.0
    dd = np.abs(((deg_b - cape_b + 180.0) % 360.0) - 180.0)
    cliff = dd < 26.0
    zone = land & ~pad_mask & ~cliff
    cz = np.where(zone, curv, 0.0)
    print("creases (Laplacian uu): p95 {:.0f}, p99 {:.0f}, max {:.0f}".format(
        np.percentile(cz[zone], 95), np.percentile(cz[zone], 99), cz.max()))
    print("slope outside pads: p95 {:.1f} deg, area >35 deg: {:.1f}%".format(
        np.percentile(sl[zone], 95),
        100.0 * float((sl[zone] > 35.0).sum()) / float(zone.sum())))
    flat = cz.flatten()
    order = np.argsort(flat)[::-1]
    print("top crease spots (world coords):")
    seen = []
    k = 0
    for idx in order:
        if k >= 12:
            break
        r, c = divmod(int(idx), N)
        x, y = c * SCALE - HALF, r * SCALE - HALF
        if any(math.hypot(x - px, y - py) < 1500.0 for px, py in seen):
            continue
        seen.append((x, y))
        print("  ({:7.0f},{:7.0f})  crease {:5.0f}  h {:6.0f}  slope {:4.1f} deg"
              .format(x, y, flat[idx], H[r, c], sl[r, c]))
        k += 1

    # --- route comfort (the PLAYER path: corridor to the A5 entry, then the
    # exit bench behind bort_s to the citadel-stairs landing; the straight
    # line THROUGH the arena is not walked - keep in sync with the generator)
    print("\n=== ROUTE (player path legs) ===")
    g3 = slots["G3"]
    cit = slots["Citadel"]
    yaw3 = math.radians(float(g3.get("yaw", 0.0)))
    c3, s3 = math.cos(yaw3), math.sin(yaw3)

    def g3_world(lxv, lyv):
        return (g3["pos"][0] + lxv * c3 - lyv * s3,
                g3["pos"][1] + lxv * s3 + lyv * c3)

    cyw = math.radians(float(cit.get("yaw", 0.0)))
    cc, cs = math.cos(cyw), math.sin(cyw)
    ll = layout["rules"]["citadel_stairs"]["landing_local"]
    landing = (cit["pos"][0] + ll[0] * cc - ll[1] * cs,
               cit["pos"][1] + ll[0] * cs + ll[1] * cc)
    corr = layout["rules"]["route_corridor"]
    legs = [list(corr[:3]) + [g3_world(0.0, -4600.0)],
            [g3_world(1103.0, 5300.0), landing]]
    total = 0
    worst = 0.0
    for pts in legs:
        for a, b in zip(pts, pts[1:]):
            L = math.hypot(b[0] - a[0], b[1] - a[1])
            kk = max(2, int(L / 150.0))
            hs = []
            for i in range(kk):
                t = i / (kk - 1.0)
                hs.append(sample_h(H, a[0] + (b[0] - a[0]) * t,
                                   a[1] + (b[1] - a[1]) * t))
            hs = np.array(hs)
            seg = L / (kk - 1.0)
            g = np.degrees(np.arctan(np.abs(np.diff(hs)) / seg))
            worst = max(worst, g.max())
            total += int((g > 30.0).sum())
            print("  leg ({:6.0f},{:6.0f})->({:6.0f},{:6.0f}): max {:.1f} deg".format(
                a[0], a[1], b[0], b[1], g.max()))
    print("route: worst {:.1f} deg, samples >30 deg: {}".format(worst, total))


if __name__ == "__main__":
    main()
