# -*- coding: utf-8 -*-
"""AABB overlap validator for ArenaBlockout specs (axis-aligned pieces only).
Reports pairs whose boxes interpenetrate deeper than TOL on all three axes.
Intentional contacts (stacking, wall-on-slab) have zero/near-zero depth and pass."""
import json
import sys

TOL = 5.0  # uu

def aabb(piece):
    px, py, pz = piece["pos"]
    sx, sy, sz = [v / 2.0 for v in piece["size"]]
    return (px - sx, px + sx), (py - sy, py + sy), (pz - sz, pz + sz)

def depth(a, b):
    ds = []
    for i in range(3):
        lo = max(a[i][0], b[i][0])
        hi = min(a[i][1], b[i][1])
        ds.append(hi - lo)
    return ds

def main(path):
    spec = json.load(open(path, encoding="utf-8"))
    pieces = [p for p in spec["pieces"] if p.get("shape") != "ramp"]
    bad = 0
    for i in range(len(pieces)):
        for j in range(i + 1, len(pieces)):
            a, b = aabb(pieces[i]), aabb(pieces[j])
            d = depth(a, b)
            if all(v > TOL for v in d):
                bad += 1
                print("OVERLAP %s <-> %s  depths=%s" % (
                    pieces[i]["id"], pieces[j]["id"],
                    ["%.0f" % v for v in d]))
    print("checked %d pieces: %d overlapping pairs (tol %suu)" % (len(pieces), bad, TOL))
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
