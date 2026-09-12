"""Рельеф карты стенда: поле высот считается из map_spec.json и заводится в ландшафт.

Идея рельефа: **всё, что не площадка и не лейн, поднято**. Карта это сеть коридоров в высокой
местности, а не поле бугров. Ширина лейна и есть разница между горлом и открыткой: у узкого стены
рядом, у широкого далеко. Отдельного параметра «сделай тут чоукпоинт» нет намеренно, иначе он
разъедется со шириной.

Стена это НЕ обрыв. Обрыв круче 50 градусов на сетке 100 uu неизбежно даёт по плечам тонкую полосу
30-50, а она запрещена П19 и рвёт навмеш на острова. Стена здесь это плато на 12 метров с подъёмом
под 23 градуса: перелезть можно, но медленно и на виду, а навмеш остаётся сплошным (П32-П34).

Порядок наложения, каждый следующий шаг перекрывает предыдущий:
    1. база      мягкая интерполяция отметок площадок по всей карте (IDW)
    2. пол лейна коридор идёт ровным уклоном от отметки одного конца к отметке другого
    3. стены     подъём везде, где нет ни лейна, ни площадки
    4. шум       длинные волны, погашенные в лейнах и на площадках
    5. площадки  принудительное выравнивание, пишется последним и всегда выигрывает
    6. зажим     ограничитель уклона, гарантирует П19

Скульпт-вызовы движка тут не используются: они аддитивны, второй прогон удвоил бы горы. Высота
каждой вершины это чистая функция от спеки, поэтому пересборка идемпотентна.

Запуск:
    вне редактора:      python terrain.py          посчитать, проверить, выложить heightmap.png
    внутри редактора:   import terrain; terrain.build()

Правила, на которые опирается каждое число: Docs/LevelDesign_Rules_Map_2026-09-01.md
"""

import json
import math
import os

try:
    import numpy as np
except ImportError:  # pragma: no cover
    np = None

SPEC_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "map_spec.json")


def log(msg):
    print("[TERRAIN] {}".format(msg))


def load_spec(path=SPEC_PATH):
    with open(path, "r", encoding="utf-8") as fh:
        return json.load(fh)


# ==================== сетка ====================

def grid_size(spec):
    w = spec["world"]
    return w["quads_per_section"] * w["sections_per_component"] * w["component_count"] + 1


def half_extent(spec):
    return (grid_size(spec) - 1) * spec["world"]["vertex_spacing_uu"] * 0.5


def axis(spec):
    n = grid_size(spec)
    step = spec["world"]["vertex_spacing_uu"]
    lo = -half_extent(spec)
    return np.array([lo + i * step for i in range(n)], dtype=np.float64)


def pad_by_id(spec):
    return {p["id"]: p for p in spec["pads"]}


def is_poi(pad):
    return pad.get("is_poi", True)


def _smooth_falloff(d, inner, rise):
    """1 внутри inner, 0 за inner+rise, гладко между. Ноль производной на обоих концах."""
    t = np.clip((d - inner) / max(rise, 1.0), 0.0, 1.0)
    return 1.0 - (t * t * (3.0 - 2.0 * t))


def _seg_project(X, Y, x0, y0, x1, y1):
    """Параметр вдоль отрезка (0..1) и расстояние до него."""
    vx, vy = x1 - x0, y1 - y0
    seg2 = max(vx * vx + vy * vy, 1e-9)
    t = np.clip(((X - x0) * vx + (Y - y0) * vy) / seg2, 0.0, 1.0)
    d = np.hypot(X - (x0 + t * vx), Y - (y0 + t * vy))
    return t, d


