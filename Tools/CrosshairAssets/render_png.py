"""Render crosshair_classic.svg to a transparent PNG with Pillow.

cairosvg/svglib aren't installed, so instead of parsing SVG we redraw the (simple, known)
shapes directly. Supersample 4x then LANCZOS-downscale for crisp anti-aliased edges.
Geometry mirrors crosshair_classic.svg (viewBox 160 units).
"""
import os
from PIL import Image, ImageDraw

S = 4  # supersample factor
HERE = os.path.dirname(os.path.abspath(__file__))
LANCZOS = getattr(Image, "Resampling", Image).LANCZOS

DARK = (5, 7, 10, 153)  # outline halo, ~0.60 alpha
WHITE = (255, 255, 255, 255)


def rrect(d, x, y, w, h, r, fill, k):
    d.rounded_rectangle([(x * k, y * k), ((x + w) * k, (y + h) * k)], radius=r * k, fill=fill)


def circ(d, cx, cy, r, k, fill=None, outline=None, width=0):
    box = [((cx - r) * k, (cy - r) * k), ((cx + r) * k, (cy + r) * k)]
    d.ellipse(box, fill=fill, outline=outline, width=int(round(width * k)))


W = H = 256
k = (W * S) / 160.0
img = Image.new("RGBA", (W * S, H * S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)

for (x, y, w, h) in [(75, 22, 10, 38), (75, 100, 10, 38), (22, 75, 38, 10), (100, 75, 38, 10)]:
    rrect(d, x, y, w, h, 5, DARK, k)
for (x, y, w, h) in [(77, 24, 6, 34), (77, 102, 6, 34), (24, 77, 34, 6), (102, 77, 34, 6)]:
    rrect(d, x, y, w, h, 3, WHITE, k)

img.resize((W, H), LANCZOS).save(os.path.join(HERE, "crosshair_classic.png"))
print("wrote crosshair_classic.png", f"{W}x{H}")
