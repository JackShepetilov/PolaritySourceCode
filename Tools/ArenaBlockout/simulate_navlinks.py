#!/usr/bin/env python
"""Offline validation of place_navlinks.py's placement logic against the dumped arena
geometry (Build/all_arena_geo.json). Mirrors floor_under() + the perimeter edge loop so
we can predict per-arena navlink counts and catch over/under-filtering WITHOUT the editor.
Approx: uses dump 'loc' (pivot) as AABB centre (blockout meshes are centred boxes)."""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
rows = json.load(open(os.path.join(HERE, "Build", "all_arena_geo.json"), encoding="utf-8"))

# --- same constants as place_navlinks.py ---
STEP = 1700.0
OUT_MIN, OUT_MAX = 350.0, 950.0
MIN_HALF = 200.0
MIN_TOP = 150.0
MARGIN = 40.0
BLOCK_MARGIN = 60.0
PROBE = 300.0
MIN_DROP = 80.0


def clamp(v, lo, hi):
    return max(lo, min(hi, v))


def simulate(arena, meshes):
    all_m = []          # (label, minx,maxx,miny,maxy, top)
    ground_top = None
    ground_bb = None
    platforms = []      # (label, cx,cy,hx,hy, top)
    for m in meshes:
        cx, cy = m["loc"][0], m["loc"][1]
        hx, hy = m["ext"][0], m["ext"][1]
        top = m["top"]
        lbl = m["label"]
        bb = (cx - hx, cx + hx, cy - hy, cy + hy)
        all_m.append((lbl, bb[0], bb[1], bb[2], bb[3], top))
        mat = m["mat"].lower()
        is_surface = ("floor" in mat or "deco" in mat or "wall" in mat)
        wide = min(hx, hy) >= MIN_HALF
        if top <= MIN_TOP:
            if is_surface and wide:
                if ground_top is None or top < ground_top:
                    ground_top = top
                if ground_bb is None:
                    ground_bb = list(bb)
                else:
                    ground_bb[0] = min(ground_bb[0], bb[0]); ground_bb[1] = max(ground_bb[1], bb[1])
                    ground_bb[2] = min(ground_bb[2], bb[2]); ground_bb[3] = max(ground_bb[3], bb[3])
            continue
        if is_surface and wide:
            platforms.append((lbl, cx, cy, hx, hy, top))
    if ground_top is None:
        ground_top = 0.0

    def floor_under(px, py, self_label, plat_top):
        best = None
        for (lbl, mnx, mxx, mny, mxy, mtop) in all_m:
            if lbl == self_label:
                continue
            if px < mnx - MARGIN or px > mxx + MARGIN or py < mny - MARGIN or py > mxy + MARGIN:
                continue
            if mtop >= plat_top - BLOCK_MARGIN:
                return (None, True)
            if best is None or mtop > best:
                best = mtop
        if ground_bb and ground_bb[0] - MARGIN <= px <= ground_bb[1] + MARGIN \
                and ground_bb[2] - MARGIN <= py <= ground_bb[3] + MARGIN:
            if best is None or ground_top > best:
                best = ground_top
        return (best, False)

    total = 0
    per_plat = []
    for (lbl, cx, cy, hx, hy, top) in platforms:
        kept = 0
        edges = 0
        targets = {}

        def place(ox, oy, dx, dy):
            nonlocal kept, edges, total
            edges += 1
            fz, blocked = floor_under(ox + dx * PROBE, oy + dy * PROBE, lbl, top)
            if blocked or fz is None:
                return
            drop = top - fz
            if drop < MIN_DROP:
                return
            kept += 1
            total += 1
            targets[int(fz)] = targets.get(int(fz), 0) + 1

        y = cy - hy + STEP * 0.5
        while y < cy + hy:
            place(cx - hx, y, -1.0, 0.0)
            place(cx + hx, y, 1.0, 0.0)
            y += STEP
        x = cx - hx + STEP * 0.5
        while x < cx + hx:
            place(x, cy - hy, 0.0, -1.0)
            place(x, cy + hy, 0.0, 1.0)
            x += STEP
        sid = lbl.replace("BLK_{}_".format(arena), "")
        per_plat.append((sid, top, kept, edges, targets))
    return platforms, ground_top, total, per_plat


from collections import defaultdict
byA = defaultdict(list)
for r in rows:
    byA[r["arena"]].append(r)

grand = 0
for arena in sorted(byA):
    plats, gtop, total, per = simulate(arena, byA[arena])
    grand += total
    print("\n=== {}  ground={:.0f}  platforms={}  -> {} navlinks ===".format(
        arena, gtop, len(plats), total))
    for (sid, top, kept, edges, targets) in per:
        tg = ", ".join("z{}x{}".format(z, c) for z, c in sorted(targets.items()))
        print("   {:14s} top{:5.0f}  {:2d}/{:2d} links  drop->[{}]".format(
            sid, top, kept, edges, tg))
print("\nGRAND TOTAL predicted navlinks across 9 arenas: {}".format(grand))
