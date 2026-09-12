"""Скелет боевой карты: слоты один раз, роли каждый забег.

Что это решает. Карта одна, а забеги должны отличаться. Значит место на карте не может само по себе
означать "штаб" или "миссия": роли раскладываются заново каждый раз, а карта обязана быть пулом
слотов, годных под несколько ролей сразу.

Схема кольцевая, а не полосовая, и это не вкусовщина. У полос (Край Света: запад-восток) есть
подразумеваемая ось. Как только роли переезжают, ось перестаёт что-либо значить и полосы
вырождаются в произвольные ряды. Кольца (Олимп, E-District) не подразумевают ролей вообще, они
говорят только "ближе к середине" и "дальше от середины" - и это верно при любой раскладке.

Форма правил взята у Hunt: Showdown, где эта механика отгружена и отлажена: логово босса выпадает
в один из шестнадцати компаундов, три эвакуации тянутся из пула, и игра гарантирует минимум 500 м
между двумя из трёх.

    python layout.py                 -> layout_preview.png, четыре забега на одном скелете
    python layout.py --seed 7        -> один забег, крупно
    python layout.py --seeds 1,2,3,4 -> свои четыре

Обоснование целиком: Docs/Map_Prod_Layout_Method_2026-09-06.md
"""

import argparse
import itertools
import json
import math
import os
import random

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = os.path.join(HERE, "layout_spec.json")
OUT = os.path.join(HERE, "layout_preview.png")
JSON_OUT = os.path.join(HERE, "layout.json")

SPRINT_MS = 7.6          # правило П2, замеры Понцио по Apex

COLOURS = {
    "HQ_A":     (86, 148, 255),
    "HQ_B":     (255, 96, 84),
    "MISSION":  (255, 168, 62),
    "EXTRACT":  (116, 236, 138),
    "NEUTRAL":  (170, 176, 188),
}
INK = (232, 236, 244)
GROUND = (22, 25, 32)
RING_INK = (44, 50, 62)
LANE_INK = (78, 86, 104)


# ----------------------------------------------------------------------------- скелет

class Slot:
    """Место на карте. Роли у него нет: роль приезжает на забег и уезжает."""

    def __init__(self, sid, ring, sector, x, y, tier):
        self.id = sid
        self.ring = ring          # "hub" | "inner" | "mid" | "outer"
        self.tier = tier          # "hub" | "major" | "buffer" - ранг места, не роль
        self.sector = sector      # адрес по кругу, 0..sectors-1; у хаба None
        self.x = x
        self.y = y                # метры от центра карты, +Y это юг (как в preview.py)

    def __repr__(self):
        return f"{self.id}({self.ring},{self.sector})"


def ring_positions(cfg, sectors, rng):
    """Слоты одного кольца. Эллипс, поворот и дрожание - три источника неравномерности.

    Без них все слоты кольца равноудалены и взаимозаменяемы, и тогда раскладка ролей ничего не
    меняет: любой забег выглядит как предыдущий. Неравномерность здесь не украшение, она и есть
    источник разнообразия.

    Углы раскладываются равномерно по числу слотов, а сектор ВЫЧИСЛЯЕТСЯ из угла. Так кольцу не
    нужно, чтобы число слотов делило число секторов нацело: сектор это адрес для правил расстановки
    штабов, и точным он обязан быть только на внешнем кольце, где слотов ровно по числу секторов.
    """
    out = []
    n = cfg["slots"]
    rot = math.radians(cfg["rotation_deg"])
    a0 = math.radians(cfg.get("angle_offset_deg", 0.0))
    for k in range(n):
        a = a0 + 2.0 * math.pi * k / n
        sector = int(round(a / (2.0 * math.pi / sectors))) % sectors
        r = cfg["radius_m"]
        ex = r * math.cos(a)
        ey = r * cfg["ellipse"] * math.sin(a)
        co = cfg["centre_offset_m"]
        x = co[0] + ex * math.cos(rot) - ey * math.sin(rot)
        y = co[1] + ex * math.sin(rot) + ey * math.cos(rot)
        j = cfg["jitter_m"]
        x += rng.uniform(-j, j)
        y += rng.uniform(-j, j)
        out.append((sector, x, y))
    return out


