"""Рельеф и разметка карты с линиями (MOBA): базы в углах, три линии, река посередине, лес.

Вне редактора (нужен numpy):
    python terrain_lanes.py      посчитать, проверить, выложить heightmap.png, preview.png, layout.json

Концепт: Docs/MOBA_Lanes_Concept_2026-09-29.md (раскладка «базы в углах» выбрана автором 2026-09-29).
Правила рельефа: Docs/LevelDesign_Rules_Map_2026-09-01.md (П19 уклоны, П20 шум, П28 площадки, П30 навмеш).

Оси как в L_HomeBase: север = +X, восток = +Y. Единицы uu (1 м = 100 uu). Центр карты (0, 0).
Наша база на юго-западе (-X, -Y), вражеская на северо-востоке (+X, +Y).

    TOP  от нашей базы на север вдоль западного края, в северо-западном углу поворот на восток
    MID  по диагонали через центр
    BOT  от нашей базы на восток вдоль южного края, в юго-восточном углу поворот на север
    река по второй диагонали X + Y = 0: граница половин, углы линий TOP и BOT лежат на ней

Что лепится, по порядку:
    1. основа    пологий шум, в лесу сильнее, на линиях слабее
    2. кромка    по краям карты подъём (граница, за край не уйти); тыл базы это два края карты
    3. река      мелкое русло по диагонали, проходимо вброд
    4. линии     полотно 14 м, плоское поперёк, пологое вдоль
    5. площадки  базы, места под турели, места под кемпы: плоские диски, пишутся последними (П28)
    6. зажим     ограничитель уклона чистит стыки, пиненное не трогает
"""

import json
import math
import os
import random
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "MapEventBench"))
import terrain as bench  # noqa: E402  export_png

WORLD = {
    "landscape_label": "LanesTerrain2x",
    "quads_per_section": 63,
    "sections_per_component": 1,
    # 1260 м: вдвое больше первой версии, чтобы мид стал вдвое длиннее (автор 2026-09-29).
    # Раскладка растянута в 2 раза, а то, что меряется телом (полотно, плато, пандусы, площадки,
    # русло), осталось в прежнем размере.
    "component_count": 20,
    "vertex_spacing_uu": 100.0,
    "z_scale": 100.0,
}

# ---- раскладка ----
CORNER = 46000.0          # центры баз и углы линий: 170 м от края карты
BASE_R = 5500.0           # плоская площадка базы
LANE_W = 1400.0           # полотно линии
RIVER_W = 1600.0          # ровное дно
RIVER_DEPTH = 250.0       # русло 2.5 м, дно из ландшафта, линии спускаются в него бродом
RIVER_BANK = 1500.0       # берег: 2.5 м на 15 м, около 10 градусов
HG_R = 6000.0             # хайграунд базы: плато радиусом 60 м
HG_H = 900.0              # на 9 м выше округи, по краю обрыв
RAMP_L = 3600.0           # пандус на каждой линии: 9 м на 36 м, около 14 градусов
RAMP_HALF = 1100.0        # полуширина пандуса: полотно линии и по 4 м запаса
EDGE_RISE = 1600.0        # подъём у края карты: не круче 26 градусов (smoothstep, 1.5 * 1600 / 5000), иначе хвосты подъёма в полосе 30-50
EDGE_BAND = 5000.0        # ширина подъёма
GROUND_AMP = 150.0        # шум на линиях и у баз
FOREST_AMP = 450.0        # шум в лесу
TURRET_PAD_R = 600.0
TURRET_SIDE = 1700.0      # турель в стороне от оси линии: край площадки за краем полотна, иначе два пина разной высоты дают обрыв
POI_R = 3000.0            # площадка кемпа: точка радиусом ~25 м + полоса (П28, у нас мельче базовых 40 м)
POI_MIN_GAP = 9500.0      # между центрами кемпов, 12.5 с бега: чуть меньше П2 (15 с), иначе в треугольник леса влезает два
POI_LANE_CLEAR = 4600.0   # от оси линии до центра кемпа
POI_RIVER_CLEAR = 4000.0
POI_PER_JUNGLE = 10
SEED = 7

