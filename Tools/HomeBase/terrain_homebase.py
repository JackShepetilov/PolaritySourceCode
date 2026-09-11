"""Рельеф прототипа базы игрока: холм на 25 м, дом на макушке.

Вне редактора (нужен numpy):
    python terrain_homebase.py      посчитать, проверить, выложить heightmap.png, preview.png, layout.json

Оси: север = +X, восток = +Y, центр холма в (0, 0). Азимут: 0 север, 90 восток, 180 юг, 270 запад.
Единицы uu (1 м = 100 uu).

Что лепится, по порядку:
    1. холм      вогнутый радиальный профиль z = H * (1 - s)^P, край макушки и ширина склона
                 шумят по азимуту, чтобы не было циркуля. Вогнутый значит: круче всего у бровки,
                 внизу полого, и с бровки видно весь склон без мёртвой зоны
    2. уступ     на востоке склон начинается не от макушки, а на 18 м ниже: вертикальная стенка
                 по дуге, концы сходят на нет. Стенку закрывают коробки (layout.json)
    3. волны     пологий шум вокруг холма, чтобы округа не была столом
    4. дорога    спираль по склону ровно на отметке земли, поэтому без насыпей
    5. овраг     узкая выемка на западо-северо-западе
    6. зажим     ограничитель уклона чистит стыки, пиненное не трогает

Правила: Docs/LevelDesign_Rules_Map_2026-09-01.md (П19 уклоны, П28 площадки, П60 проверки).
"""

import json
import math
import os
import struct
import sys
import zlib

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "MapEventBench"))
import terrain as bench  # noqa: E402  export_png

WORLD = {
    "landscape_label": "HomeBaseTerrain",
    "quads_per_section": 63,
    "sections_per_component": 1,
    "component_count": 8,
    "vertex_spacing_uu": 100.0,
    "z_scale": 100.0,
}

H = 2500.0            # макушка над округой
R_TOP = 5500.0        # средний радиус макушки
R_TOP_NOISE = 350.0   # разброс края макушки; забор на 4000 должен остаться внутри
P_EXP = 1.4           # вогнутость; уклон у бровки = P * H / L
L_BASE = 7500.0       # ширина склона по радиусу
L_NORTH = 9000.0      # северное поле положе
L_NOISE = 0.08        # разброс ширины склона, доля; при 0.12 бровка местами круче лимита зажима

SCARP_MAX = 1800.0    # высота стенки уступа в центре
SCARP_AT = 90.0       # азимут центра уступа
SCARP_FULL = 15.0     # полуширина дуги, где стенка в полную высоту, градусы
SCARP_END = 60.0      # полуширина, где стенка сошла на нет; уже 45 давало складки по склону
SCARP_BOX_MIN = 40.0  # ниже этой высоты стенку не закрываем, её пригладит зажим

ROLL_AMP = 180.0      # волны вокруг холма
ROLL_FADE = 3000.0

FENCE_R = 4000.0
GATE_AT = 165.0       # ворота и начало дороги
ROAD_W = 800.0
ROAD_GRADE_DEG = 10.0
RAVINE_AT = 290.0
RAVINE_W = 600.0
RAVINE_DEPTH = 450.0
FOG_R = 15000.0

MAX_SLOPE = 30.0
BAND = (30.0, 50.0)


def log(msg):
    print("[HOMEBASE] {}".format(msg))


# ==================== сетка и азимут ====================

def grid():
    n = WORLD["quads_per_section"] * WORLD["sections_per_component"] * WORLD["component_count"] + 1
    step = WORLD["vertex_spacing_uu"]
    half = (n - 1) * step * 0.5
    a = np.array([-half + i * step for i in range(n)], dtype=np.float64)
    X, Y = np.meshgrid(a, a)
    return X, Y, n, step, half


def bearing(x, y):
    return np.degrees(np.arctan2(y, x)) % 360.0


def ang_dist(a, b):
    return np.abs((np.asarray(a) - b + 180.0) % 360.0 - 180.0)