def relax_separation(slots, min_sep, rounds=200, frozen=frozenset()):
    """Растолкать слипшиеся слоты.

    Кольца задаются радиусами и эллипсами, и на стыке двух колец пара точек регулярно оказывается
    ближе, чем допустимо: два места в полутора сотнях метров это одно место с двумя названиями.
    Подкручивать радиусы руками до исчезновения коллизий - работа, которую придётся делать заново
    после каждой правки спеки, поэтому она делается здесь и один раз.

    Хаб не двигается: он якорь всей схемы. Остальные расходятся по прямой между ними, понемногу,
    чтобы кольцевая структура не рассыпалась в облако.
    """
    movable = [s for s in slots if s.ring != "hub" and s.id not in frozen]
    for _ in range(rounds):
        worst = 0.0
        for a, b in itertools.combinations(slots, 2):
            d = dist(a, b)
            if d >= min_sep:
                continue
            worst = max(worst, min_sep - d)
            if d < 1e-6:
                dx, dy = 1.0, 0.0
                d = 1.0
            else:
                dx, dy = (b.x - a.x) / d, (b.y - a.y) / d
            push = (min_sep - d) * 0.5
            for s_, sign in ((a, -1.0), (b, 1.0)):
                if s_ in movable:
                    s_.x += dx * push * sign
                    s_.y += dy * push * sign
        if worst < 0.5:
            break
    return slots


def build_skeleton(spec, rng):
    """Слоты и связи. Это авторская часть: она НЕ меняется от забега к забегу."""
    sectors = spec["sectors"]

    slots = [Slot("HUB", "hub", None, 0.0, 0.0, "hub")]
    for cfg in spec["rings"]:
        name = cfg["name"]
        for i, (sector, x, y) in enumerate(ring_positions(cfg, sectors, rng)):
            sid = f"{name[0].upper()}{sector}" if name == "outer" else f"{name[0].upper()}{i}"
            slots.append(Slot(sid, name, sector, x, y, cfg["tier"]))

    # С запасом в пару процентов: расталкивание сходится К границе, а ровно на границе проверка
    # спотыкается о плавающую точку и ругается на пару, которая формально в порядке.
    # Ручной сдвиг слота. Позиции считаются кольцами и секторами, но последнее слово всегда за
    # автором: место точки это дизайн, а не вывод формулы. Пишется в spec["slot_overrides"].

    # Карта центруется по ГАБАРИТУ слотов, а не по хабу. Именно поэтому кольца заданы
    # неконцентричными: хаб стоит в нуле, внешнее кольцо смещено, и после этой центровки хаб
    # оказывается смещённым относительно середины карты. Если бы двигали сам хаб, центровка
    # вернула бы его в середину и эксцентриситет пропал бы.
    cx = (min(s.x for s in slots) + max(s.x for s in slots)) * 0.5
    cy = (min(s.y for s in slots) + max(s.y for s in slots)) * 0.5
    for s in slots:
        s.x -= cx
        s.y -= cy
    # Кольца рисуются по своим исходным центрам, значит им нужен тот же сдвиг.
    spec["_recentre_shift"] = (cx, cy)

    # Ручной сдвиг применяется ПОСЛЕ центровки, а не до неё.
    #
    # До - не работало: центровка сдвигает все слоты на габарит, и заданные автором координаты
    # уезжали вместе с остальными. Точка, поставленная в (547,-27), оказывалась в (427,40), и
    # выглядело это как будто override просто не читается.
    #
    # Координаты здесь - те же, что в layout.json и на превью, то есть то, что автор видит.
    ov = spec.get("slot_overrides", {})
    pinned_ids = set()
    for s_ in slots:
        o = ov.get(s_.id)
        if o:
            s_.x, s_.y = float(o["x_m"]), float(o["y_m"])
            pinned_ids.add(s_.id)

    # Расталкивание идёт ПОСЛЕДНИМ и не трогает сдвинутое вручную. Порядок тут важен весь:
    # кольца -> центровка -> ручной сдвиг -> расталкивание. Ручной сдвиг задаётся в тех же
    # координатах, что автор видит на превью, поэтому он обязан идти после центровки; а
    # расталкивание после него, иначе оно честно отодвинет точку от соседей и отменит решение
    # автора - в первом прогоне так и вышло, точку увело с 586 м обратно на 501.
    relax_separation(slots, spec["checks"]["min_slot_separation_m"] * 1.03, frozen=pinned_ids)

    by_ring = {}
    for s in slots:
        by_ring.setdefault(s.ring, []).append(s)

    lanes = []
    cfg = spec["lanes"]

    if cfg.get("hub_to_inner"):
        for s in by_ring.get("inner", []):
            lanes.append(("HUB", s.id))

    if cfg.get("outer_ring_road"):
        # Замкнутая кольцевая дорога снаружи. Без неё обойти можно только через середину, и хаб
        # становится обязательным для всех - то есть тем же Skull Town, только строением.
        ring = sorted(by_ring.get("outer", []), key=lambda s: s.sector)
        for a, b in zip(ring, ring[1:] + ring[:1]):
            lanes.append((a.id, b.id))

    # Буферы стёжками: внутреннее кольцо цепляется за них, они за внешнее. Прямой связи
    # внутреннее-внешнее нет намеренно, иначе через буфер никто не пойдёт.
    for src, dst, key in (("inner", "mid", "inner_to_nearest_mid"),
                          ("mid", "outer", "mid_to_nearest_outer")):
        k = cfg.get(key, 0)
        for s in by_ring.get(src, []):
            near = sorted(by_ring.get(dst, []), key=lambda o: dist(s, o))
            for o in near[:k]:
                lanes.append((s.id, o.id))

    return slots, sorted({tuple(sorted(l)) for l in lanes})