def lane_floor(spec, X, Y, ln):
    """Пол коридора: (t вдоль оси, расстояние до оси, высота пола с перегибом).

    Одна функция на два вызова, потому что дно коридора теперь пинится ограничителем, и если
    поле высот и пин посчитают его по-разному, ограничитель будет вечно воевать сам с собой.

    Перегиб задан в МЕТРАХ вдоль оси, а не в долях длины: доля на коротком лейне даёт короткий
    склон, и перегиб в 700 uu на длине 1700 это 32 градуса, то есть запретная полоса П19 прямо
    посреди коридора, да ещё и запиненная, так что ограничитель её не вылечит."""
    pads = pad_by_id(spec)
    pa, pb = pads[ln["from"]], pads[ln["to"]]
    (x0, y0), (x1, y1) = pa["xy"], pb["xy"]
    length = math.hypot(x1 - x0, y1 - y0)
    t, d = _seg_project(X, Y, x0, y0, x1, y1)

    z = pa["pad_z"] + (pb["pad_z"] - pa["pad_z"]) * t
    h = ln.get("crest", spec["walls"].get("crest", 700.0))
    if h > 0.0:
        span_uu = ln.get("crest_span_uu", spec["walls"].get("crest_span_uu", 2800.0))
        span_t = min(0.45, max(span_uu, 1.0) / max(length, 1.0))
        u = np.clip(np.abs(t - 0.5) / span_t, 0.0, 1.0)
        z = z + h * (np.cos(u * math.pi * 0.5) ** 2)
    return t, d, z


# ==================== поле высот ====================

def height_field(spec):
    """Поле высот. Строится сразу легальным, а не «поднять, потом зажать».

    Предыдущая версия поднимала всю карту и звала итеративный ограничитель уклона. Он не мог
    сойтись: коридоры и площадки закреплены, и там, где две закреплённые высоты оказывались ближе,
    чем нужно на легальный склон, получался бутерброд, который нечем разрулить. На замере это
    выглядело как дно коридора ВЫШЕ соседней земли, то есть «тупо бугры» вместо карты.

    Теперь так: считается расстояние до КРАЯ сети (коридоры плюс площадки), и земля поднимается
    от этого края ровно на разрешённый угол, пока не упрётся в потолок стены. Склон легален
    по построению, ущелье получается самое глубокое, какое разрешает П19, и никаких итераций.
    Ограничитель остаётся, но теперь он подчищает стыки, а не держит всю конструкцию."""
    if np is None:
        raise RuntimeError("нужен numpy")

    pads = spec["pads"]
    walls = spec["walls"]
    wall_h = walls["height"]
    grade = math.tan(math.radians(walls.get("wall_slope_deg", 25.0)))

    a = axis(spec)
    X, Y = np.meshgrid(a, a)

    # Элементы сети: площадки и коридоры. У каждого свой пол и свой радиус.
    elems = []
    for p in pads:
        px, py = p["xy"]
        d = np.hypot(X - px, Y - py)
        elems.append((np.maximum(d - p["flat"], 0.0), np.full_like(X, float(p["pad_z"])),
                      d <= p["flat"], float(p["pad_z"])))
    for ln in spec["lanes"]:
        _, d, lane_z = lane_floor(spec, X, Y, ln)
        hw = ln["width"] * 0.5
        elems.append((np.maximum(d - hw, 0.0), lane_z, d <= hw, None))

    # Расстояние до края сети и её пол, размазанный наружу. Вес обратно-квадратичный, поэтому
    # рядом с элементом его пол и решает, а вдали высоты соседних элементов сходятся плавно:
    # без этого на границе между зонами влияния стоял бы обрыв в разницу их отметок.
    dist = np.full_like(X, 1e9)
    w_sum = np.zeros_like(X)
    z_sum = np.zeros_like(X)
    for d_edge, floor_z, _, _ in elems:
        dist = np.minimum(dist, d_edge)
        w = 1.0 / (d_edge + 400.0) ** 2
        w_sum += w
        z_sum += w * floor_z
    floor = z_sum / w_sum

    z = floor + np.minimum(wall_h, dist * grade)

    # Пол коридоров ставится точно: размазанный пол уводит дно, а по коридору бегают.
    # Но на ПЕРЕСЕЧЕНИИ двух коридоров с разными отметками правило «кто последний, тот и прав»
    # ставит обрыв в 700 uu на одну вершину, то есть 75 градусов. Поэтому пересечения смешиваются
    # весом, падающим к краю коридора: вдоль одиночного лейна вес один и пол точный, а в
    # перекрестье высоты сходятся плавно.
    lw_sum = np.zeros_like(X)
    lz_sum = np.zeros_like(X)
    lane_any = np.zeros_like(X, dtype=bool)
    for ln in spec["lanes"]:
        _, d, lane_z = lane_floor(spec, X, Y, ln)
        hw = ln["width"] * 0.5
        inside = d <= hw
        w = np.where(inside, np.clip(1.0 - (d / hw) ** 2, 1e-3, 1.0), 0.0)
        lw_sum += w
        lz_sum += w * lane_z
        lane_any |= inside
    z = np.where(lane_any, lz_sum / np.maximum(lw_sum, 1e-9), z)

    # Площадки поверх всего: точка обязана быть плоской, коридор её не продавливает (П28).
    for p in pads:
        px, py = p["xy"]
        z = np.where(np.hypot(X - px, Y - py) <= p["flat"], float(p["pad_z"]), z)

    # Шум только в высокой местности: рябь в коридоре ломает читаемость коридора. low это
    # «насколько мы в сети»: 1 внутри коридора или площадки, 0 на полной высоте стены.
    low = np.clip(1.0 - dist * grade / max(wall_h, 1.0), 0.0, 1.0)
    n = spec.get("noise", {})
    if n.get("waves"):
        # Каждая волна под своим углом. Первая версия перемножала sin(X) на cos(Y) с одним и тем же
        # периодом по обеим осям, и это давало не шум, а правильную диагональную решётку: на карте
        # сверху вся высокая земля читалась вельветом в рубчик.
        ph = _phases(n.get("seed", 0), len(n["waves"]) * 2)
        bump = np.zeros_like(X)
        for k, wave in enumerate(n["waves"]):
            k0 = 2.0 * math.pi / wave["wavelength"]
            ang = ph[k * 2]
            ca, sa = math.cos(ang), math.sin(ang)
            u = X * ca + Y * sa
            v = -X * sa + Y * ca
            bump += wave["amplitude"] * np.sin(u * k0 + ph[k * 2 + 1]) \
                * np.sin(v * k0 * 0.61 + ph[k * 2] * 1.7)
        z = z + bump * (1.0 - low)

    # 5 и 6
    z = stamp_pads(spec, z, X, Y)
    z = relax_slopes(spec, z, X, Y)
    return z


