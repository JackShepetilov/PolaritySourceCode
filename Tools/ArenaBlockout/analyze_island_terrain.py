# Offline diagnostics for the Biome1 heightmap (no editor needed).
#
# Checks, per arena slot, the terrain against the ACTUAL arena geometry from
# Arenas/<name>.json (floor tops, perimeter walls, antenna plates, rotated by
# the slot yaw): max protrusion above walkable tops = "terrain buried the
# arena". Also maps angularity (curvature hotspots) OUTSIDE the pads - the
# ugly pleats/creases the author flagged - and open-water gaps between the
# islets of the chain (v9 pivot: islets + final island, no big island).
#
# Usage: python analyze_island_terrain.py [heightmap.png] [--layout PATH] [--json]
#   Resolution is derived from the PNG size (fast previews analyze too).
#   --json appends a machine-readable GATES_JSON line (layout editor server).

import argparse
import json
import math
import os

import numpy as np
from PIL import Image

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))

LAYOUT = os.path.join(TOOLS_DIR, "Island", "biome1_island_layout.json")
ARENAS_DIR = os.path.join(TOOLS_DIR, "Arenas")

EXTENT = 201600.0
N = 2017
SCALE = 100.0
HALF = EXTENT * 0.5
SEA_FLOOR = -2000.0
ARENA_LIFT = 10.0
GATES = {}


def load_height(path):
    global N, SCALE
    px = np.asarray(Image.open(path), dtype=np.float64)
    N = px.shape[0]
    SCALE = EXTENT / (N - 1)
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
    ap = argparse.ArgumentParser()
    ap.add_argument("heightmap", nargs="?", default=os.path.join(
        TOOLS_DIR, "Island", "biome1_heightmap_2017.png"))
    ap.add_argument("--layout", default=LAYOUT)
    ap.add_argument("--json", action="store_true",
                    help="append a GATES_JSON line for machine parsing")
    args = ap.parse_args()
    H = load_height(args.heightmap)
    with open(args.layout, encoding="utf-8") as f:
        layout = json.load(f)
    slots = {s["id"]: s for s in layout["slots"]}

    print("=== ARENA FIT (terrain vs walkable tops; >0 = BURIED) ===")
    GATES["arenas"] = {}
    for s in layout["slots"]:
        if s["kind"] != "arena":
            continue
        spec_path = os.path.join(ARENAS_DIR, s["default"] + ".json")
        with open(spec_path, encoding="utf-8") as f:
            spec = json.load(f)
        rows = check_arena_fit(H, s, spec)
        bad = [r for r in rows if r[2] > 0.0]
        GATES["arenas"][s["id"]] = round(max(r[2] for r in rows), 1)
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
    # mask out water and arena pads (+blend ring)
    land = H > 80.0
    pad_mask = np.zeros((N, N), dtype=bool)
    for s in layout["slots"]:
        if s["kind"] != "arena":
            continue
        d = np.hypot(Xu - s["pos"][0], Yu - s["pos"][1])
        pad_mask |= d < (s["r"] + 900.0 + 500.0)
    zone = land & ~pad_mask
    cz = np.where(zone, curv, 0.0)
    GATES["crease_p99"] = round(float(np.percentile(cz[zone], 99)), 1)
    print("creases (Laplacian uu): p95 {:.0f}, p99 {:.0f}, max {:.0f}".format(
        np.percentile(cz[zone], 95), np.percentile(cz[zone], 99), cz.max()))
    # spike detector: lone texels vs the median of their 8 neighbours = the
    # pyramid rows left by binary masks (in-engine catch 2026-06-12)
    cell = SCALE / 100.0
    nbrs = np.stack([np.roll(np.roll(H, dy, 0), dx, 1)
                     for dy in (-1, 0, 1) for dx in (-1, 0, 1)
                     if (dy, dx) != (0, 0)])
    med = np.median(nbrs, axis=0)
    spikes = (np.abs(H - med) > 60.0 * cell) & zone
    n_sp = int(spikes.sum())
    GATES["spikes"] = n_sp
    print("spikes (|H - nbr median| > 60): {}{}".format(
        n_sp, "" if n_sp == 0 else "  **CHECK**"))
    if n_sp:
        ys, xs2 = np.where(spikes)
        for k in range(min(6, n_sp)):
            r, c = ys[k], xs2[k]
            print("  spike at ({:.0f},{:.0f}) h {:.0f} vs median {:.0f}".format(
                c * SCALE - HALF, r * SCALE - HALF, H[r, c], med[r, c]))
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

    # --- open water between islands (layout rule: hops MUST stay water;
    # coastline lobes can silently land-bridge a strait on a bad seed).
    # Hops are derived from the route: consecutive ARENA entries (the
    # reef->M1 toss leg is exempt - M1 merges with the reef by design).
    print("\n=== WATER GAPS (islet hops) ===")
    ids = [r for r in layout["route"]
           if r in slots and slots[r]["kind"] == "arena"]
    hops = list(zip(ids, ids[1:]))
    GATES["water"] = {}
    for a_id, b_id in hops:
        a = slots[a_id]["pos"]
        b = slots[b_id]["pos"]
        L = math.hypot(b[0] - a[0], b[1] - a[1])
        kk = max(2, int(L / 100.0))
        best = cur = 0.0
        minh = 1e9
        for i in range(kk):
            t = i / (kk - 1.0)
            h = sample_h(H, a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
            minh = min(minh, h)
            cur = cur + L / kk if h < -80.0 else 0.0
            best = max(best, cur)
        tag = "OK" if best >= 1500.0 else "**LAND-BRIDGED**"
        GATES["water"]["{}-{}".format(a_id, b_id)] = round(best)
        print("  {}->{}: longest water {:.0f} uu (min H {:.0f}) {}".format(
            a_id, b_id, best, minh, tag))
    if args.json:
        print("GATES_JSON: " + json.dumps(GATES))


if __name__ == "__main__":
    main()