def derive_world(spec, slots):
    """Размер карты из геометрии, а не наоборот.

    габарит слотов -> плюс поле под сам POI -> игровое пространство -> плюс пляжи -> ландшафт,
    округлённый вверх до целого числа компонентов. Пляжи снаружи игровой зоны: по ним не воюют.
    """
    w = spec["world"]
    span = max(max(abs(s.x) for s in slots), max(abs(s.y) for s in slots)) * 2.0
    play = span + 2.0 * w["poi_margin_m"]
    need = play + 2.0 * w["beach_m"]

    per_comp = w["quads_per_section"] * w["sections_per_component"] * w["vertex_spacing_uu"] / 100.0
    comps = max(1, math.ceil(need / per_comp))
    land = comps * per_comp

    return {
        "play_m": play,
        "land_m": land,
        "components": comps,
        "beach_m": (land - play) * 0.5,
        "size_uu": land * 100.0,
        "vertices": comps * w["quads_per_section"] * w["sections_per_component"] + 1,
        "density": len(slots) / (play / 1000.0) ** 2,
    }


def dist(a, b):
    return math.hypot(a.x - b.x, a.y - b.y)


# ----------------------------------------------------------------------------- роли

def sector_gap(a, b, sectors):
    """Круговое расстояние между секторами: 0..sectors//2. Ровно sectors//2 это 'напротив'."""
    d = abs(a - b) % sectors
    return min(d, sectors - d)


def interleaved(pair_a, pair_b, sectors):
    """Идут ли штабы по кольцу через одного: A, B, A, B.

    Без этого требования обе пары могут сесть рядом, и фракция получает половину карты даром -
    воевать будет не за что, потому что делить нечего.
    """
    order = sorted([(s, "A") for s in pair_a] + [(s, "B") for s in pair_b])
    tags = [t for _, t in order]
    return all(tags[i] != tags[(i + 1) % len(tags)] for i in range(len(tags)))


def hq_configurations(spec, outer):
    """Все допустимые расклады четырёх штабов. Считается один раз, не зависит от seed."""
    sectors = spec["sectors"]
    r = spec["roles"]
    lo = r["hq_pair_min_sector_distance"]
    opposite = sectors // 2
    ids = sorted(s.sector for s in outer)

    pairs = [p for p in itertools.combinations(ids, 2)
             if sector_gap(p[0], p[1], sectors) >= lo]

    out = []
    for pa, pb in itertools.permutations(pairs, 2):
        if set(pa) & set(pb):
            continue
        if r["hq_must_interleave"] and not interleaved(pa, pb, sectors):
            continue
        # Обе пары ровно напротив себя - это идеально симметричная вертушка. Формально она проходит
        # все правила, а играется одинаково каждый раз, потому что у сторон нет разницы вообще.
        if r["hq_forbid_both_opposite"]                 and sector_gap(*pa, sectors) == opposite                 and sector_gap(*pb, sectors) == opposite:
            continue
        out.append((pa, pb))
    return out