def _phases(seed, count):
    """Детерминированные фазы без зависимости от состояния random."""
    out = []
    s = seed & 0xFFFFFFFF
    for _ in range(count):
        s = (1103515245 * s + 12345) & 0x7FFFFFFF
        out.append((s / 0x7FFFFFFF) * 2.0 * math.pi)
    return out


def stamp_pads(spec, z, X, Y):
    """Принудительное выравнивание под площадками, П28."""
    for p in spec["pads"]:
        px, py = p["xy"]
        d = np.hypot(X - px, Y - py)
        t = np.clip((d - p["flat"]) / max(p["blend"], 1.0), 0.0, 1.0)
        w = 1.0 - (t * t * (3.0 - 2.0 * t))
        z = z * (1.0 - w) + p["pad_z"] * w
    return z


def pad_core_mask(spec, X, Y):
    """Что ограничителю запрещено двигать: ядра площадок И дно коридоров.

    Полосу сглаживания площадки НЕ пиним: первая версия пинила её вместе с площадкой и не сходилась
    никогда, потому что сглаживание тянуло склон вниз, а следующая итерация впечатывала полосу
    обратно во всей крутизне.

    Дно коридоров пинить ОБЯЗАТЕЛЬНО, и это оплачено заходом. Ограничитель зажимает перепад в обе
    стороны, то есть коридор, который на 12 метров ниже стены, он видит ямой и поднимает. За два
    десятка итераций вся сеть коридоров засыпалась, и на карте оставалась рябь в 200-300 uu:
    замер поперёк «горла» показывал дно ВЫШЕ соседней земли. С пином дно неподвижно, и склон
    вырастает от края коридора вверх ровно на максимально допустимый угол, то есть ущелье
    получается самое глубокое, какое разрешает П19."""
    mask = np.zeros_like(X, dtype=bool)
    core = np.zeros_like(X)

    for p in spec["pads"]:
        px, py = p["xy"]
        inside = np.hypot(X - px, Y - py) <= p["flat"]
        mask |= inside
        core = np.where(inside, p["pad_z"], core)

    # Коридоры пересекаются, и на пересечении их полы стоят на РАЗНОЙ высоте: перегиб одного
    # приходится на нормальную отметку другого. Правило «кто первый, тот и прав» ставило там
    # запиненный обрыв в 700 uu между соседними вершинами, то есть 79 градусов, и ограничитель
    # не мог его вылечить, потому что обе стороны запинены. Поэтому дно на пересечении это
    # взвешенное среднее: вес падает к нулю у края коридора, так что вдоль одиночного лейна
    # ничего не меняется, а в перекрестье высоты сходятся плавно.
    # Коридоры НЕ пиним. Пробовали: тогда там, где две закреплённые высоты оказываются ближе,
    # чем нужно на легальный склон (коридор впритык к чужой площадке, пересечение двух коридоров
    # с разными отметками), получается бутерброд, который ограничителю нечем разрулить, и он
    # «сходится» с максимумом в 75 градусов. Держать коридоры теперь не его работа: поле строится
    # легальным по построению, земля поднимается от края сети ровно на разрешённый угол.
    # Ограничитель остался подчищать стыки, и засыпать коридор ему уже нечем: рядом с коридором
    # склон и так пологий.
    return mask, core


