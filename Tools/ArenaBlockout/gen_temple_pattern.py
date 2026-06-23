# Generates a TILEABLE texture matching the LSJ temple PLAZA: a RED square-spiral MEANDER/LABYRINTH
# (a single continuous line spiralling inward, Greek-key style) on a WHITE ground, grid of tiles.
# This matches the real plaza photo (connected meander lines), not closed concentric rings.
# 2048x2048, pure Python. Output: DownloadedTextures/temple_pattern.png

import os
import struct
import zlib

W = H = 2048
WHITE = (243, 238, 227)
RED = (176, 42, 38)
CELLS = 4
GAP = 50          # spacing between spiral arms
HT = 13           # half line thickness (line = 2*HT ~ 26px)
MARGIN = 6
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "DownloadedTextures", "temple_pattern.png")

px = bytearray(bytes(WHITE) * (W * H))


def fill_rect(x0, y0, x1, y1, c):
    x0 = max(0, int(x0)); y0 = max(0, int(y0)); x1 = min(W, int(x1)); y1 = min(H, int(y1))
    if x1 <= x0 or y1 <= y0:
        return
    row = bytes(c) * (x1 - x0)
    n = len(row)
    for y in range(y0, y1):
        s = (y * W + x0) * 3
        px[s:s + n] = row


def seg(x0, y0, x1, y1, c):
    fill_rect(min(x0, x1) - HT, min(y0, y1) - HT, max(x0, x1) + HT, max(y0, y1) + HT, c)


def spiral_points(half):
    # clockwise square spiral from outer corner inward; returns relative points
    pts = [(-half, -half)]
    x, y = -half, -half
    dirs = [(1, 0), (0, 1), (-1, 0), (0, -1)]
    length = 2 * half
    di = 0
    while length > GAP:
        dx, dy = dirs[di % 4]
        x += dx * length
        y += dy * length
        pts.append((x, y))
        di += 1
        if di % 2 == 0:
            length -= GAP
    return pts


def main():
    cs = W // CELLS
    half = cs // 2 - MARGIN
    base = spiral_points(half)
    for gy in range(CELLS):
        for gx in range(CELLS):
            cx = gx * cs + cs // 2
            cy = gy * cs + cs // 2
            pts = [(cx + px_, cy + py_) for (px_, py_) in base]
            for i in range(len(pts) - 1):
                seg(pts[i][0], pts[i][1], pts[i + 1][0], pts[i + 1][1], RED)

    raw = bytearray()
    for y in range(H):
        raw.append(0)
        raw.extend(px[y * W * 3:(y + 1) * W * 3])
    comp = zlib.compress(bytes(raw), 6)

    def chunk(typ, data):
        return (struct.pack(">I", len(data)) + typ + data +
                struct.pack(">I", zlib.crc32(typ + data) & 0xffffffff))

    blob = (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", comp) + chunk(b"IEND", b""))
    if not os.path.isdir(os.path.dirname(OUT)):
        os.makedirs(os.path.dirname(OUT))
    with open(OUT, "wb") as f:
        f.write(blob)
    print("wrote", OUT, os.path.getsize(OUT), "bytes")


main()