def assign_roles(spec, slots, seed):
    """Роли на один забег. Всё, что здесь решается, каждый раз решается заново."""
    rng = random.Random(seed)
    by_id = {s.id: s for s in slots}
    outer = [s for s in slots if s.ring == "outer"]
    roles = {s.id: "NEUTRAL" for s in slots}

    # --- штабы ---
    configs = hq_configurations(spec, outer)
    if not configs:
        raise SystemExit("нет ни одной допустимой раскладки штабов: смягчи правила в спеке")
    pa, pb = rng.choice(configs)
    sector_to_id = {s.sector: s.id for s in outer}
    hq_a = [sector_to_id[s] for s in pa]
    hq_b = [sector_to_id[s] for s in pb]
    for i in hq_a:
        roles[i] = "HQ_A"
    for i in hq_b:
        roles[i] = "HQ_B"

    # --- миссии ---
    # Разнесены между собой намеренно. Три миссии в одном пятне это забег, который весь целиком
    # происходит в этом пятне: остальная карта тогда декорация, а фракциям нечего делить на местах,
    # до которых никто не идёт.
    pool = [s.id for s in slots
            if s.tier in spec["roles"]["mission_pool_tiers"] and roles[s.id] == "NEUTRAL"]
    want = spec["roles"]["mission_count"]
    gap = spec["roles"]["mission_min_separation_m"]
    missions = []
    for _ in range(60):
        rng.shuffle(pool)
        picked = []
        for i in pool:
            if all(dist(by_id[i], by_id[j]) >= gap for j in picked):
                picked.append(i)
            if len(picked) == want:
                break
        if len(picked) > len(missions):
            missions = picked
        if len(missions) == want:
            break
    for i in missions:
        roles[i] = "MISSION"

    # --- эвакуации ---
    # Правило Hunt: не на пороге ни у одного штаба. Проверяется до КАЖДОГО, а не до ближайшего.
    lo = spec["roles"]["extract_min_dist_to_hq_m"]
    hqs = [by_id[i] for i in hq_a + hq_b]
    cand = [s.id for s in slots
            if roles[s.id] == "NEUTRAL" and all(dist(by_id[s.id], h) >= lo for h in hqs)]
    rng.shuffle(cand)
    for i in cand[: spec["roles"]["extract_count"]]:
        roles[i] = "EXTRACT"

    got = sum(1 for v in roles.values() if v == "EXTRACT")
    return roles, {
        "seed": seed,
        "hq_a": hq_a,
        "hq_b": hq_b,
        "missions": missions,
        "mission_short": spec["roles"]["mission_count"] - len(missions),
        "extract_short": spec["roles"]["extract_count"] - got,
        "configs": len(configs),
    }


# ----------------------------------------------------------------------------- проверки

def check(spec, slots, lanes, roles, world):
    """Отбраковка до постройки. Дешевле поймать здесь, чем в редакторе."""
    problems = []
    c = spec["checks"]

    for a, b in itertools.combinations(slots, 2):
        d = dist(a, b)
        if d < c["min_slot_separation_m"]:
            problems.append(f"{a.id} и {b.id} стоят в {d:.0f} м (минимум {c['min_slot_separation_m']:.0f})")

    # Квадратом, а не радиусом: карта квадратная, и угол у неё дальше, чем бок.
    lim = world["play_m"] * 0.5 - spec["world"]["poi_margin_m"] + 1.0
    for s in slots:
        if abs(s.x) > lim or abs(s.y) > lim:
            problems.append(f"{s.id} вылез за игровое пространство ({s.x:+.0f},{s.y:+.0f})")

    d0 = world["density"]
    if not (c["target_density_min"] <= d0 <= c["target_density_max"]):
        problems.append(f"плотность {d0:.1f} точки на км2 вне вилки "
                        f"{c['target_density_min']}-{c['target_density_max']}: "
                        f"меняй радиусы колец, не размер карты")

    # Пересечения коридоров. На схеме это просто линии, а в мире это либо мост с тоннелем, либо
    # перекрёсток, которого в графе нет: отряды пройдут друг сквозь друга там, где связи не было.
    # Дешевле узнать здесь, чем обнаружить в редакторе.
    by_id = {s.id: s for s in slots}

    def seg_cross(p1, p2, p3, p4):
        def side(a, b, c):
            return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)
        d1, d2 = side(p3, p4, p1), side(p3, p4, p2)
        d3, d4 = side(p1, p2, p3), side(p1, p2, p4)
        return ((d1 > 0) != (d2 > 0)) and ((d3 > 0) != (d4 > 0))

    for (a, b), (c2, d2_) in itertools.combinations(lanes, 2):
        if {a, b} & {c2, d2_}:
            continue      # общий конец это не пересечение, это перекрёсток
        if seg_cross(by_id[a], by_id[b], by_id[c2], by_id[d2_]):
            problems.append(f"коридоры {a}-{b} и {c2}-{d2_} пересекаются: нужен мост или связь")

    deg = {s.id: 0 for s in slots}
    for a, b in lanes:
        deg[a] += 1
        deg[b] += 1
    if deg:
        top = max(deg.values())
        rest = sorted(deg.values())[:-1]
        typical = sum(rest) / max(len(rest), 1)
        if top > typical * c["max_degree_ratio"]:
            worst = [k for k, v in deg.items() if v == top]
            problems.append(f"доминирующий узел {worst}: {top} связей против {typical:.1f} у прочих")

    return problems, deg