def relax_slopes(spec, z, X, Y, iterations=400):
    """Зажимает перепад между соседями, пока уклон везде не уйдёт под порог.

    Не размытие, а прямое ограничение: вершина выше соседа больше допустимого опускается, ниже
    поднимается. Ядра площадок неподвижны, поэтому склон вокруг них вырастает сам и ровно такой
    пологий, какой разрешён."""
    cap = spec["checks"]["max_slope_deg"]
    step = spec["world"]["vertex_spacing_uu"]
    # Запас на то, что проверка меряет уклон центральной разностью через две вершины, а зажим
    # работает по одной паре соседей: на границе ядра площадки это расходится примерно на градус.
    lim_o = math.tan(math.radians(cap * 0.85)) * step
    lim_d = lim_o * math.sqrt(2.0)
    mask, core = pad_core_mask(spec, X, Y)

    for i in range(iterations):
        a = np.pad(z, 1, mode="edge")
        nb = [(a[:-2, 1:-1], lim_o), (a[2:, 1:-1], lim_o),
              (a[1:-1, :-2], lim_o), (a[1:-1, 2:], lim_o),
              (a[:-2, :-2], lim_d), (a[:-2, 2:], lim_d),
              (a[2:, :-2], lim_d), (a[2:, 2:], lim_d)]
        upper = np.minimum.reduce([v + l for v, l in nb])
        lower = np.maximum.reduce([v - l for v, l in nb])
        new_z = np.where(mask, core, np.clip(z, lower, upper))
        moved = float(np.abs(new_z - z).max())
        z = new_z
        if moved < 0.5:
            gy, gx = np.gradient(z, step)
            log("зажим уклона: сошлось за {} итераций, максимум {:.1f}".format(
                i + 1, np.degrees(np.arctan(np.hypot(gx, gy))).max()))
            return z
    log("зажим уклона: НЕ сошёлся за {} итераций".format(iterations))
    return z


# ==================== проверки (П60) ====================

def check_lane_graph(spec, report):
    """П13 и П34: у каждой боевой точки есть и горло, и открытка, и сеть связна одним куском."""
    by_id = pad_by_id(spec)
    kinds = {p["id"]: {"choke": 0, "open": 0} for p in spec["pads"]}
    parent = {p["id"]: p["id"] for p in spec["pads"]}

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    for ln in spec["lanes"]:
        for end in (ln["from"], ln["to"]):
            if end not in kinds:
                report["fail"].append("лейн ссылается на несуществующую площадку {}".format(end))
                return
            kinds[end][ln["kind"]] += 1
        ra, rb = find(ln["from"]), find(ln["to"])
        if ra != rb:
            parent[ra] = rb

    for p in spec["pads"]:
        k = kinds[p["id"]]
        if not is_poi(p):
            report["info"].append("{}: не боевая точка, П13 не применяется ({} лейнов)".format(
                p["id"], k["choke"] + k["open"]))
            continue
        if k["choke"] == 0 or k["open"] == 0:
            report["fail"].append(
                "{}: подходов горло {} / открытка {}, нужен хотя бы один каждого, П13".format(
                    p["id"], k["choke"], k["open"]))
        else:
            report["info"].append("{}: горл {}, открыток {}, П13 ок".format(
                p["id"], k["choke"], k["open"]))

    roots = {find(p["id"]) for p in spec["pads"]}
    if len(roots) == 1:
        report["info"].append("сеть лейнов связна одним куском, П34 ок")
    else:
        report["fail"].append("сеть лейнов распалась на {} кусков, П34".format(len(roots)))


