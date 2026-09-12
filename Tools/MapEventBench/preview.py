"""Карта стенда сверху, картинкой, без редактора.

Нужна затем же, зачем стадии с картинками в скилле map_blockout: посмотреть на раскладку раньше,
чем поднимать движок. Рисует высоту серым, поверх неё площадки и лейны, горло и открытку разными
цветами. Читается за секунду, стоит ноль.

    python preview.py            -> map_preview.png рядом со спекой
"""

import math
import os
import struct
import zlib

import numpy as np

import terrain

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "map_preview.png")

COLORS = {
    "HQ_A":            (80, 140, 255),
    "HQ_B":            (255, 90, 80),
    "FINAL":           (255, 215, 60),
    "P_Mission_West":  (255, 150, 60),
    "P_Mission_East":  (255, 150, 60),
    "P_Mission_North": (255, 150, 60),
    "P_Plain_South":   (235, 235, 235),
    "LAUNCH":          (110, 235, 130),
}
CHOKE = (230, 70, 230)
OPEN = (90, 220, 190)
EXIT = (120, 255, 255)


def write_png_rgb(path, rgb):
    h, w, _ = rgb.shape
    rows = bytearray()
    data = rgb.astype(np.uint8)
    for y in range(h):
        rows.append(0)
        rows += data[y].tobytes()

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(rows), 6))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as fh:
        fh.write(png)
    return path


def to_px(spec, x, y):
    """Мир -> пиксель. Ось Y переворачивается, чтобы север карты был вверху картинки."""
    step = spec["world"]["vertex_spacing_uu"]
    lo = -terrain.half_extent(spec)
    n = terrain.grid_size(spec)
    ix = int(round((x - lo) / step))
    iy = int(round((y - lo) / step))
    return max(0, min(n - 1, ix)), max(0, min(n - 1, n - 1 - iy))


def stroke(rgb, spec, p0, p1, color, thickness):
    n = terrain.grid_size(spec)
    x0, y0 = to_px(spec, *p0)
    x1, y1 = to_px(spec, *p1)
    steps = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
    for s in range(steps + 1):
        t = s / float(steps)
        cx = int(round(x0 + (x1 - x0) * t))
        cy = int(round(y0 + (y1 - y0) * t))
        r = thickness
        a0, b0 = max(0, cy - r), min(n, cy + r + 1)
        c0, d0 = max(0, cx - r), min(n, cx + r + 1)
        rgb[a0:b0, c0:d0] = color


def disc(rgb, spec, centre, radius_uu, color, filled=False):
    step = spec["world"]["vertex_spacing_uu"]
    n = terrain.grid_size(spec)
    cx, cy = to_px(spec, *centre)
    r = int(radius_uu / step)
    a0, b0 = max(0, cy - r - 1), min(n, cy + r + 2)
    c0, d0 = max(0, cx - r - 1), min(n, cx + r + 2)
    ys = np.arange(a0, b0)[:, None]
    xs = np.arange(c0, d0)[None, :]
    d = np.hypot(xs - cx, ys - cy)
    mask = d <= r if filled else (d <= r) & (d >= r - 2)
    sub = rgb[a0:b0, c0:d0]
    sub[mask] = color
    rgb[a0:b0, c0:d0] = sub


def main():
    spec = terrain.load_spec()
    z = terrain.height_field(spec)
    by_id = terrain.pad_by_id(spec)

    # высота серым, север вверху
    zz = np.flipud(z)
    lo, hi = float(zz.min()), float(zz.max())
    g = (zz - lo) / max(hi - lo, 1.0)
    shade = (40.0 + g * 185.0)

    # штриховка склонов, чтобы стены читались рельефом, а не только яркостью
    gy, gx = np.gradient(zz, spec["world"]["vertex_spacing_uu"])
    relief = np.clip(0.5 + (gx * 1.4 - gy * 1.4) * 30.0, 0.2, 1.35)
    shade = np.clip(shade * relief, 0, 255)

    rgb = np.dstack([shade, shade, shade]).astype(np.uint8)

    for ln in spec["lanes"]:
        color = CHOKE if ln["kind"] == "choke" else OPEN
        stroke(rgb, spec, by_id[ln["from"]]["xy"], by_id[ln["to"]]["xy"], color, 2)

    for p in spec["pads"]:
        col = COLORS.get(p["id"], (255, 255, 255))
        disc(rgb, spec, p["xy"], p["flat"], col, filled=False)
        disc(rgb, spec, p["xy"], 700.0, col, filled=True)

    for e in spec.get("extractions", []):
        disc(rgb, spec, e["xy"], 1100.0, EXIT, filled=True)

    write_png_rgb(OUT, rgb)
    print("[PREVIEW] WROTE:", OUT, "{}x{}".format(rgb.shape[1], rgb.shape[0]))
    print("[PREVIEW] высота {:.0f}..{:.0f} uu; малиновый лейн = горло, бирюзовый = открытка".format(lo, hi))
    return OUT


if __name__ == "__main__":
    main()