def stats(slots, lanes, roles):
    by_id = {s.id: s for s in slots}
    lens = [dist(by_id[a], by_id[b]) for a, b in lanes]
    hq = [s for s in slots if roles[s.id] in ("HQ_A", "HQ_B")]
    reach = []
    for s in slots:
        if roles[s.id] in ("HQ_A", "HQ_B"):
            continue
        reach.append(min(dist(s, h) for h in hq))
    return {
        "lane_min": min(lens), "lane_max": max(lens), "lane_avg": sum(lens) / len(lens),
        "reach_max": max(reach) if reach else 0.0,
    }


# ----------------------------------------------------------------------------- картинка

def draw_one(spec, slots, lanes, roles, info, size, world):
    half = world["land_m"] * 0.5
    img = Image.new("RGB", (size, size), GROUND)
    d = ImageDraw.Draw(img)

    def px(s):
        return (size * (0.5 + s.x / (2 * half)), size * (0.5 + s.y / (2 * half)))

    # две рамки: снаружи ландшафт с пляжами, внутри игровое пространство
    for edge_m, col, wid in ((world["land_m"] * 0.5, (52, 44, 34), 3),
                             (world["play_m"] * 0.5, RING_INK, 2)):
        k = size * edge_m / (2 * half)
        d.rectangle([size / 2 - k, size / 2 - k, size / 2 + k, size / 2 + k], outline=col, width=wid)

    # кольца, чтобы был виден эксцентриситет
    # кольца там, где они на самом деле: у каждого свой центр, и хаб им не центр
    shift = spec["_recentre_shift"]
    for cfg in spec["rings"]:
        co = cfg["centre_offset_m"]
        cx = size * (0.5 + (co[0] - shift[0]) / (2 * half))
        cy = size * (0.5 + (co[1] - shift[1]) / (2 * half))
        r = cfg["radius_m"] * size / (2 * half)
        d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=RING_INK)

    by_id = {s.id: s for s in slots}
    for a, b in lanes:
        d.line([px(by_id[a]), px(by_id[b])], fill=LANE_INK, width=2)

    for s in slots:
        x, y = px(s)
        role = roles[s.id]
        col = COLOURS[role]
        # Крупно - ранг места (буфер всегда мелкий, он и должен читаться мелким).
        r = {"hub": 14, "major": 11, "buffer": 7}[s.tier]
        if role != "NEUTRAL":
            r += 2
        d.ellipse([x - r, y - r, x + r, y + r], fill=col, outline=INK)
        label = s.id if role == "NEUTRAL" else f"{s.id} {role}"
        d.text((x + r + 4, y - 7), label, fill=INK)

    d.text((10, 10), f"seed {info['seed']}", fill=INK)
    return img