# Турели нашей половины: доля пути от центра базы до реки по линии (3 на линию, как в доте).
TURRET_AT = {"T3": 0.22, "T2": 0.52, "T1": 0.84}
# Турели под троном (T4): от центра базы по биссектрисам между линиями.
T4_R = 2800.0

MAX_SLOPE = 30.0
BAND = (30.0, 50.0)


def log(msg):
    print("[LANES] {}".format(msg))


# ==================== сетка ====================

def grid():
    n = WORLD["quads_per_section"] * WORLD["sections_per_component"] * WORLD["component_count"] + 1
    step = WORLD["vertex_spacing_uu"]
    half = (n - 1) * step * 0.5
    a = np.array([-half + i * step for i in range(n)], dtype=np.float64)
    X, Y = np.meshgrid(a, a)
    return X, Y, n, step, half


def smoothstep(t):
    t = np.clip(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def value_noise(X, Y, cell, seed):
    """Гладкий шум значений -1..1 с ячейкой cell uu."""
    rng = np.random.RandomState(seed)
    half = float(np.abs(X).max()) + cell
    k = int(math.ceil(2.0 * half / cell)) + 2
    g = rng.uniform(-1.0, 1.0, size=(k, k))
    fx = (X + half) / cell
    fy = (Y + half) / cell
    ix, iy = np.floor(fx).astype(int), np.floor(fy).astype(int)
    tx, ty = smoothstep(fx - ix), smoothstep(fy - iy)
    a, b = g[iy, ix], g[iy, ix + 1]
    c, d = g[iy + 1, ix], g[iy + 1, ix + 1]
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty


# ==================== линии ====================

def rounded(points, radius=6000.0, seg=10):
    """Ломаная со скруглёнными углами: дуга радиуса radius в каждом внутреннем углу."""
    out = [points[0]]
    for i in range(1, len(points) - 1):
        p0, p1, p2 = (np.array(points[j], dtype=float) for j in (i - 1, i, i + 1))
        d0 = (p0 - p1) / np.linalg.norm(p0 - p1)
        d2 = (p2 - p1) / np.linalg.norm(p2 - p1)
        a = p1 + d0 * radius
        b = p1 + d2 * radius
        for k in range(seg + 1):
            t = k / float(seg)
            q = (1 - t) ** 2 * a + 2 * (1 - t) * t * p1 + t * t * b
            out.append((float(q[0]), float(q[1])))
    out.append(points[-1])
    return out


def lanes_full():
    """Три линии целиком, от нашей базы до вражеской."""
    c = CORNER
    return {
        "Top": rounded([(-c, -c), (c, -c), (c, c)]),
        "Mid": [(-c, -c), (0.0, 0.0), (c, c)],
        "Bot": rounded([(-c, -c), (-c, c), (c, c)]),
    }


def polyline_len(pts):
    return sum(math.hypot(pts[i + 1][0] - pts[i][0], pts[i + 1][1] - pts[i][1]) for i in range(len(pts) - 1))


def point_at(pts, dist):
    """Точка и направление на расстоянии dist вдоль ломаной."""
    for i in range(len(pts) - 1):
        (x0, y0), (x1, y1) = pts[i], pts[i + 1]
        seg = math.hypot(x1 - x0, y1 - y0)
        if dist <= seg or i == len(pts) - 2:
            t = min(max(dist / seg, 0.0), 1.0) if seg > 0 else 0.0
            return (x0 + (x1 - x0) * t, y0 + (y1 - y0) * t), ((x1 - x0) / seg, (y1 - y0) / seg)
        dist -= seg
    return pts[-1], (1.0, 0.0)


def river_cross_dist(pts):
    """Расстояние от начала ломаной до пересечения с рекой X + Y = 0."""
    acc = 0.0
    for i in range(len(pts) - 1):
        (x0, y0), (x1, y1) = pts[i], pts[i + 1]
        s0, s1 = x0 + y0, x1 + y1
        seg = math.hypot(x1 - x0, y1 - y0)
        if s0 <= 0.0 <= s1 or s1 <= 0.0 <= s0:
            t = s0 / (s0 - s1) if s0 != s1 else 0.0
            return acc + seg * t
        acc += seg
    return acc


def dist_to_polyline(X, Y, pts):
    d = np.full(X.shape, np.inf)
    for (x0, y0), (x1, y1) in zip(pts[:-1], pts[1:]):
        vx, vy = x1 - x0, y1 - y0
        L2 = vx * vx + vy * vy
        t = np.clip(((X - x0) * vx + (Y - y0) * vy) / L2, 0.0, 1.0)
        d = np.minimum(d, np.hypot(X - (x0 + t * vx), Y - (y0 + t * vy)))
    return d


def lane_splines(full):
    """Сплайны ASiegeLane: наша половина, от вражеской стороны к нашей базе.

    Начало на 60 м за рекой, в тумане: пачку не видно в момент появления. Конец в центре базы."""
    out = {}
    for name, pts in full.items():
        cross = river_cross_dist(pts)
        start = cross + 6000.0
        rev = list(reversed(pts))
        total = polyline_len(pts)
        # Точки ломаной между началом (на вражеской стороне) и базой, в обратном порядке.
        keep = [point_at(pts, start)[0]]
        acc = 0.0
        dists = [0.0]
        for i in range(len(pts) - 1):
            acc += math.hypot(pts[i + 1][0] - pts[i][0], pts[i + 1][1] - pts[i][1])
            dists.append(acc)
        inner = [pts[i] for i in range(len(pts)) if 0.0 < dists[i] < start]
        keep += list(reversed(inner))
        keep.append(pts[0])
        # Редкие точки: сплайн сам сгладит, а в редакторе их удобно тянуть руками.
        thin = [keep[0]]
        for p in keep[1:-1]:
            # 5 м: точки дуги угла (радиус 30 м) остаются, иначе сплайн срезает или раздувает поворот.
            if math.hypot(p[0] - thin[-1][0], p[1] - thin[-1][1]) >= 500.0:
                thin.append(p)
        thin.append(keep[-1])
        out[name] = {"points": thin, "length": polyline_len(thin), "total_full": total, "river_at": cross}
        del rev
    return out


def turret_pads(full):
    pads = []
    for name, pts in full.items():
        cross = river_cross_dist(pts)
        for tier, frac in TURRET_AT.items():
            (x, y), (dx, dy) = point_at(pts, max(cross * frac, HG_R + RAMP_L + 1500.0))
            # Сбоку от полотна, со стороны леса нашей половины (левая или правая сторона зависит от линии).
            nx, ny = -dy, dx
            side = 1.0 if name != "Bot" else -1.0
            if name == "Mid":
                side = 1.0
            pads.append({"name": "{}_{}".format(tier, name), "tier": tier, "lane": name,
                         "x": x + nx * TURRET_SIDE * side, "y": y + ny * TURRET_SIDE * side,
                         "axis": (x, y)})
    c = CORNER
    for i, ang in enumerate((67.5, 22.5)):   # между TOP и MID, между MID и BOT (азимут от +X к +Y)
        a = math.radians(ang)
        pads.append({"name": "T4_{}".format("A" if i == 0 else "B"), "tier": "T4", "lane": "Base",
                     "x": -c + T4_R * math.cos(a), "y": -c + T4_R * math.sin(a)})
    return pads


def jungle_slots(dists, pads=()):
    """Места под кемпы на нашей половине: два треугольника между линиями, до реки."""
    rng = random.Random(SEED)
    c = CORNER
    d_lanes, d_river = dists
    step = WORLD["vertex_spacing_uu"]
    # Половина карты из сетки, а не «угол + 85 м»: после растяжки карты вдвое старая формула
    # читала расстояние до линии не из той клетки, и кемпы вставали на полотно.
    half = WORLD["quads_per_section"] * WORLD["sections_per_component"] * WORLD["component_count"] * step * 0.5
    zones = {
        "JungleTop": ((-c, -c), (c, -c), (0.0, 0.0)),    # между TOP и MID
        "JungleBot": ((-c, -c), (-c, c), (0.0, 0.0)),    # между MID и BOT
    }

    def inside(p, tri):
        (x1, y1), (x2, y2), (x3, y3) = tri
        d = (y2 - y3) * (x1 - x3) + (x3 - x2) * (y1 - y3)
        a = ((y2 - y3) * (p[0] - x3) + (x3 - x2) * (p[1] - y3)) / d
        b = ((y3 - y1) * (p[0] - x3) + (x1 - x3) * (p[1] - y3)) / d
        return a >= 0 and b >= 0 and a + b <= 1

    slots = []
    for zname, tri in zones.items():
        placed = []
        for _ in range(20000):
            if len(placed) >= POI_PER_JUNGLE:
                break
            p = (rng.uniform(-c, c), rng.uniform(-c, c))
            if not inside(p, tri):
                continue
            ix = int(round((p[0] + half) / step))
            iy = int(round((p[1] + half) / step))
            if d_lanes[iy, ix] < POI_LANE_CLEAR or d_river[iy, ix] < POI_RIVER_CLEAR:
                continue
            if math.hypot(p[0] + c, p[1] + c) < BASE_R + POI_R + 3000.0:
                continue
            if any(math.hypot(p[0] - q[0], p[1] - q[1]) < POI_MIN_GAP for q in placed + [(s["x"], s["y"]) for s in slots]):
                continue
            # Не на площадке турели: две запиненные площадки разной высоты дают ступеньку.
            if any(math.hypot(p[0] - q["x"], p[1] - q["y"]) < POI_R + TURRET_PAD_R + 1500.0 for q in pads):
                continue
            placed.append(p)
        for i, p in enumerate(placed):
            slots.append({"name": "{}_{}".format(zname, i + 1), "zone": zname, "x": p[0], "y": p[1], "r": POI_R})
        log("{}: {} мест под кемпы".format(zname, len(placed)))
    return slots


# ==================== сборка поля ====================

def relax(z, pinned, step, iterations=1500):
    """Ограничитель уклона: каждое ребро не круче лимита, пиненное не трогаем.

    Границы от запиненных соседей главнее границ от свободных. Иначе ячейка между высоким и
    низким соседом не знает, куда ей деться: np.clip при нижней границе выше верхней берёт
    верхнюю, и откос у бока пандуса не насыпается; а если всегда брать нижнюю, то у площадки
    в низине не срезается бровка. Пин задаёт, где правда, свободный сосед подстраивается."""
    # 21 градус на ребро: зажим держит каждое ребро отдельно, а у треугольника ландшафта два
    # ребра, и уклон по обоим сразу складывается (при 25 на ребро выходило 33, в полосе П19).
    lim_o = math.tan(math.radians(MAX_SLOPE * 0.7)) * step
    lim_d = lim_o * math.sqrt(2.0)
    fixed = z.copy()
    pp = np.pad(pinned, 1, mode="edge")
    offs = [((0, -2, 1, -1), lim_o), ((2, None, 1, -1), lim_o), ((1, -1, 0, -2), lim_o), ((1, -1, 2, None), lim_o),
            ((0, -2, 0, -2), lim_d), ((0, -2, 2, None), lim_d), ((2, None, 0, -2), lim_d), ((2, None, 2, None), lim_d)]

    def sl(a, o):
        return a[o[0]:o[1], o[2]:o[3]]

    pin_nb = [(sl(pp, o), l) for o, l in offs]
    moved = 0.0
    inf = 1e9
    for i in range(iterations):
        a = np.pad(z, 1, mode="edge")
        nb = [(sl(a, o), l) for o, l in offs]
        lo_f = np.maximum.reduce([np.where(pm, -inf, v - l) for (v, l), (pm, _) in zip(nb, pin_nb)])
        hi_f = np.minimum.reduce([np.where(pm, inf, v + l) for (v, l), (pm, _) in zip(nb, pin_nb)])
        lo_p = np.maximum.reduce([np.where(pm, v - l, -inf) for (v, l), (pm, _) in zip(nb, pin_nb)])
        hi_p = np.minimum.reduce([np.where(pm, v + l, inf) for (v, l), (pm, _) in zip(nb, pin_nb)])
        free = np.where(lo_f > hi_f, (lo_f + hi_f) * 0.5, np.clip(z, lo_f, hi_f))
        cand = np.where(lo_p > hi_p, (lo_p + hi_p) * 0.5, np.clip(free, lo_p, hi_p))
        new_z = np.where(pinned, fixed, cand)
        moved = float(np.abs(new_z - z).max())
        z = new_z
        if moved < 0.5:
            log("зажим сошёлся за {} итераций".format(i + 1))
            return z
    log("зажим НЕ сошёлся, последний сдвиг {:.1f}".format(moved))
    return z


def disc(X, Y, x, y, r, band):
    """1 внутри r, плавно 0 к r + band."""
    return 1.0 - smoothstep((np.hypot(X - x, Y - y) - r) / band)


def high_ground(X, Y, z, pinned, full):
    """Базы на плато, как хайграунд в доте: обрыв по кругу, по пандусу на каждую линию.

    Обрыв это пин верха и пин низа в соседних вершинах: 9 м на метр, далеко за 50 градусов (П19).
    Пандус тоже пинится, по бокам от него зажим сам кладёт откос не круче 28 градусов."""
    c = CORNER
    z = z.copy()
    ramps_all = np.zeros(X.shape, dtype=bool)
    cliffs_all = np.zeros(X.shape, dtype=bool)
    for bx, by, end in ((-c, -c, 0), (c, c, -1)):
        d = np.hypot(X - bx, Y - by)
        near = d <= HG_R
        top = float(np.median(z[near])) + HG_H
        ramp_any = np.zeros(X.shape, dtype=bool)
        skirt_any = np.zeros(X.shape, dtype=bool)
        ramp_z = np.zeros(X.shape)
        for pts in full.values():
            # Направление линии от центра базы: первый отрезок у нашей базы, последний у вражеской.
            a, b = (pts[0], pts[1]) if end == 0 else (pts[-1], pts[-2])
            ux, uy = b[0] - a[0], b[1] - a[1]
            L = math.hypot(ux, uy)
            ux, uy = ux / L, uy / L
            s_along = (X - bx) * ux + (Y - by) * uy
            lat = np.abs((X - bx) * uy - (Y - by) * ux)
            fx, fy = bx + ux * (HG_R + RAMP_L), by + uy * (HG_R + RAMP_L)
            half = float(np.abs(X).max())
            step = WORLD["vertex_spacing_uu"]
            foot = float(z[int(round((fy + half) / step)), int(round((fx + half) / step))])
            mask = (lat <= RAMP_HALF) & (s_along > HG_R - 800.0) & (s_along <= HG_R + RAMP_L)
            t = np.clip((s_along - HG_R) / RAMP_L, 0.0, 1.0)
            ramp_z = np.where(mask, top + (foot - top) * t, ramp_z)
            ramp_any |= mask
            # Бока пандуса обрывом, как у пандусов хайграунда в доте: низ обрыва пинится на отметке
            # земли. Насыпь вместо обрыва упиралась в низ обрыва плато и давала полосу 30-50.
            # Обрыв по всей длине пандуса. У подножия он сходит на нет и там неизбежно проходит
            # через 30-50; эти треугольники закрывает стенка вдоль пандуса (build_lanes, ramp_walls).
            skirt_any |= (lat > RAMP_HALF) & (lat <= RAMP_HALF + 400.0) & (s_along > HG_R - 800.0) & (s_along <= HG_R + RAMP_L + 200.0)
        # Кольцо низа обрыва в 4 вершины: при 2 вершинах верх плато по диагонали касался свободной
        # ячейки снаружи, и зажим насыпал от него вал за обрывом.
        ring = (((d > HG_R) & (d <= HG_R + 400.0)) | (skirt_any & (d > HG_R))) & ~ramp_any
        z = np.where(near, top, z)
        z = np.where(ramp_any & ~near | (ramp_any & (d > HG_R - 800.0)), np.where(ramp_any, ramp_z, z), z)
        pinned = pinned | near | ramp_any | ring
        ramps_all |= ramp_any
        cliffs_all |= ring
        log("хайграунд {}: плато {:.0f} uu, на {:.0f} выше округи".format("наш" if end == 0 else "враг", top, HG_H))
    return z, pinned, ramps_all, cliffs_all


def build():
    X, Y, n, step, half = grid()
    c = CORNER
    full = lanes_full()

    d_lanes = np.full(X.shape, np.inf)
    for pts in full.values():
        d_lanes = np.minimum(d_lanes, dist_to_polyline(X, Y, pts))
    d_river = np.abs(X + Y) / math.sqrt(2.0)

    # 1. основа: крупный пологий шум, в лесу сильнее.
    lane_w = 1.0 - smoothstep((d_lanes - LANE_W * 0.5) / 4000.0)
    base_w = np.maximum(disc(X, Y, -c, -c, BASE_R, 5000.0), disc(X, Y, c, c, BASE_R, 5000.0))
    calm = np.maximum(lane_w, base_w)
    amp = FOREST_AMP * (1.0 - calm) + GROUND_AMP * calm
    z = amp * (0.7 * value_noise(X, Y, 9000.0, SEED) + 0.3 * value_noise(X, Y, 3500.0, SEED + 1))

    # 2. кромка: подъём к краям карты.
    edge = np.minimum.reduce([half - X, half + X, half - Y, half + Y])
    z = z + EDGE_RISE * smoothstep(1.0 - edge / EDGE_BAND)

    # 3. река: русло с ровным дном, берега пологие.
    river_w = 1.0 - smoothstep((d_river - RIVER_W * 0.5) / RIVER_BANK)
    z = z * (1.0 - river_w) + (-RIVER_DEPTH) * river_w

    # 4. линии: полотно на гладкой отметке (шум крупной ячейки, без мелкого), берег плавный.
    road_z = GROUND_AMP * 0.7 * value_noise(X, Y, 9000.0, SEED)
    # Брод: полотно плавно спускается к воде, а не ступенькой (обе стороны ступеньки запинены).
    ford = 1.0 - smoothstep((d_river - RIVER_W * 0.5) / 3000.0)
    road_z = road_z * (1.0 - ford) + (-RIVER_DEPTH) * ford
    road_floor = d_lanes <= LANE_W * 0.5
    road_w = 1.0 - smoothstep((d_lanes - LANE_W * 0.5) / 1500.0)
    z = z * (1.0 - road_w) + road_z * road_w

    # 5. площадки: базы, турели, кемпы. Плоские диски, пишутся последними (П28).
    pads = turret_pads(full)
    slots = jungle_slots((d_lanes, d_river), pads)
    pinned = road_floor.copy()
    z, pinned, ramps, cliffs = high_ground(X, Y, z, pinned, full)
    flats = []
    # Площадка турели на отметке полотна рядом с ней: между двумя пинами разной высоты зажиму
    # нечем сгладить 4 м зазора, выходит ступенька.
    flats += [(p["x"], p["y"], TURRET_PAD_R, 700.0, p.get("axis")) for p in pads]
    flats += [(s["x"], s["y"], s["r"], 1000.0, None) for s in slots]
    flats = [f if len(f) == 5 else f + (None,) for f in flats]
    for (fx, fy, fr, fb, level_at) in flats:
        hx, hy = level_at if level_at else (fx, fy)
        ix = int(round((hx + half) / step))
        iy = int(round((hy + half) / step))
        h = float(z[iy, ix])
        # Полоса сглаживания соседней площадки не лезет в уже запиненную (кемп рядом с турелью).
        w = np.where(pinned, 0.0, disc(X, Y, fx, fy, fr, fb))
        z = z * (1.0 - w) + h * w
        pinned |= np.hypot(X - fx, Y - fy) <= fr

    z = relax(z, pinned, step)

    lanes = lane_splines(full)
    return {"X": X, "Y": Y, "z": z, "full": full, "lanes": lanes, "pads": pads, "slots": slots,
            "d_lanes": d_lanes, "d_river": d_river, "road_floor": road_floor, "half": half, "step": step,
            "ramps": ramps, "pinned": pinned, "cliffs": cliffs}


# ==================== проверки ====================

def z_at(m, x, y):
    ix = int(round((x + m["half"]) / m["step"]))
    iy = int(round((y + m["half"]) / m["step"]))
    return float(m["z"][iy, ix])


def analyse(m):
    z, step = m["z"], m["step"]
    gy, gx = np.gradient(z, step)
    slope = np.degrees(np.arctan(np.hypot(gx, gy)))
    play = np.minimum.reduce([m["half"] - np.abs(m["X"]), m["half"] - np.abs(m["Y"])]) > EDGE_BAND
    # П19 про треугольники, которые видит навмеш, а не про центральную разность через две вершины:
    # та на краю обрыва плато (9 м на одну вершину) даёт 30-50 там, где треугольник стоит на 84.
    t1 = np.degrees(np.arctan(np.hypot(z[1:, :-1] - z[:-1, :-1], z[:-1, 1:] - z[:-1, :-1]) / step))
    t2 = np.degrees(np.arctan(np.hypot(z[1:, 1:] - z[:-1, 1:], z[1:, 1:] - z[1:, :-1]) / step))
    pq = play[:-1, :-1]
    band_t = (((t1 > BAND[0]) & (t1 < BAND[1])) | ((t2 > BAND[0]) & (t2 < BAND[1]))) & pq
    # Треугольник, касающийся низа обрыва хайграунда, это рукотворный обрыв: там, где он сходит
    # на нет у подножия пандуса, полосу закрывает стенка вдоль пандуса. Считаются отдельно.
    cl = m["cliffs"]
    at_cliff = cl[:-1, :-1] | cl[1:, :-1] | cl[:-1, 1:] | cl[1:, 1:]
    log("уклоны: у обрывов хайграунда 30-50 треугольников {} (закрываются стенками пандусов)".format(int((band_t & at_cliff).sum())))
    band = band_t & ~at_cliff
    ok = True
    frac = float(band.sum()) / float(pq.sum())
    log("уклоны: доля 30-50 в игровой зоне {:.5f} (порог 0) {}".format(frac, "OK" if frac == 0 else "FAIL"))
    ok &= frac == 0.0
    # Пандусы хайграунда круче 10 намеренно (14): их из проверки полотна исключаем.
    flat_road = m["road_floor"] & ~m["ramps"]
    steep_road = float((slope[flat_road] > 10.0).mean())
    log("полотно линий круче 10: {:.4f} (порог 0.01) {}".format(steep_road, "OK" if steep_road < 0.01 else "FAIL"))
    ok &= steep_road < 0.01
    for p in m["pads"]:
        # Уклон по центральным разностям у края диска берёт соседа снаружи: меряем на шаг внутрь.
        r = np.hypot(m["X"] - p["x"], m["Y"] - p["y"]) <= TURRET_PAD_R - 1.5 * step
        s = float(slope[r].max())
        if s > 5.0:
            log("площадка {} уклон {:.1f} > 5 FAIL".format(p["name"], s))
            ok = False
    for s_ in m["slots"]:
        r = np.hypot(m["X"] - s_["x"], m["Y"] - s_["y"]) <= s_["r"] - 1.5 * step
        s = float(slope[r].max())
        if s > 5.0:
            log("кемп {} уклон {:.1f} > 5 FAIL".format(s_["name"], s))
            ok = False
    for name, ln in m["lanes"].items():
        log("линия {}: наша половина {:.0f} м ({:.0f} с спринтом), вся линия {:.0f} м".format(
            name, ln["length"] / 100.0, ln["length"] / 760.0, ln["total_full"] / 100.0))
    log("z: {:.0f} .. {:.0f} uu".format(float(z.min()), float(z.max())))
    return ok


def preview(m, path, scale=1):
    """Картинка сверху: высота серым, линии, река, базы, турели, кемпы."""
    z = m["z"]
    lo, hi = float(z.min()), float(z.max())
    g = ((z - lo) / max(hi - lo, 1.0) * 150.0 + 60.0)
    img = np.stack([g, g, g], axis=-1)

    def paint(mask, color, a=1.0):
        for i in range(3):
            img[..., i] = np.where(mask, img[..., i] * (1 - a) + color[i] * a, img[..., i])

    paint(m["d_river"] <= RIVER_W * 0.5, (60, 110, 200), 0.8)
    paint(m["road_floor"], (190, 150, 90), 0.9)
    X, Y = m["X"], m["Y"]
    c = CORNER
    paint(np.hypot(X + c, Y + c) <= BASE_R, (60, 170, 80), 0.7)
    paint(np.hypot(X - c, Y - c) <= BASE_R, (200, 60, 60), 0.7)
    for p in m["pads"]:
        paint(np.hypot(X - p["x"], Y - p["y"]) <= 700.0, (255, 230, 40))
    for s in m["slots"]:
        ring = np.abs(np.hypot(X - s["x"], Y - s["y"]) - s["r"]) <= 150.0
        paint(ring, (40, 220, 220))
    for ln in m["lanes"].values():
        for (px, py) in ln["points"]:
            paint(np.hypot(X - px, Y - py) <= 350.0, (255, 80, 200))
    # Картинка: север (+X) вверх, восток (+Y) вправо: строки по X сверху вниз убывают.
    img = np.transpose(img, (1, 0, 2))[::-1, :, :]
    img = img[::scale, ::scale].clip(0, 255).astype(np.uint8)
    bench_png(img, path)


def bench_png(img, path):
    import struct
    import zlib
    h, w, _ = img.shape
    raw = b"".join(b"\x00" + img[y].tobytes() for y in range(h))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    with open(path, "wb") as fh:
        fh.write(png)
    log("WROTE: {}".format(path))


def ramp_layout(full):
    """Оси пандусов хайграунда: от кромки плато до подножия, для стенок вдоль них."""
    out = []
    c = CORNER
    for bx, by, end, base in ((-c, -c, 0, "Base"), (c, c, -1, "Enemy")):
        for name, pts in full.items():
            a, b = (pts[0], pts[1]) if end == 0 else (pts[-1], pts[-2])
            ux, uy = b[0] - a[0], b[1] - a[1]
            L = math.hypot(ux, uy)
            ux, uy = ux / L, uy / L
            out.append({"base": base, "lane": name, "ux": ux, "uy": uy,
                        "x0": bx + ux * HG_R, "y0": by + uy * HG_R,
                        "x1": bx + ux * (HG_R + RAMP_L), "y1": by + uy * (HG_R + RAMP_L)})
    return out


def main():
    m = build()
    ok = analyse(m)
    preview(m, os.path.join(HERE, "preview.png"))
    if not ok:
        log("проверки НЕ прошли: heightmap и layout не выкладываю")
        return False
    bench.export_png({"world": WORLD}, m["z"], os.path.join(HERE, "heightmap.png"))
    layout = {
        "world": WORLD, "half_extent": m["half"], "corner": CORNER, "base_r": BASE_R,
        "base_z": z_at(m, -CORNER, -CORNER), "enemy_base_z": z_at(m, CORNER, CORNER),
        "lanes": {k: {"points": [[p[0], p[1], z_at(m, p[0], p[1])] for p in v["points"]],
                      "length": v["length"]} for k, v in m["lanes"].items()},
        "lanes_full": {k: [[p[0], p[1], z_at(m, p[0], p[1])] for p in v] for k, v in m["full"].items()},
        "turret_pads": [dict({k: v for k, v in p.items() if k != "axis"}, z=z_at(m, p["x"], p["y"])) for p in m["pads"]],
        "poi_slots": [dict(s, z=z_at(m, s["x"], s["y"])) for s in m["slots"]],
        "lane_w": LANE_W, "river_w": RIVER_W,
        "hg": {"r": HG_R, "h": HG_H, "ramp_l": RAMP_L, "ramp_half": RAMP_HALF},
        "ramps": ramp_layout(m["full"]),
    }
    with open(os.path.join(HERE, "layout.json"), "w", encoding="utf-8") as fh:
        json.dump(layout, fh, ensure_ascii=False, indent=1)
    log("WROTE: layout.json")
    return True


if __name__ == "__main__":
    sys.exit(0 if main() else 1)