def analyse(spec, z):
    step = spec["world"]["vertex_spacing_uu"]
    chk = spec["checks"]
    report = {"fail": [], "warn": [], "info": []}

    check_lane_graph(spec, report)

    gy, gx = np.gradient(z, step)
    slope = np.degrees(np.arctan(np.hypot(gx, gy)))

    lo, hi = chk["forbidden_slope_band_deg"]
    band = int(np.count_nonzero((slope >= lo) & (slope <= hi)))
    report["info"].append("уклон: максимум {:.1f}, средний {:.1f}".format(slope.max(), slope.mean()))
    if band:
        report["fail"].append("запретная полоса {:.0f}-{:.0f}: {} вершин ({:.3f}%), П19".format(
            lo, hi, band, 100.0 * band / slope.size))
    else:
        report["info"].append("запретная полоса {:.0f}-{:.0f}: пусто, П19 ок".format(lo, hi))

    over = int(np.count_nonzero(slope > chk["max_slope_deg"]))
    if over:
        report["fail"].append("уклон выше {:.0f}: {} вершин, П19".format(chk["max_slope_deg"], over))

    n = grid_size(spec)
    lo_w = -half_extent(spec)
    for p in spec["pads"]:
        px, py = p["xy"]
        ix = int(round((px - lo_w) / step))
        iy = int(round((py - lo_w) / step))
        rad = int(p["flat"] / step)
        a0 = max(0, iy - rad); b0 = min(n, iy + rad + 1)
        c0 = max(0, ix - rad); d0 = min(n, ix + rad + 1)
        ys_i = np.arange(a0, b0)[:, None]
        xs_i = np.arange(c0, d0)[None, :]
        # Минус две вершины от края: np.gradient в граничной клетке считает разность по соседу
        # СНАРУЖИ площадки, поэтому кольцо по краю всегда показывает уклон склона, а не площадки.
        # Первая версия проверки ловила на этом по 50 градусов там, где ядро идеально плоское.
        inner = max(p["flat"] - 2.0 * step, step)
        inside = np.hypot((xs_i - ix) * step, (ys_i - iy) * step) <= inner
        s_max = float(slope[a0:b0, c0:d0][inside].max())
        zz = z[a0:b0, c0:d0][inside]
        if s_max > chk["pad_max_slope_deg"]:
            report["fail"].append("{}: уклон на площадке {:.1f} > {:.0f}, П28".format(
                p["id"], s_max, chk["pad_max_slope_deg"]))
        else:
            report["info"].append("{}: площадка ровная, уклон {:.2f}, разброс {:.0f} uu".format(
                p["id"], s_max, float(zz.max() - zz.min())))

    smin, smax = chk["poi_spacing_uu"]
    pads = spec["pads"]
    closest = None
    for i in range(len(pads)):
        for j in range(i + 1, len(pads)):
            ax, ay = pads[i]["xy"]; bx, by = pads[j]["xy"]
            dd = math.hypot(bx - ax, by - ay)
            if closest is None or dd < closest[0]:
                closest = (dd, pads[i]["id"], pads[j]["id"])
            if dd < smin and is_poi(pads[i]) and is_poi(pads[j]):
                report["fail"].append("{} и {} на {:.0f} uu, ближе минимума {:.0f}, П2".format(
                    pads[i]["id"], pads[j]["id"], dd, smin))
    if closest:
        report["info"].append("ближайшая пара: {} и {}, {:.0f} uu".format(
            closest[1], closest[2], closest[0]))

    eye = chk["eye_height_uu"]
    blocked = clear = 0
    for i in range(len(pads)):
        for j in range(i + 1, len(pads)):
            a1, b1 = pads[i], pads[j]
            ax, ay = a1["xy"]; bx, by = b1["xy"]
            dd = math.hypot(bx - ax, by - ay)
            if dd >= chk["sightline_break_below_uu"]:
                continue
            if _line_blocked(spec, z, a1, b1, eye):
                blocked += 1
            else:
                clear += 1
                report["fail"].append(
                    "{} видит {} насквозь ({:.0f} uu), рельеф не срубил прострел, П25".format(
                        a1["id"], b1["id"], dd))
    report["info"].append("прострелы точка-точка: срублено {}, открыто {}".format(blocked, clear))

    ex = spec.get("extractions", [])
    gaps = []
    for i in range(len(ex)):
        for j in range(i + 1, len(ex)):
            ax, ay = ex[i]["xy"]; bx, by = ex[j]["xy"]
            gaps.append(math.hypot(bx - ax, by - ay))
    need = chk["extraction_min_gap_uu"]
    if len(ex) >= 3 and any(g >= need for g in gaps):
        report["info"].append("эвакуации: максимум {:.0f} uu между парой, П52 ок".format(max(gaps)))
    elif ex:
        report["fail"].append("эвакуации ближе {:.0f} uu друг к другу, П52".format(need))

    report["info"].append("высоты: от {:.0f} до {:.0f} uu, размах {:.0f}".format(
        z.min(), z.max(), z.max() - z.min()))
    return report


