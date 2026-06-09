"""3-row swatch comparison: BEFORE / REFERENCE grade / LIGHT grade."""
import json, colorsys
import numpy as np
from PIL import Image
from bake_lut import apply_grade

SWATCHES = [
    ("acid grass",   (0.55, 0.72, 0.13)),
    ("hedge ivy",    (0.62, 0.78, 0.18)),
    ("palm frond",   (0.18, 0.42, 0.16)),
    ("turquoise",    (0.25, 0.79, 0.80)),
    ("deep water",   (0.11, 0.37, 0.54)),
    ("sky blue",     (0.36, 0.67, 0.87)),
    ("coral sand",   (0.93, 0.88, 0.78)),
    ("warm wood",    (0.78, 0.50, 0.22)),
    ("white arch",   (0.95, 0.95, 0.94)),
    ("cyan enemy",   (0.10, 0.95, 1.00)),
    ("red accent",   (0.85, 0.15, 0.12)),
    ("mid grey",     (0.50, 0.50, 0.50)),
]

with open("final_spec.json", encoding="utf-8") as f:
    ref = json.load(f)
with open("light_spec.json", encoding="utf-8") as f:
    light = json.load(f)

base = np.array([[c for _, c in SWATCHES]], dtype=np.float64)  # (1,n,3)
rows = [base, apply_grade(base, ref), apply_grade(base, light)]
labels = ["BEFORE", "REFERENCE (current)", "LIGHT (new)"]

n = len(SWATCHES); sw = 64; gap = 4
H = len(rows) * (sw + gap) + gap
W = n * (sw + gap) + gap
img = np.ones((H, W, 3))
for r, row in enumerate(rows):
    y0 = gap + r * (sw + gap)
    for i in range(n):
        x0 = gap + i * (sw + gap)
        img[y0:y0+sw, x0:x0+sw] = row[0, i]
Image.fromarray(np.clip(img*255+0.5,0,255).astype(np.uint8), "RGB").save("comparison_3row.png")
print("wrote comparison_3row.png  (row1 BEFORE, row2 REFERENCE, row3 LIGHT)")

def sat(c):
    h,l,s = colorsys.rgb_to_hls(*c); return s
print(f"{'swatch':12s} {'BEFORE':>7s} {'REF':>7s} {'LIGHT':>7s}  (saturation)")
for i,(name,_) in enumerate(SWATCHES):
    print(f"{name:12s} {sat(rows[0][0,i]):7.2f} {sat(rows[1][0,i]):7.2f} {sat(rows[2][0,i]):7.2f}")