def render(spec, slots, lanes, runs, path, world, tile=760):
    if len(runs) == 1:
        img = draw_one(spec, slots, lanes, runs[0][0], runs[0][1], tile * 2, world)
    else:
        img = Image.new("RGB", (tile * 2 + 12, tile * 2 + 12), (12, 14, 18))
        for i, (roles, info) in enumerate(runs[:4]):
            t = draw_one(spec, slots, lanes, roles, info, tile, world)
            img.paste(t, ((i % 2) * (tile + 12), (i // 2) * (tile + 12)))
    img.save(path)
    return path


# ----------------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, help="один забег, крупно")
    ap.add_argument("--seeds", type=str, default="1,2,3,4", help="четыре забега на одном скелете")
    ap.add_argument("--skeleton-seed", type=int, default=11,
                    help="дрожание слотов. Меняет САМУ карту, а не раскладку ролей.")
    args = ap.parse_args()

    with open(SPEC, encoding="utf-8") as f:
        spec = json.load(f)

    slots, lanes = build_skeleton(spec, random.Random(args.skeleton_seed))
    world = derive_world(spec, slots)

    rings = "  ".join(f"{c['name']} {c['slots']} ({c['tier']})" for c in spec["rings"])
    print(f"ИГРОВОЕ ПРОСТРАНСТВО  {world['play_m']:.0f} x {world['play_m']:.0f} м")
    print(f"ЛАНДШАФТ              {world['land_m']:.0f} x {world['land_m']:.0f} м  "
          f"({world['components']} компонентов, {world['vertices']} вершин, "
          f"{world['size_uu']:.0f} uu)")
    print(f"ПЛЯЖИ                 {world['beach_m']:.0f} м с каждой стороны, вне игровой зоны")
    print(f"СЛОТОВ {len(slots)}: hub 1  {rings}")
    print(f"ПЛОТНОСТЬ {world['density']:.1f} точки на км2 "
          f"(World's Edge 8.5, Hunt: Showdown 16)")
    print(f"СВЯЗЕЙ {len(lanes)}")

    seeds = [args.seed] if args.seed is not None else [int(x) for x in args.seeds.split(",")]
    runs = []
    deg = {}
    for sd in seeds:
        roles, info = assign_roles(spec, slots, sd)
        problems, deg = check(spec, slots, lanes, roles, world)
        st = stats(slots, lanes, roles)
        runs.append((roles, info))

        print()
        print(f"--- забег seed {sd} " + "-" * 40)
        print(f"  штабы A: {info['hq_a']}   штабы B: {info['hq_b']}")
        print(f"  миссии:  {info['missions']}")
        if info["mission_short"]:
            print(f"  МИССИЙ НЕ ХВАТИЛО: {info['mission_short']} "
                  f"(mission_min_separation_m слишком велика для этого пула)")
        if info["extract_short"]:
            print(f"  ЭВАКУАЦИЙ НЕ ХВАТИЛО: {info['extract_short']} "
                  f"(правило дистанции до штабов слишком строгое)")
        print(f"  коридоры {st['lane_min']:.0f}-{st['lane_max']:.0f} м "
              f"(в среднем {st['lane_avg']:.0f}) = "
              f"{st['lane_min'] / SPRINT_MS:.0f}-{st['lane_max'] / SPRINT_MS:.0f} с бегом")
        print(f"  дальше всего от ближайшего штаба: {st['reach_max']:.0f} м "
              f"= {st['reach_max'] / SPRINT_MS:.0f} с")
        for pr in problems:
            print(f"  ПРОБЛЕМА: {pr}")
        if not problems:
            print("  проверки пройдены")

    print()
    print(f"РАЗНЫХ РАСКЛАДОК ШТАБОВ ВСЕГО: {runs[0][1]['configs']} "
          f"(плюс варианты миссий и эвакуаций поверх каждой)")
    print(f"степени вершин: {dict(sorted(deg.items(), key=lambda kv: -kv[1]))}")

    out = render(spec, slots, lanes, runs, OUT, world)
    print(f"КАРТИНКА: {out}")

    # Выгрузка для редактора. Метры здесь, юниты считает тот, кто ставит акторы: смешивать
    # единицы в одном файле - надёжный способ однажды поставить точку в ста метрах от места.
    payload = {
        "world": world,
        "slots": [{"id": s_.id, "ring": s_.ring, "tier": s_.tier, "sector": s_.sector,
                   "x_m": round(s_.x, 1), "y_m": round(s_.y, 1)} for s_ in slots],
        "lanes": [list(l) for l in lanes],
        "runs": [{"seed": i["seed"], "roles": r} for r, i in runs],
    }
    with open(JSON_OUT, "w", encoding="utf-8") as f:
        json.dump(payload, f, ensure_ascii=False, indent=1)
    print(f"ДАННЫЕ:   {JSON_OUT}")


if __name__ == "__main__":
    main()