def smoothstep(t):
    t = np.clip(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def ring_noise(theta_deg, seed):
    """Гладкий шум по кругу, примерно -1..1. Сумма гармоник со случайными фазами."""
    th = np.radians(theta_deg)
    out = 0.0
    for k, amp in ((2, 0.5), (3, 0.3), (5, 0.2)):
        ph = ((seed * 7919 + k * 104729) % 1000) / 1000.0 * 2.0 * math.pi
        out = out + amp * np.sin(k * th + ph)
    return out


# ==================== профиль холма ====================

def r_top(theta):
    return R_TOP + R_TOP_NOISE * ring_noise(theta, 1)


def slope_len(theta):
    north = np.clip(np.cos(np.radians(theta)), 0.0, 1.0) ** 2
    base = L_BASE + (L_NORTH - L_BASE) * north
    return base * (1.0 + L_NOISE * ring_noise(theta, 2))


def scarp(theta):
    return SCARP_MAX * smoothstep((SCARP_END - ang_dist(theta, SCARP_AT)) / (SCARP_END - SCARP_FULL))


def rolling(x, y, r_foot):
    waves = ((7000.0, 0.35, 0.4), (11000.0, 1.9, 0.35), (17000.0, 4.1, 0.25))
    z = 0.0
    for wl, ang, amp in waves:
        c, s = math.cos(ang), math.sin(ang)
        u, v = x * c + y * s, -x * s + y * c
        z = z + amp * np.sin(u * 2 * math.pi / wl + ang * 3.1) * np.sin(v * 2 * math.pi / (wl * 1.3) + ang)
    r = np.hypot(x, y)
    return ROLL_AMP * z * smoothstep((r - r_foot) / ROLL_FADE)


def ground(x, y):
    """Земля без дороги и оврага. Работает и на сетке, и на одной точке."""
    x = np.asarray(x, dtype=np.float64)
    y = np.asarray(y, dtype=np.float64)
    r = np.hypot(x, y)
    th = bearing(x, y)
    rt = r_top(th)
    L = slope_len(th)
    hb = H - scarp(th)
    Lb = L * hb / H                      # уступ укорачивает склон, уклон у его бровки прежний
    s = np.clip((r - rt) / Lb, 0.0, 1.0)
    z = np.where(r <= rt, H, hb * (1.0 - s) ** P_EXP)
    return z + rolling(x, y, rt + Lb)


# ==================== дорога и овраг ====================

def trace_road():
    """Спираль от ворот по склону: на каждом шаге выбирается направление, где земля падает ровно
    на заданный уклон. Дорога лежит на отметке земли, поэтому насыпей и выемок почти нет."""
    g = math.tan(math.radians(ROAD_GRADE_DEG))
    th0 = math.radians(GATE_AT)
    pts = [(FENCE_R * math.cos(th0), FENCE_R * math.sin(th0))]
    rt0 = float(r_top(GATE_AT))
    x, y = (rt0 - 150.0) * math.cos(th0), (rt0 - 150.0) * math.sin(th0)
    pts.append((x, y))
    ds = 100.0
    z = float(ground(x, y))
    for _ in range(400):
        r = math.hypot(x, y)
        tx, ty = -y / r, x / r               # касательная в сторону роста азимута (на запад)
        ox, oy = x / r, y / r
        best = None
        for phi in np.radians(np.arange(0.0, 86.0, 2.0)):
            dx = math.cos(phi) * tx + math.sin(phi) * ox
            dy = math.cos(phi) * ty + math.sin(phi) * oy
            nz = float(ground(x + dx * ds, y + dy * ds))
            err = abs((z - nz) - g * ds)
            if best is None or err < best[0]:
                best = (err, dx, dy, nz)
        _, dx, dy, nz = best
        x, y, z = x + dx * ds, y + dy * ds, nz
        pts.append((x, y))
        if z < 60.0 or math.hypot(x, y) > FOG_R + 2000.0:
            break
    # Хвост в туман по радиусу.
    r = math.hypot(x, y)
    pts.append((x / r * (FOG_R + 2500.0), y / r * (FOG_R + 2500.0)))
    return pts


def ravine_points():
    out = []
    for r, off in ((4300.0, 0.0), (5700.0, 0.0), (8500.0, -3.0), (11500.0, 2.0), (13500.0, 0.0)):
        th = math.radians(RAVINE_AT + off)
        out.append((r * math.cos(th), r * math.sin(th)))
    return out


def polyline_param(X, Y, pts):
    best_d = np.full_like(X, 1e12)
    best_s = np.zeros_like(X)
    s0 = 0.0
    for (x0, y0), (x1, y1) in zip(pts[:-1], pts[1:]):
        vx, vy = x1 - x0, y1 - y0
        seg = max(math.hypot(vx, vy), 1e-6)
        t = np.clip(((X - x0) * vx + (Y - y0) * vy) / (seg * seg), 0.0, 1.0)
        d = np.hypot(X - (x0 + t * vx), Y - (y0 + t * vy))
        closer = d < best_d
        best_d = np.where(closer, d, best_d)
        best_s = np.where(closer, s0 + t * seg, best_s)
        s0 += seg
    return best_s, best_d


def lengths(pts):
    out = [0.0]
    for (x0, y0), (x1, y1) in zip(pts[:-1], pts[1:]):
        out.append(out[-1] + math.hypot(x1 - x0, y1 - y0))
    return np.array(out)


# ==================== уступ: коробки ====================

def scarp_boxes():
    boxes = []
    step = 4.0
    a = SCARP_AT - SCARP_END
    while a < SCARP_AT + SCARP_END:
        mid = a + step * 0.5
        h = float(scarp(mid))
        if h >= SCARP_BOX_MIN:
            rt = float(r_top(mid))
            r0, r1 = rt - 300.0, rt + 300.0
            chord = 2.0 * r1 * math.sin(math.radians(step * 0.5)) + 80.0
            rc = (r0 + r1) * 0.5
            th = math.radians(mid)
            boxes.append({"kind": "scarp", "x": rc * math.cos(th), "y": rc * math.sin(th), "yaw": mid,
                          "len": r1 - r0, "wid": chord, "z0": H - h - 300.0, "z1": H})
        a += step
    return boxes


def box_mask(X, Y, boxes, pad=0.0):
    m = np.zeros_like(X, dtype=bool)
    for b in boxes:
        yaw = math.radians(b["yaw"])
        c, s = math.cos(yaw), math.sin(yaw)
        dx, dy = X - b["x"], Y - b["y"]
        along = dx * c + dy * s
        across = -dx * s + dy * c
        m |= (np.abs(along) <= b["len"] * 0.5 + pad) & (np.abs(across) <= b["wid"] * 0.5 + pad)
    return m


# ==================== сборка поля ====================

def relax(z, pinned, step, iterations=800):
    lim_o = math.tan(math.radians(MAX_SLOPE * 0.93)) * step
    lim_d = lim_o * math.sqrt(2.0)
    fixed = z.copy()
    moved = 0.0
    for i in range(iterations):
        a = np.pad(z, 1, mode="edge")
        nb = [(a[:-2, 1:-1], lim_o), (a[2:, 1:-1], lim_o), (a[1:-1, :-2], lim_o), (a[1:-1, 2:], lim_o),
              (a[:-2, :-2], lim_d), (a[:-2, 2:], lim_d), (a[2:, :-2], lim_d), (a[2:, 2:], lim_d)]
        upper = np.minimum.reduce([v + l for v, l in nb])
        lower = np.maximum.reduce([v - l for v, l in nb])
        new_z = np.where(pinned, fixed, np.clip(z, lower, upper))
        moved = float(np.abs(new_z - z).max())
        z = new_z
        if moved < 0.5:
            log("зажим сошёлся за {} итераций".format(i + 1))
            return z
    log("зажим НЕ сошёлся, последний сдвиг {:.1f}".format(moved))
    return z


def build():
    X, Y, n, step, half = grid()
    r = np.hypot(X, Y)
    th = bearing(X, Y)
    base = ground(X, Y)
    z = base.copy()

    road = trace_road()
    road_L = lengths(road)
    road_zc = np.array([float(ground(px, py)) for px, py in road])
    road_zc[:2] = H
    s, d = polyline_param(X, Y, road)
    zr = np.interp(s, road_L, road_zc)
    hw = ROAD_W * 0.5
    span = np.clip(3.2 * np.abs(zr - base), 300.0, 2500.0)
    w = 1.0 - smoothstep((d - hw) / span)
    z = z * (1.0 - w) + zr * w
    road_floor = d <= hw

    rav = ravine_points()
    s2, d2 = polyline_param(X, Y, rav)
    total = lengths(rav)[-1]
    depth = RAVINE_DEPTH * np.minimum(smoothstep(s2 / 1300.0), smoothstep((total - s2) / 2000.0))
    zf = base - depth
    hw2 = RAVINE_W * 0.5
    span2 = np.clip(3.2 * depth, 300.0, 3000.0)
    w2 = 1.0 - smoothstep((d2 - hw2) / span2)
    z = np.minimum(z, z * (1.0 - w2) + zf * w2)
    ravine_floor = (d2 <= hw2) & (depth > 1.0)

    boxes = scarp_boxes()
    covered = box_mask(X, Y, boxes)
    # Макушка пинится до самой бровки. Если оставить незакреплённое кольцо у края, зажим видит
    # там склон чуть круче лимита и срезает бровку внутрь двора на метры (было до 6 м).
    # Исключение одно: над дорогой. Плоское полотно, врезанное в склон под бровкой, делает откос
    # над собой круче склона, и без права срезать край он не помещается. Там бровка проседает,
    # как настоящая выемка у дороги. Путь от ворот по самой макушке не в счёт, он на отметке H.
    near_road_below = (d < hw + 3500.0) & (zr < H - 1.0)
    plateau = (r <= r_top(th)) & (d2 > hw2 + 3.2 * RAVINE_DEPTH + 800.0) & ~road_floor & ~near_road_below
    z = np.where(road_floor, zr, z)
    z = np.where(ravine_floor, zf, z)
    z = np.where(plateau, H, z)
    pinned = plateau | road_floor | ravine_floor | covered
    z = relax(z, pinned, step)
    return {"X": X, "Y": Y, "z": z, "boxes": boxes, "plateau": plateau, "road": road,
            "road_z": road_zc.tolist(), "ravine": rav, "ravine_floor": ravine_floor, "road_floor": road_floor}


# ==================== проверки ====================

def analyse(m):
    X, Y, z = m["X"], m["Y"], m["z"]
    step = WORLD["vertex_spacing_uu"]
    gy, gx = np.gradient(z, step)
    slope = np.degrees(np.arctan(np.hypot(gx, gy)))
    live = ~box_mask(X, Y, m["boxes"], pad=2.0 * step)
    fails = []
    band = live & (slope >= BAND[0]) & (slope <= BAND[1])
    over = live & (slope > MAX_SLOPE)
    log("уклон вне уступа: максимум {:.1f}, средний {:.1f}".format(slope[live].max(), slope[live].mean()))
    for name, mask in (("полоса 30-50", band), ("круче 30", over)):
        if mask.any():
            ys, xs = np.nonzero(mask)
            fails.append("{}: {} вершин, например {}".format(
                name, int(mask.sum()), [(int(X[a, b]), int(Y[a, b])) for a, b in list(zip(ys, xs))[:4]]))
    inner = m["plateau"] & (np.hypot(X, Y) <= R_TOP - R_TOP_NOISE - 400.0)
    log("макушка: уклон {:.2f}".format(float(slope[inner].max())))
    rz = np.array(m["road_z"])
    L = lengths(m["road"])
    grades = np.degrees(np.arctan(np.abs(np.diff(rz)) / np.maximum(np.diff(L), 1.0)))
    log("дорога: {} м, уклон до {:.1f} градуса, конец на азимуте {:.0f}".format(
        int(L[-1] / 100), float(grades[2:].max()), float(bearing(*m["road"][-1]))))
    log("уступ: коробок {}, стенка до {:.0f} м".format(len(m["boxes"]), SCARP_MAX / 100))
    for f in fails:
        log("ПРОВАЛ: " + f)
    log("итог: {} провалов".format(len(fails)))
    return not fails


# ==================== превью ====================

HOUSES = [((0.0, 0.0), (900.0, 1200.0)), ((-300.0, 2500.0), (1400.0, 1000.0)), ((2000.0, 1900.0), (500.0, 500.0))]


def _png(img, path):
    h, w, _ = img.shape
    raw = b"".join(b"\x00" + img[y].tobytes() for y in range(h))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    with open(path, "wb") as fh:
        fh.write(png)


def preview(m, path, crop_uu=20000.0, scale=2):
    """Сверху, север вверх, восток вправо. Отмывка + горизонтали через 5 м + дорога, овраг, забор,
    уступ, постройки, кольцо тумана. Только для глаз."""
    X, Y, z = m["X"], m["Y"], m["z"]
    step = WORLD["vertex_spacing_uu"]
    gy, gx = np.gradient(z, step)
    l = np.array([-0.6, -0.4, 0.7])
    l = l / np.linalg.norm(l)
    nrm = np.sqrt(gx * gx + gy * gy + 1.0)
    shade = np.clip((-gx * l[0] - gy * l[1] + l[2]) / nrm, 0.0, 1.0) ** 1.3
    h = np.clip(z / H, 0.0, 1.0)[..., None]
    low = np.array([0.36, 0.50, 0.28])
    high = np.array([0.82, 0.76, 0.55])
    rgb = (low + (high - low) * h) * (0.35 + 0.65 * shade[..., None])

    contour = (np.floor(z / 500.0) != np.floor(np.roll(z, 1, 0) / 500.0)) | \
              (np.floor(z / 500.0) != np.floor(np.roll(z, 1, 1) / 500.0))
    rgb[contour] = rgb[contour] * 0.55

    r = np.hypot(X, Y)

    def paint(mask, color):
        rgb[mask] = color

    paint(m["road_floor"], (0.55, 0.52, 0.48))
    paint(m["ravine_floor"], (0.25, 0.45, 0.75))
    paint(box_mask(X, Y, m["boxes"]), (0.55, 0.22, 0.18))
    paint(np.abs(r - FENCE_R) < 60.0, (1.0, 1.0, 1.0))
    paint(np.abs(r - FOG_R) < 60.0, (0.7, 0.7, 0.75))
    for (cx, cy), (sx, sy) in HOUSES:
        paint((np.abs(X - cx) <= sx * 0.5) & (np.abs(Y - cy) <= sy * 0.5), (0.1, 0.1, 0.1))

    img = (np.clip(rgb, 0, 1) * 255).astype(np.uint8)
    # Строки массива это Y, столбцы X. Нужно: вверх +X, вправо +Y.
    img = img.transpose(1, 0, 2)[::-1, :, :]
    n = img.shape[0]
    c = n // 2
    k = int(crop_uu / step)
    img = img[c - k:c + k + 1, c - k:c + k + 1]
    img = np.repeat(np.repeat(img, scale, axis=0), scale, axis=1)
    _png(np.ascontiguousarray(img), path)
    log("WROTE: " + path)


def main():
    m = build()
    ok = analyse(m)
    bench.export_png({"world": WORLD}, m["z"], os.path.join(HERE, "heightmap.png"))
    preview(m, os.path.join(HERE, "preview.png"))
    X, Y, z = m["X"], m["Y"], m["z"]
    half = (z.shape[0] - 1) * WORLD["vertex_spacing_uu"] * 0.5

    def z_at(x, y):
        ix = int(round((x + half) / WORLD["vertex_spacing_uu"]))
        iy = int(round((y + half) / WORLD["vertex_spacing_uu"]))
        return float(z[iy, ix])

    rim = {str(a): float(r_top(a)) for a in range(0, 360, 5)}

    # Рощи на склонах между подходами: рвут прострелы с поля на макушку. Детерминированный разброс.
    rng = np.random.default_rng(7)
    _, d_road = polyline_param(X, Y, m["road"])
    _, d_rav = polyline_param(X, Y, m["ravine"])
    trees = []
    for (a0, a1, r0, r1, count) in ((120.0, 155.0, 7500.0, 12500.0, 14), (305.0, 345.0, 8000.0, 13000.0, 14),
                                    (35.0, 55.0, 9000.0, 13000.0, 8), (215.0, 240.0, 11000.0, 14000.0, 8)):
        placed = 0
        for _ in range(400):
            if placed >= count:
                break
            a = math.radians(rng.uniform(a0, a1))
            rr = rng.uniform(r0, r1)
            x, y = rr * math.cos(a), rr * math.sin(a)
            ix = int(round((x + half) / WORLD["vertex_spacing_uu"]))
            iy = int(round((y + half) / WORLD["vertex_spacing_uu"]))
            if d_road[iy, ix] < 900.0 or d_rav[iy, ix] < 700.0:
                continue
            if any(math.hypot(x - tx, y - ty) < 700.0 for tx, ty, _ in trees):
                continue
            trees.append((x, y, float(z[iy, ix])))
            placed += 1
    layout = {"world": WORLD, "half_extent": half, "H": H, "fence_r": FENCE_R, "gate_at": GATE_AT,
              "fog_r": FOG_R, "rim_r": rim, "road": m["road"], "road_z": m["road_z"],
              "ravine": m["ravine"], "ravine_at": RAVINE_AT, "scarp_boxes": m["boxes"], "trees_slope": trees,
              "spawn_ground_z": {"north": z_at(16000.0, 0.0), "west": z_at(0.0, -16000.0)}}
    with open(os.path.join(HERE, "layout.json"), "w", encoding="utf-8") as fh:
        json.dump(layout, fh, ensure_ascii=False, indent=1)
    log("WROTE: layout.json")
    return ok


if __name__ == "__main__":
    sys.exit(0 if main() else 1)
