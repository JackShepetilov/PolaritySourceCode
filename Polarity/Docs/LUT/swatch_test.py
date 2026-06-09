"""Visual sanity check: apply the final grade to representative scene colors, save before/after."""
import json
import numpy as np
from PIL import Image
from bake_lut import apply_grade

SWATCHES = [
    ("acid grass",   (0.55, 0.72, 0.13)),  # chartreuse — #1 fix
    ("hedge ivy",    (0.62, 0.78, 0.18)),
    ("palm frond",   (0.18, 0.42, 0.16)),
    ("turquoise",    (0.25, 0.79, 0.80)),  # hero water — protect
    ("deep water",   (0.11, 0.37, 0.54)),
    ("sky blue",     (0.36, 0.67, 0.87)),
    ("coral sand",   (0.93, 0.88, 0.78)),  # should stay cream, not olive
    ("warm wood",    (0.78, 0.50, 0.22)),  # oversat amber — tame, keep rich
    ("white arch",   (0.95, 0.95, 0.94)),  # should stay clean-ish
    ("cyan enemy",   (0.10, 0.95, 1.00)),  # protect
    ("red accent",   (0.85, 0.15, 0.12)),  # keep chroma (red=1.0)
    ("mid grey",     (0.50, 0.50, 0.50)),  # should stay ~neutral
]

with open("final_spec.json", encoding="utf-8") as f:
    spec = json.load(f)

n = len(SWATCHES)
sw = 64   # swatch width
gap = 4
H = 160   # before row + after row
W = n * (sw + gap) + gap
img = np.ones((H, W, 3))  # white bg

before = np.array([[c for _, c in SWATCHES]])  # (1, n, 3)
after = apply_grade(before.astype(np.float64), spec)

for i in range(n):
    x0 = gap + i * (sw + gap)
    img[10:70, x0:x0+sw] = SWATCHES[i][1]          # before (top)
    img[90:150, x0:x0+sw] = after[0, i]            # after (bottom)

img8 = np.clip(img * 255 + 0.5, 0, 255).astype(np.uint8)
Image.fromarray(img8, "RGB").save("swatch_before_after.png")
print("wrote swatch_before_after.png   (top row = BEFORE, bottom row = AFTER)")

# print numeric HSL deltas for the record
import colorsys
def hsl(c):
    h, l, s = colorsys.rgb_to_hls(*c)
    return h*360, s, l
for i, (name, c) in enumerate(SWATCHES):
    hb = hsl(c); ha = hsl(tuple(after[0, i]))
    print(f"{name:12s} H {hb[0]:6.1f}->{ha[0]:6.1f}  S {hb[1]:.2f}->{ha[1]:.2f}  L {hb[2]:.2f}->{ha[2]:.2f}")