def _line_blocked(spec, z, a, b, eye):
    step = spec["world"]["vertex_spacing_uu"]
    lo = -half_extent(spec)
    n = grid_size(spec)
    ax, ay = a["xy"]; bx, by = b["xy"]
    az = a["pad_z"] + eye
    bz = b["pad_z"] + eye
    samples = 240
    for s in range(1, samples):
        t = s / float(samples)
        x = ax + (bx - ax) * t
        y = ay + (by - ay) * t
        ix = int(round((x - lo) / step)); iy = int(round((y - lo) / step))
        if 0 <= ix < n and 0 <= iy < n and z[iy, ix] > az + (bz - az) * t:
            return True
    return False


def print_report(report):
    for line in report["info"]:
        log("  " + line)
    for line in report["warn"]:
        log("ВНИМАНИЕ: " + line)
    for line in report["fail"]:
        log("ПРОВАЛ: " + line)
    log("итог: {} провалов, {} предупреждений".format(len(report["fail"]), len(report["warn"])))
    return not report["fail"]


# ==================== вывод ====================

def export_png(spec, z, path):
    """16-битный серый PNG, который понимает import_heightmap.

    Через файл, а не через set_height_in_region, по прозаической причине: в питоне редактора нет
    numpy, а считать 398 тысяч вершин с зажимом уклона на чистом питоне внутри редактора это минуты.

    Кодировка высоты в UE: raw = 32768 + z * 128 / ZScale. При ZScale 100 это -25600..+25600 uu
    с шагом 0.78 uu."""
    import struct
    import zlib

    zs = spec["world"]["z_scale"]
    raw = np.clip(np.round(32768.0 + z * 128.0 / zs), 0, 65535).astype(">u2")
    h, w = raw.shape

    rows = bytearray()
    for y in range(h):
        rows.append(0)
        rows += raw[y].tobytes()

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 16, 0, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(rows), 6))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as fh:
        fh.write(png)
    log("WROTE: {} ({}x{}, 16 бит)".format(path, w, h))
    return path


def export(spec=None, path=None):
    spec = spec or load_spec()
    z = height_field(spec)
    ok = print_report(analyse(spec, z))
    path = path or os.path.join(os.path.dirname(SPEC_PATH), "heightmap.png")
    export_png(spec, z, path)
    return ok, path


def build(spec=None):
    """Внутри редактора: создать ландшафт, если его нет, и импортировать готовый PNG."""
    import unreal

    spec = spec or load_spec()
    w = spec["world"]
    label = w["landscape_label"]
    png = os.path.join(os.path.dirname(SPEC_PATH), "heightmap.png")

    existing = [l.actor_label for l in (unreal.LandscapeService.list_landscapes() or [])]
    if label not in existing:
        half = half_extent(spec)
        unreal.LandscapeService.create_landscape(
            unreal.Vector(-half, -half, 0.0), unreal.Rotator(0, 0, 0),
            unreal.Vector(w["vertex_spacing_uu"], w["vertex_spacing_uu"], w["z_scale"]),
            sections_per_component=w["sections_per_component"],
            quads_per_section=w["quads_per_section"],
            component_count_x=w["component_count"],
            component_count_y=w["component_count"],
            landscape_label=label)
        log("CREATED: ландшафт {}".format(label))

    res = unreal.LandscapeService.import_heightmap(label, png)
    unreal.LandscapeService.set_landscape_collision(label, True)
    log("MODIFIED: импортирован {}".format(getattr(res, "resolution", "?")))
    return res


def main():
    ok, _ = export()
    return ok


if __name__ == "__main__":
    import sys
    sys.exit(0 if main() else 1)
