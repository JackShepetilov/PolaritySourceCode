"""Где проверка П19 находит треугольники 30-50 вне обрывов: для отладки terrain_lanes.py."""
import collections
import math

import numpy as np

import terrain_lanes as t

m = t.build()
z = m["z"]
step = 100.0
t1 = np.degrees(np.arctan(np.hypot(z[1:, :-1] - z[:-1, :-1], z[:-1, 1:] - z[:-1, :-1]) / step))
t2 = np.degrees(np.arctan(np.hypot(z[1:, 1:] - z[:-1, 1:], z[1:, 1:] - z[1:, :-1]) / step))
play = (np.minimum(m["half"] - np.abs(m["X"]), m["half"] - np.abs(m["Y"])) > t.EDGE_BAND)[:-1, :-1]
cl = m["cliffs"].copy()
for _ in range(2):
    g = np.pad(cl, 1, mode="edge")
    cl = cl | g[:-2, 1:-1] | g[2:, 1:-1] | g[1:-1, :-2] | g[1:-1, 2:]
at = cl[:-1, :-1] | cl[1:, :-1] | cl[:-1, 1:] | cl[1:, 1:]
b = (((t1 > 30) & (t1 < 50)) | ((t2 > 30) & (t2 < 50))) & play & ~at
ys, xs = np.nonzero(b)
c = t.CORNER
P = m["pinned"]
cnt = collections.Counter()
ex = []
for i in range(len(xs)):
    iy, ix = ys[i], xs[i]
    X = m["X"][iy, ix]
    Y = m["Y"][iy, ix]
    dB = math.hypot(X + c, Y + c)
    dE = math.hypot(X - c, Y - c)
    if dB < dE:
        bb = math.degrees(math.atan2(Y + c, X + c)) % 360
        cnt[(round(dB / 500) * 5, round(bb / 10) * 10)] += 1
        if len(ex) < 8:
            ex.append((round(dB), round(bb), [(round(z[iy + a, ix + q]), int(P[iy + a, ix + q]))
                                              for a, q in ((0, 0), (0, 1), (1, 0), (1, 1), (-1, 0), (0, -1))]))
print(len(xs))
print(sorted(cnt.items(), key=lambda kv: -kv[1])[:14])
for e in ex:
    print(e)
