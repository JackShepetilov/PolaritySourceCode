"""Рельеф прод-карты: четыре слоя, строго по порядку.

Это замена подхода из MapEventBench/terrain.py. Тот знал одну мысль - "всё, что не площадка и не
коридор, поднимается от края под легальным уклоном" - и оттого у рельефа не было своего замысла: он
был не местом, а отрицанием места. Шум сверху это признавал.

Здесь наоборот. Порядок слоёв скопирован с пайплайна Wildlands (рельеф -> дороги -> поселения ->
детали): каждый следующий слой перебивает предыдущий ЛОКАЛЬНО, а не спорит с ним.

    Слой 1  макро      высотный скелет из подписей автора. Никакого шума.
    Слой 2  точки      рецепт на слот, с секторами подхода.
    Слой 3  коридоры   прорезаются после макро, поэтому через нагорье идёт перевал.
    Слой 4  детали     пока выключен: до этого надо увидеть, что первые три читаются.

Главное правило, из которого всё остальное: ЛЮБАЯ поверхность либо ходимая (<= walk_max_deg), либо
обрыв (>= cliff_min_deg). Запрещённой полосы между ними генератор не умеет делать - не потому что
мы её вычищаем, а потому что нет шага, который бы её порождал.

    python terrain.py            -> heightmap.png, terrain_preview.png, отчёт по проверкам
    python terrain.py --quick    -> вчетверо мельче сетка, для быстрой итерации

Обоснование: Docs/Terrain_System_Design_2026-09-06.md
Спека точки:  Docs/POI_LakeFortress_Spec_2026-09-06.md
"""

import argparse
import math as _math
import json
import math
import os

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
LAYOUT = os.path.join(HERE, "layout.json")
TSPEC = os.path.join(HERE, "terrain_spec.json")
HEIGHTMAP = os.path.join(HERE, "heightmap.png")
PREVIEW = os.path.join(HERE, "terrain_preview.png")


# ---------------------------------------------------------------- вспомогательное

def bearing_deg(dx, dy):
    """Азимут от севера по часовой. Север это мировой -Y, как на всех наших схемах."""
    return np.degrees(np.arctan2(dx, -dy)) % 360.0


def in_arc(ang, a0, a1):
    """Попадает ли азимут в дугу a0->a1 по часовой. Дуга через ноль работает."""
    a0 %= 360.0
    a1 %= 360.0
    if a0 <= a1:
        return (ang >= a0) & (ang <= a1)
    return (ang >= a0) | (ang <= a1)


def smoothstep(t):
    t = np.clip(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


# ---------------------------------------------------------------- слой 1: макро

def warp_sum(ang_deg, harmonics):
    """Сумма гармоник по углу, в метрах. Общая для берега и для точек."""
    off = 0.0
    for h in harmonics:
        off = off + h["amp_m"] * np.sin(np.radians(h["k"] * ang_deg + h["phase_deg"]))
    return off


def build_macro(spec, X, Y, half_m):
    """Слой 1: базовый ОСТРОВ. Тот общий знаменатель, на который лягут высоты точек с весами.

    Раньше здесь был квадратный съезд к краю карты. Он не был ни пляжем, ни берегом - просто фаска
    по периметру, из-за которой остров читался кубиком. Теперь радиальный профиль от центра карты, а
    радиус согнут гармониками: урез воды гуляет мысами и бухтами, пляж то шире, то уже.

    Холмов и впадин на этом слое нет намеренно. Возвышенности вокруг точек делают сами точки своими
    кольцами - так решил автор, и два раза выяснялось, что отдельные макро-формы этому только мешают.
    """
    isl = spec["island"]

    R = np.sqrt(X * X + Y * Y)
    ang = bearing_deg(X, Y)

    # Один варп на весь береговой профиль: урез и пляж двигаются вместе, иначе песок отрывается
    # от воды. Второй, со своей фазой, живёт только на верхнем крае пляжа и меняет его ширину.
    coast = warp_sum(ang, isl["coast_warp"])
    beach_extra = warp_sum(ang, isl["beach_width_warp"])

    # Всё считается ШИРИНАМИ внутрь от уреза. Независимые радиусы со своими варпами давали
    # отрицательную ширину пляжа на части азимутов - вместо песка обрыв в 32 градуса. Ширина,
    # отложенная от уреза, отрицательной стать не может.
    r_shore = isl["shore_r_m"] + coast
    r_shelf = r_shore + isl["shelf_width_m"]

    beach_w = np.maximum(isl["beach_slope_w_m"] + beach_extra, isl["beach_w_min_m"])
    r_beach_top = r_shore - beach_w
    r_flat_in = r_beach_top - isl["beach_flat_w_m"]
    r_plateau = r_flat_in - isl["rise_w_m"]

    h = np.full(X.shape, float(isl["plateau_m"]))

    def band(lo, hi, a, b):
        m = (R > lo) & (R <= hi)
        t = (R - lo) / np.maximum(hi - lo, 1e-6)
        return m, a + (b - a) * smoothstep(t)

    # Плато -> спуск -> сухой пляж -> урез -> шельф. Все стыки через smoothstep: линейные отрезки,
    # состыкованные под углом, дают террасы, и это уже стоило одной переделки.
    for lo, hi, a, b in ((r_plateau, r_flat_in, isl["plateau_m"], isl["beach_top_m"]),
                         (r_flat_in, r_beach_top, isl["beach_top_m"], isl["beach_top_m"]),
                         (r_beach_top, r_shore, isl["beach_top_m"], isl["shore_m"]),
                         (r_shore, r_shelf, isl["shore_m"], isl["sea_floor_m"])):
        m, v = band(lo, hi, a, b)
        h = np.where(m, v, h)

    h = np.where(R > r_shelf, isl["sea_floor_m"], h)
    return h


def export_coast_rocks(spec, H, X, Y, path):
    """Места под береговые скалы-меши: кучками на разных азимутах, у самого уреза.

    Рельеф скал не изображает - ни здесь, ни у крепости. В карте высот они читались тонкой змейкой,
    а поднимать их выше значит лепить из хайтмапа то, для чего он не годится.
    """
    isl = spec["island"]
    cfg = spec["coast_rocks"]
    rng = np.random.default_rng(cfg["seed"])
    lo, hi = cfg["per_cluster"]
    off_lo, off_hi = cfg["radius_offset_m"]

    rocks = []
    for c in range(cfg["clusters"]):
        base_ang = 360.0 * c / cfg["clusters"] + float(rng.uniform(-14.0, 14.0))
        for _ in range(int(rng.integers(lo, hi + 1))):
            a = base_ang + float(rng.uniform(-cfg["spread_deg"], cfg["spread_deg"]))
            coast = warp_sum(np.array(a), isl["coast_warp"])
            r = isl["shore_r_m"] + float(coast) + float(rng.uniform(off_lo, off_hi))
            wx = math.sin(math.radians(a)) * r
            wy = -math.cos(math.radians(a)) * r
            rocks.append({"x_m": round(wx, 1), "y_m": round(wy, 1),
                          "z_m": round(float(sample(H, X, Y, wx, wy)), 2),
                          "yaw_deg": round(a, 1),
                          "in_water": bool(sample(H, X, Y, wx, wy) < isl["shore_m"])})
    with open(path, "w", encoding="utf-8") as f:
        json.dump({"coast_rocks": rocks}, f, ensure_ascii=False, indent=1)
    return len(rocks)


def cone_clamp(H, target, dist_from_flat, max_deg):
    """Прижать поле к целевой высоте конусом, раскрывающимся под легальным уклоном.

    Это ключевая функция всего генератора, и она заменяет плавное смешивание фиксированной ширины.
    Смешивание было ошибкой: ширина бралась константой, и когда перепад до окружающего рельефа
    оказывался больше, чем эта ширина позволяет пройти под 28 градусами, на границе вырастала стена.
    Первый прогон дал ровно это - 10% вершин в запрещённой полосе, все на стыках коридоров с
    площадками.

    Правильная формулировка: НА РАССТОЯНИИ d ОТ ПЛОСКОГО КРАЯ ЗЕМЛЯ МОЖЕТ ОТЛИЧАТЬСЯ ОТ НЕГО НЕ
    БОЛЬШЕ ЧЕМ НА d * tan(угол). Тогда ширина перехода выводится из перепада сама, а не задаётся, и
    склон легален по построению, а не после зажимания. Далеко от точки конус раскрывается настолько,
    что ничего не ограничивает, и макрорельеф проходит как есть.
    """
    lim = dist_from_flat * math.tan(math.radians(max_deg))
    return np.clip(H, target - lim, target + lim)


def warp_radius(R, ang_deg, spec):
    """Согнуть радиус гармониками по углу.

    Профиль точки описан кольцами, и если рисовать его буквально, на карте вырастают идеальные
    концентрические окружности. Первый же импорт в движок дал ровно это: шестнадцать блюдец и
    мишень в центре, уровень читался как круги на полях.

    Искажение решает это, не трогая замысел: к радиусу прибавляется гладкая функция угла, поэтому
    берег озера и подошва апрона гуляют, а глубины, уровни и сектора остаются те же. Это НЕ шум -
    сумма нескольких синусов, детерминированная и управляемая: хочешь другую форму берега, меняешь
    три числа в спеке, а не крутишь seed.

    Амплитуды В МЕТРАХ, и это принципиально. Сначала они были долями радиуса, и у центра, где радиус
    мал, одна и та же доля разносила соседние по углу клетки на метры по высоте: на полном
    разрешении юбка острова вместо своих 28 градусов давала 60. В метрах возмущение одинаково на
    любом радиусе и ничего не ломает.
    """
    off = np.zeros_like(R)
    for h in spec["radius_warp"]["harmonics"]:
        off = off + h["amp_m"] * np.sin(np.radians(h["k"] * ang_deg + h["phase_deg"]))

    # Гашение к центру, и без него всё разваливается. Смещение постоянное в метрах, но УГОЛ у центра
    # меняется тем быстрее, чем меньше радиус: на радиусе 40 м шаг в один метр вбок это полтора
    # градуса, а на семнадцатой гармонике это уже двадцать четыре градуса фазы. Соседние клетки
    # разъезжались по радиусу на 2.4 м, и на юбке острова с её 28 градусами это давало 1.3 м высоты
    # на метр пути - то есть 58 градусов вместо 28, и рваный чёрный край у самой воды.
    #
    # Множитель растёт от нуля в центре: у берега край треплется как задумано, у острова варпа нет.
    damp = np.clip(R / max(spec["radius_warp"]["damp_radius_m"], 1.0), 0.0, 1.0)
    return np.maximum(R + off * damp, 0.0)


# ---------------------------------------------------------------- слой 2: точки

def stamp_recipe(cfg, spec, H, X, Y, cx, cy, cliff_mask, pinned, h0=None, out_weight=None):
    """Универсальный штамп точки: профиль читается ИЗ ДАННЫХ, а не из кода.

    Здесь была `stamp_lake_fortress` - отдельная функция под одну крепость. Это ломало модульность в
    самом корне: вторая точка потребовала бы второй функции, пятая - пятой, и генератор стал бы
    свалкой частных случаев, разъехавшихся между собой.

    При этом сам профиль крепости - просто список колец: радиус, уровень, и всё. Специфичного для
    крепости в нём нет ничего. Значит это данные.

    Формат рецепта:
        rings: [{r_m, level_m}, ...]  - от центра наружу, уровни ОТНОСИТЕЛЬНЫЕ
        hold_r_m                      - до этого радиуса вес точки равен единице
        fade_r_m                      - здесь вес падает до нуля
        sectors                       - дуги подхода: choke / open / cliff
        overlook (необязательно)      - {floor_m, top_m, wall_r_m} для конуса П61

    Внутри первого кольца - его уровень. Между кольцами - smoothstep: у него нулевой наклон на
    концах, поэтому стыки не дают изломов. Линейные отрезки, состыкованные под углом, дают террасы,
    и это уже стоило одной переделки.
    """
    rings = cfg["rings"]
    dx, dy = X - cx, Y - cy
    R_true = np.sqrt(dx * dx + dy * dy)
    ang = bearing_deg(dx, dy)
    R = warp_radius(R_true, ang, spec)

    fade_r = cfg["fade_r_m"]
    hold_r = cfg.get("hold_r_m", cfg["core_r_m"])
    near = R <= fade_r * 1.45
    if out_weight is None:
        out_weight = np.zeros_like(H)
    if not near.any():
        return

    H0 = float(sample(H, X, Y, cx, cy)) if h0 is None else h0

    prof = np.full(R.shape, float(rings[0]["level_m"]))
    for a, b in zip(rings, rings[1:]):
        m = (R > a["r_m"]) & (R <= b["r_m"])
        t = (R - a["r_m"]) / max(b["r_m"] - a["r_m"], 1e-6)
        prof = np.where(m, a["level_m"] + (b["level_m"] - a["level_m"]) * smoothstep(t), prof)
    prof = np.where(R > rings[-1]["r_m"], rings[-1]["level_m"], prof)

    # Уровни рецепта либо ОТНОСИТЕЛЬНЫЕ (смещения от местной высоты), либо АБСОЛЮТНЫЕ - мировые
    # отметки. Второе нужно, когда точка задана горизонталями с чертежа: 30, 40, 60 это высоты, а не
    # смещения, и привязывать их к местной земле значит терять то, что нарисовал автор.
    world = prof if cfg.get("levels_absolute") else H0 + prof

    ov = cfg.get("overlook")
    if ov:
        # Конус П61 по НАСТОЯЩЕМУ радиусу: правило про реальную дистанцию до защищаемого пола,
        # и гнуть её вместе с формой берега значит гнуть само правило.
        ceiling = H0 + ov["floor_m"] + (ov["top_m"] - ov["floor_m"]) * R_true / ov["wall_r_m"]
        world = np.where(R > hold_r, np.minimum(world, ceiling), world)

    # Вес точки: ЯДРО и линейное затухание.
    #
    # core_r_m - радиус, внутри которого точка хозяйка целиком: её вес единица, а чужие веса там
    # обнуляются при сборке. Это не то же самое, что "вес равен единице": две точки могут иметь вес
    # единица в одном месте и тогда сборка их усредняет - именно так у крепости пропадала половина
    # озера, в неё подмешивалась соседняя башня.
    #
    # Снаружи ядра вес падает ЛИНЕЙНО до нуля на fade_r. Без хитростей: smoothstep размазан и сосед
    # успевает съесть профиль, экспонента требует подбирать постоянную под каждую пару.
    core_r = cfg["core_r_m"]
    weight = np.clip(1.0 - (R - core_r) / max(fade_r - core_r, 1e-6), 0.0, 1.0)
    world = np.where(weight > 0.0, world, H)

    keep = ~np.isnan(world) & near
    H[keep] = world[keep]
    out_weight[:] = np.where(near, weight, 0.0)

    pin_r = cfg.get("pin_r_m", 0.0)
    if pin_r:
        pinned |= near & (R <= pin_r)


def stamp_lake_fortress(cfg, spec, H, X, Y, cx, cy, cliff_mask, pinned, h0=None,
                        out_weight=None):
    """Крепость на острове в озере. Профиль радиальный, характер задают сектора.

    Все высоты рецепта ОТНОСИТЕЛЬНЫЕ: ноль это внешняя равнина точки, она же уровень боевого хода
    на стене. Мир входит сюда одним числом - высотой макро в центре слота, - поэтому точку можно
    переставить в другой слот, ничего не пересчитывая.
    """
    r_ = cfg["rings_m"]
    lv = cfg["levels_m"]

    dx, dy = X - cx, Y - cy
    R_true = np.sqrt(dx * dx + dy * dy)
    ang = bearing_deg(dx, dy)

    # Профиль читается по СОГНУТОМУ радиусу, а сектора и проверки - по настоящему: дуга скал должна
    # лежать там, где автор её назначил, а не гулять вместе с берегом.
    R = warp_radius(R_true, ang, spec)

    outer = r_["fade_r"]
    near = R <= outer * 1.45
    if out_weight is None:
        out_weight = np.zeros_like(H)
    if not near.any():
        return

    # Ноль точки = макро в центре слота, снятое ДО правок соседних слоёв.
    H0 = float(sample(H, X, Y, cx, cy)) if h0 is None else h0

    prof = np.full(R.shape, np.nan)

    # --- двор ---
    prof = np.where(R <= r_["courtyard_r"], lv["courtyard"], prof)

    # --- юбка острова: двор вниз ко дну ---
    m = (R > r_["courtyard_r"]) & (R <= r_["island_r"])
    t = (R - r_["courtyard_r"]) / max(r_["island_r"] - r_["courtyard_r"], 1e-6)
    prof = np.where(m, lv["courtyard"] + (lv["lake_bed"] - lv["courtyard"]) * smoothstep(t), prof)

    # --- дно озера, плоское: по нему идут вброд ---
    m = (R > r_["island_r"]) & (R <= r_["lake_bed_outer_r"])
    prof = np.where(m, lv["lake_bed"], prof)

    # --- берег: дно вверх до уровня начала моста ---
    m = (R > r_["lake_bed_outer_r"]) & (R <= r_["lake_outer_r"])
    t = (R - r_["lake_bed_outer_r"]) / max(r_["lake_outer_r"] - r_["lake_bed_outer_r"], 1e-6)
    prof = np.where(m, lv["lake_bed"] + (lv["courtyard"] - lv["lake_bed"]) * smoothstep(t), prof)

    # --- ЕДИНАЯ НЕПРЕРЫВНАЯ КРИВАЯ: уровень начала моста -> равнина ---
    #
    # Здесь стояли два куска - апрон и берег - и на их стыке был излом. Автор попросил, чтобы высота
    # блендилась непрерывно ОТ ВЕРХНЕГО УРОВНЯ ДО УРОВНЯ НАЧАЛА МОСТА, и это ровно один участок,
    # а не два. smoothstep даёт нулевой наклон на обоих концах, поэтому кривая касается и равнины,
    # и уровня моста без перелома.
    #
    # Перелом остаётся только на урезе воды, ниже уровня моста. Там ему и место: это береговая
    # линия, а не откос, и автор про этот участок ничего не просил.
    m = (R > r_["lake_outer_r"]) & (R <= r_["crest_r"])
    t = (R - r_["lake_outer_r"]) / max(r_["crest_r"] - r_["lake_outer_r"], 1e-6)
    prof = np.where(m, lv["courtyard"] + (lv["crest"] - lv["courtyard"]) * smoothstep(t), prof)

    # --- снаружи апрона: потолок КОНУСОМ, а не плоскостью ---
    #
    # Красный овал автора это "уровень максимальной высоты", и первая реализация поняла его
    # буквально, плоским потолком на уровне равнины. Это оказалось неверно по двум причинам.
    #
    # По смыслу: заглянуть во двор мешает не абсолютная высота стрелка, а ПАРАПЕТ. Чем стрелок
    # дальше, тем выше ему надо забраться, чтобы посмотреть через парапет под достаточным углом.
    # Порог поэтому растёт с дистанцией: близкая высота смертельна, дальняя безобидна (П61).
    #
    # По следствиям: плоский потолок радиусом 300 м выжигает на карте диск 600 м, а среднее
    # расстояние между слотами здесь 343 м. Замер показал, что овал накрывает два соседних слота -
    # M3 в 210 м и HUB в 286 м - и расплющивает их. Конус этого не делает.
    release = outer * 0.35
    # Потолок считается по НАСТОЯЩЕМУ радиусу, а не по согнутому: П61 про реальную дистанцию до
    # двора, и гнуть её вместе с формой берега значит гнуть само правило безопасности.
    ceiling = H0 + lv["courtyard"] + (lv["parapet"] - lv["courtyard"]) * R_true / r_["courtyard_r"]

    # ОБЛАСТЬ выбирается по согнутому радиусу, ЗНАЧЕНИЕ потолка считается по настоящему. Раньше в
    # одной маске стояли оба, и на их расхождении появлялись клетки, которые профиль считал уже
    # снаружи апрона, а по правде лежали в озере: потолок заливал их землёй, и бублик воды рвался.
    #
    # Второе тут же: снаружи апрона земля не обрывается к макрорельефу, а отходит от уровня равнины
    # конусом. Без этого профиль кончался на высоте H0, встречал макро на своей высоте, и по краю
    # апрона шёл обрыв - тот самый, которых быть не должно нигде.
    # Наружу от точки высота ПЛАВНО перетекает в макрорельеф, и только потом на неё накладывается
    # ограничение по уклону.
    #
    # Сначала здесь стоял один cone_clamp. Он держал уклон в законных 28 градусах, но именно в них и
    # упирался: там, где макро далеко от уровня точки, зажатие шло ровно по пределу, и на границе
    # вырастала коническая стенка. На плане это был отчётливый резкий край - при том, что рядом
    # стояла моя же подпись "без изломов". Ограничение уклона не то же самое, что гладкость.
    # Чистый конус, применённый ШИРОКО. Промежуточная попытка подмешивать макро через smoothstep
    # была хуже обеих: она давала 17142 клетки в запрещённой полосе против 1400 у голого конуса.
    # Причина в том, что смешивание тянет высоту к макро быстрее, чем конус разрешает, и конусу
    # потом остаётся только упереться в свой предел - то есть построить ту же стенку, но раньше.
    #
    # Конус сам по себе никуда не спешит: далеко от точки он раскрывается шире перепада высот карты
    # и просто пропускает макрорельеф как есть.
    # Наружу точка РАСТВОРЯЕТСЯ в макрорельефе весом, а не кончается на границе.
    #
    # Раньше профиль держал уровень равнины до самого plain_r и там обрывался. Разрез показал, что
    # выходило на стыке: 24 градуса макрорельефа упирались в 0 градусов равнины - угол, который
    # никакой конус не лечит, потому что конус ограничивает УКЛОН, а проблема была в ИЗЛОМЕ.
    #
    # Вес гасит не только высоту, но и наклон: у края точки её профиль весит ноль, и продолжается
    # ровно тот рельеф, что был. Стыка как события больше нет.
    # Снаружи гребня профиль держит его уровень, чтобы весу было что смешивать.
    prof = np.where(R > r_["crest_r"], lv["crest"], prof)
    # Уровни рецепта либо ОТНОСИТЕЛЬНЫЕ (смещения от местной высоты), либо АБСОЛЮТНЫЕ - мировые
    # отметки. Второе нужно, когда точка задана горизонталями с чертежа: 30, 40, 60 это высоты, а не
    # смещения, и привязывать их к местной земле значит терять то, что нарисовал автор.
    world = prof if cfg.get("levels_absolute") else H0 + prof

    # Вес считается ОТ ГРАНИЦЫ ПРОФИЛЯ, а не от берега озера. Первая попытка растворяла профиль
    # начиная с радиуса 130, где он стоит на уровне начала моста, но подмешивала его к уровню
    # РАВНИНЫ - и на этом месте появлялся скачок в 7.5 м. Проверка на изломы поймала его сразу
    # (79 градусов), чего ни одна проверка по уклону не сделала бы.
    # ВЕС точки, а не смешивание на месте. Смешивать должна сборка мастера: точка обязана уметь
    # существовать отдельным файлом со своей высотой и своим весом, иначе её нельзя ни редактировать
    # в своём подуровне, ни складывать с соседом.
    fade_lo = r_["hold_r"]
    fade_hi = r_["fade_r"]
    weight = 1.0 - smoothstep((R - fade_lo) / max(fade_hi - fade_lo, 1e-6))
    world = np.where(R > fade_lo, np.minimum(world, ceiling), world)
    world = np.where(weight > 0.0, world, H)

    # Скальная гряда в рельеф НЕ пишется: она меши. В хайтмапе 8.5 м гряды на карте с перепадом
    # 88 м читались тонкой змейкой, а поднимать их выше значит лепить из карты высот то, для чего
    # она не годится. Сектора cliff при этом никуда не делись - по ним считается П13 и по ним же
    # раскладываются места под камни (см. export_rock_lines). Побочно это убирает целый класс
    # нарушений запрещённой полосы: земля теперь везде ходимая, а путь режут коллизии мешей.

    keep = ~np.isnan(world) & near
    H[keep] = world[keep]
    out_weight[:] = np.where(near, weight, 0.0)

    # Двор, дно и берег пиналить: релаксация уклонов не имеет права их трогать, иначе она
    # заполнит озеро, ровно как это случилось на стенде.
    pinned |= near & (R <= r_["lake_outer_r"])


def export_rock_lines(layout, tspec, H, X, Y, anchors, path):
    """Места под скальные меши вдоль секторов cliff.

    Рельеф их больше не изображает, значит кто-то должен сказать редактору, где они стоят. Точки
    ложатся по дуге сектора с постоянным шагом и лёгким дрожанием, высота снимается с земли.
    """
    rocks = []
    for sid, cfg in _assigned(layout, tspec):
        if not cfg.get("cliffs_as_meshes"):
            continue
        slot = next(s for s in layout["slots"] if s["id"] == sid)
        cx, cy = slot["x_m"], slot["y_m"]
        rl = cfg["rock_line"]
        rng = np.random.default_rng(7)
        for sec in cfg["sectors"]:
            if sec["kind"] != "cliff":
                continue
            a0, a1 = sec["from_deg"], sec["to_deg"]
            span = (a1 - a0) % 360.0
            step_deg = math.degrees(rl["spacing_m"] / rl["radius_m"])
            k = max(2, int(span / step_deg))
            for t in range(k + 1):
                a = math.radians(a0 + span * t / k)
                r = rl["radius_m"] + float(rng.uniform(-rl["jitter_m"], rl["jitter_m"]))
                wx, wy = cx + math.sin(a) * r, cy - math.cos(a) * r
                rocks.append({"poi": sid, "x_m": round(wx, 1), "y_m": round(wy, 1),
                              "z_m": round(float(sample(H, X, Y, wx, wy)), 2),
                              "yaw_deg": round(math.degrees(a), 1)})
    with open(path, "w", encoding="utf-8") as f:
        json.dump({"rocks": rocks}, f, ensure_ascii=False, indent=1)
    return len(rocks)


def _assigned(layout, tspec):
    """Пары (слот, рецепт) по назначениям. Одно место, где решается, что где стоит."""
    sr = tspec.get("slot_recipes", {})
    td = tspec.get("tier_defaults", {})
    out = []
    for s_ in layout["slots"]:
        name = sr.get(s_["id"]) or td.get(s_["tier"])
        if name:
            out.append((s_["id"], tspec["recipes"][name]))
    return out


def sample(H, X, Y, cx, cy):
    """Высота поля в мировой точке. Ближайшая вершина - для опоры точки этого достаточно."""
    step_x = X[0, 1] - X[0, 0]
    step_y = Y[1, 0] - Y[0, 0]
    j = int(round((cx - X[0, 0]) / step_x))
    i = int(round((cy - Y[0, 0]) / step_y))
    i = max(0, min(H.shape[0] - 1, i))
    j = max(0, min(H.shape[1] - 1, j))
    return H[i, j]


def stamp_placeholder(cfg, H, X, Y, cx, cy, pinned, max_deg):
    """Пустая плоская площадка. Это НЕ дизайн, это инфраструктура под коридоры и навмеш."""
    R = np.sqrt((X - cx) ** 2 + (Y - cy) ** 2)
    flat = cfg["flat_r_m"]
    H0 = float(sample(H, X, Y, cx, cy))
    H[:] = cone_clamp(H, H0, np.maximum(R - flat, 0.0), max_deg)
    pinned |= R <= flat


# ---------------------------------------------------------------- слой 3: коридоры

def carve_lanes(spec, layout, H, X, Y, skip_radius, max_deg):
    """Прорезать ходимые коридоры между связанными слотами.

    Работает ПОСЛЕ макро, поэтому коридор через нагорье становится перевалом. Внутрь радиуса
    авторской точки не лезет: там подходы описаны секторами, и коридор бы их продырявил.
    """
    by_id = {s["id"]: s for s in layout["slots"]}
    w = spec["lanes"]["width_m"]
    blend = spec["lanes"]["blend_m"]

    for a_id, b_id in layout["lanes"]:
        a, b = by_id[a_id], by_id[b_id]
        ax, ay = a["x_m"], a["y_m"]
        bx, by = b["x_m"], b["y_m"]
        vx, vy = bx - ax, by - ay
        L = math.hypot(vx, vy)
        if L < 1e-6:
            continue
        vx, vy = vx / L, vy / L

        px, py = X - ax, Y - ay
        t = np.clip(px * vx + py * vy, 0.0, L)
        dist = np.abs(-px * vy + py * vx)
        # Отсекается только зона авторских точек: там подходы задаются секторами, и коридор бы
        # их продырявил. Ограничения по ширине нет - см. конус ниже.
        near_lane = np.ones(H.shape, dtype=bool)
        for cx, cy, rad in skip_radius:
            near_lane &= np.sqrt((X - cx) ** 2 + (Y - cy) ** 2) > rad
        if not near_lane.any():
            continue

        ha = float(sample(H, X, Y, ax, ay))
        hb = float(sample(H, X, Y, bx, by))
        along = ha + (hb - ha) * (t / L)

        # Тем же конусом, что и площадки: за краем полотна земля отходит от него не быстрее, чем
        # под легальным уклоном. Коридор через нагорье становится выемкой с законными бортами,
        # то есть перевалом, а не канавкой со стенами.
        # Конус применяется НА ВСЮ КАРТУ, а не в полосе near. Обрезать его шириной - значит
        # снова получить стену на границе этой ширины: ровно та ошибка, что была со смешиванием.
        # Далеко от коридора конус раскрывается шире перепада высот карты и не ограничивает ничего,
        # так что глобальность тут бесплатна.
        H[:] = np.where(near_lane, cone_clamp(H, along, np.maximum(dist - w * 0.5, 0.0), max_deg), H)


# ---------------------------------------------------------------- уклоны

def slope_deg(H, step_m):
    # Вертикальное усиление: 90 м перепада на 1500 м это очень полого, и честная отмывка такого
    # рельефа выглядит почти плоской. Усиление врёт про уклоны, но показывает ФОРМУ, а превью
    # существует ровно для этого. На сам хайтмап это не влияет никак.
    gy, gx = np.gradient(H, step_m)
    return np.degrees(np.arctan(np.hypot(gx, gy)))


def relax_walkable(H, step_m, max_deg, cliff_mask, pinned, rounds=400):
    """Свести ходимую землю в легальный уклон, не трогая обрывы и запиненное.

    Считается по-хорошему лишней: слои строятся легальными по построению. Она здесь как страховка
    на стыках - и, главное, как ИЗМЕРИТЕЛЬ. Если ей пришлось много работать, значит спека
    противоречива, и об этом надо узнать из отчёта, а не гадать, глядя на карту.
    """
    max_step = math.tan(math.radians(max_deg)) * step_m
    movable = ~(cliff_mask | pinned)
    touched = np.zeros_like(H, dtype=bool)

    for _ in range(rounds):
        worst = 0.0
        for axis in (0, 1):
            d = np.diff(H, axis=axis)
            over = np.abs(d) - max_step
            if over.max() <= 0.01:
                continue
            worst = max(worst, float(over.max()))
            fix = np.clip(over, 0.0, None) * 0.5 * np.sign(d)

            lo = [slice(None)] * 2
            hi = [slice(None)] * 2
            lo[axis] = slice(0, -1)
            hi[axis] = slice(1, None)
            lo, hi = tuple(lo), tuple(hi)

            a_ok = movable[lo]
            b_ok = movable[hi]
            H[lo] = np.where(a_ok, H[lo] + fix, H[lo])
            H[hi] = np.where(b_ok, H[hi] - fix, H[hi])
            touched[lo] |= a_ok & (np.abs(fix) > 0)
            touched[hi] |= b_ok & (np.abs(fix) > 0)
        if worst <= 0.01:
            break
    return touched


# ---------------------------------------------------------------- проверки

def check_all(layout, tspec, H, X, Y, step_m, cliff_mask, anchors):
    """Отбраковка до импорта в редактор. Дешевле поймать здесь, чем в навмеше."""
    out = []
    ok = True

    sl = slope_deg(H, step_m)
    lo, hi = tspec["checks"]["forbidden_band_deg"]
    band = (sl > lo) & (sl < hi)
    frac = float(band.mean())
    good = frac <= tspec["checks"]["max_forbidden_fraction"]
    ok &= good
    out.append(("Полоса {}-{} град (П19)".format(lo, hi),
                "{:.4f}% вершин".format(frac * 100.0), good))

    # Изломы. Это та проверка, которой не хватало: лестница из площадок по 29 градусов проходит
    # порог "максимум 30" с отличием, а выглядит террасами. Уклон и гладкость - разные величины,
    # и мерить надо обе.
    kink = np.maximum(np.abs(np.diff(sl, axis=0, prepend=sl[:1])),
                      np.abs(np.diff(sl, axis=1, prepend=sl[:, :1])))
    # Нормируется НА МЕТР, а не на клетку. С клеткой метрика зависела от сетки: один и тот же склон
    # давал 6.3 на полном разрешении и 12.8 на --quick, где клетка вчетверо крупнее. Порог тогда
    # значит разное на разных прогонах, и предварительный просмотр начинает врать про финальный
    # результат - ровно то, ради чего проверка и заводилась.
    kink = np.where(cliff_mask, 0.0, kink) / max(step_m, 1e-6)
    kmax = float(kink.max())
    klim = tspec["checks"].get("max_kink_deg_per_m", 8.0)
    out.append(("Изломы (скачок уклона на метр)", "{:.1f} град/м".format(kmax), kmax <= klim))
    ok &= kmax <= klim

    walk = ~cliff_mask
    mx = float(sl[walk].max())
    out.append(("Максимальный уклон на ходимом", "{:.1f} град".format(mx), mx <= lo + 0.5))
    ok &= mx <= lo + 0.5

    for sid, cfg in _assigned(layout, tspec):
        if "overlook" not in cfg:
            continue
        slot = next(s for s in layout["slots"] if s["id"] == sid)
        cx, cy = slot["x_m"], slot["y_m"]
        ov = cfg["overlook"]
        # Опора берётся ТА ЖЕ, что использовал штамп, а не снимается с готового поля: снятая с
        # готового она равна высоте пола точки, конус едет вниз, и вся земля, которую штамп подрезал
        # ровно по конусу, объявляется нарушением. Так проверка однажды нарисовала себе восемь тысяч
        # ложных срабатываний.
        if sid not in anchors:
            continue
        H0 = anchors[sid]
        R = np.sqrt((X - cx) ** 2 + (Y - cy) ** 2)
        # Конус П61: порог растёт с дистанцией, близкая высота смертельна, дальняя безобидна.
        # Радиус берётся по краю ЗАЩИЩАЕМОГО ПОЛА, а не по краю всей формы: с большим радиусом конус
        # выходит мягче, чем должен, и разрешает стрелку быть ниже, чем нужно на самом деле.
        ceiling = H0 + ov["floor_m"] + (ov["top_m"] - ov["floor_m"]) * R / ov["wall_r_m"]
        zone = (R > ov["wall_r_m"]) & (R <= tspec["checks"]["overlook_radius_m"])
        n = int((zone & (H > ceiling)).sum())
        out.append(("Конус простреливания {} (П61)".format(sid),
                    "нарушений {}".format(n), n == 0))
        ok &= n == 0

        kinds = {s["kind"] for s in cfg.get("sectors", [])}
        good13 = "choke" in kinds and "open" in kinds
        out.append(("Подходы {} (П13)".format(sid), "+".join(sorted(kinds)), good13))
        ok &= good13

    # Зоны удержания двух точек не имеют права пересекаться: удержание это "здесь моя высота и
    # ничья больше", и две таких зоны на одной земле дают среднее, не нужное ни одной. Поймано на
    # башне: её центр стоял в 345 м от крепости при сумме удержаний 580, и вместо задуманных 60 м
    # площадка под башню вышла на 44.8.
    import itertools as _it
    by_id = {s_["id"]: s_ for s_ in layout["slots"]}
    for (ia, ca), (ib, cb) in _it.combinations(_assigned(layout, tspec), 2):
        need = ca["hold_r_m"] + cb["hold_r_m"]
        a_, b_ = by_id[ia], by_id[ib]
        d = math.hypot(a_["x_m"] - b_["x_m"], a_["y_m"] - b_["y_m"])
        if d < need:
            out.append(("Удержания {} и {} пересекаются".format(ia, ib),
                        "{:.0f} м при нужных {:.0f}".format(d, need), False))
            ok = False

    pad = tspec["placeholder_pad"]
    if not pad.get("enabled"):
        # Заглушки выключены - мерить их плоскость бессмысленно, там просто макрорельеф. Проверка,
        # которая ругается на то, чего нет, приучает не читать отчёт.
        out.append(("Плоскость площадок (П28)", "заглушки выключены", True))
        return ok, out
    worst = 0.0
    for s in layout["slots"]:
        if s["id"] in tspec["poi"]:
            continue
        R = np.sqrt((X - s["x_m"]) ** 2 + (Y - s["y_m"]) ** 2)
        m = R <= pad["flat_r_m"] * 0.8
        if m.any():
            worst = max(worst, float(sl[m].max()))
    good = worst <= tspec["checks"]["pad_flat_max_deg"]
    out.append(("Плоскость площадок (П28)", "{:.1f} град".format(worst), good))
    ok &= good

    return ok, out


# ---------------------------------------------------------------- вывод

def apply_hand_edit(field, sid, z_scale):
    """Наложить ручную лепку автора поверх сгенерированного рельефа точки.

    Хранится РАЗНИЦЕЙ (poi/<ид>/hand.png, 32768 = ноль), а не готовым рельефом, и в этом весь
    смысл: рецепт можно переписать целиком, а вылепленная руками канава останется на месте и
    поедет вместе с новым основанием. Файла нет - значит руками не трогали, это норма.

    Разница сырая, в единицах хайтмапа, поэтому переводится тем же z_scale, каким пишется карта.
    Иначе правка на метр превратилась бы в правку на метр ТОЛЬКО при стандартной вертикали.
    """
    path = os.path.join(HERE, "poi", sid, "hand.png")
    if not os.path.exists(path):
        return 0

    img = Image.open(path)
    if img.mode != "I;16":
        img = img.convert("I;16")
    delta_raw = np.asarray(img).astype(np.int32) - 32768
    if delta_raw.shape != field.shape:
        raise SystemExit("ручная правка {} размером {}, а поле {}".format(
            sid, delta_raw.shape, field.shape))

    field += delta_raw.astype(np.float64) * z_scale / 12800.0
    touched = int(np.count_nonzero(delta_raw))
    if touched:
        print("  РУКА {}: {} клеток, {:+.2f}..{:+.2f} м".format(
            sid, touched,
            delta_raw.min() * z_scale / 12800.0, delta_raw.max() * z_scale / 12800.0))
    return touched


def write_heightmap(H, path, z_scale):
    """16 бит, кодировка Unreal: raw = 32768 + z_см * 128 / ZScale."""
    raw = 32768.0 + (H * 100.0) * 128.0 / z_scale
    lo, hi = float(raw.min()), float(raw.max())
    if lo < 0 or hi > 65535:
        raise SystemExit("высоты не влезают в 16 бит: {:.0f}..{:.0f}".format(lo, hi))
    Image.fromarray(np.round(raw).astype(np.uint16), mode="I;16").save(path)
    return lo, hi


def write_preview(H, layout, tspec, X, Y, path, cliff_mask, anchors, step_m,
                  exaggerate=1.0):
    """Карта сверху с отмывкой рельефа.

    Плоский серый градиент по высоте не читается: на нём макрорельеф выглядит облаком, и разглядеть
    в нём формы нельзя. Отмывка (свет с северо-запада по нормали склона) показывает именно ФОРМУ,
    ради которой слой 1 и существует.
    """
    # Вертикальное усиление: 90 м перепада на 1500 м это очень полого, и честная отмывка такого
    # рельефа выглядит почти плоской. Усиление врёт про уклоны, но показывает ФОРМУ, а превью
    # существует ровно для этого. На сам хайтмап это не влияет никак.
    gy, gx = np.gradient(H * exaggerate, step_m)
    # Свет с северо-запада под 45 градусов, стандартная отмывка.
    az, alt = math.radians(315.0), math.radians(45.0)
    slope = np.arctan(np.hypot(gx, gy))
    aspect = np.arctan2(-gx, gy)
    shade = (np.sin(alt) * np.cos(slope) +
             np.cos(alt) * np.sin(slope) * np.cos(az - aspect))
    shade = np.clip(shade, 0.0, 1.0)

    lo, hi = float(H.min()), float(H.max())
    tint = (H - lo) / max(hi - lo, 1e-6)
    r = np.clip(shade * (0.55 + 0.45 * tint) * 255, 0, 255)
    g = np.clip(shade * (0.58 + 0.40 * tint) * 255, 0, 255)
    b = np.clip(shade * (0.62 + 0.28 * tint) * 255, 0, 255)
    img = np.dstack([r, g, b]).astype(np.uint8)

    for sid, cfg in _assigned(layout, tspec):
        if "water_m" not in cfg:
            continue
        slot = next(s for s in layout["slots"] if s["id"] == sid)
        # Опора та же, что у штампа. Снятая с готового поля она равна высоте двора, и вода тогда
        # не находится вовсе - ровно поэтому озера не было видно на прошлом превью.
        if sid not in anchors:
            continue      # режим --macro-only: точек ещё нет, рисовать их воду нечем
        H0 = anchors[sid]
        R = np.sqrt((X - slot["x_m"]) ** 2 + (Y - slot["y_m"]) ** 2)
        water = (H <= H0 + cfg["water_m"]) & (R <= cfg["rings"][-1]["r_m"])
        img[water] = [42, 110, 195]

    img[cliff_mask] = [205, 72, 60]

    step_x = X[0, 1] - X[0, 0]
    for s in layout["slots"]:
        j = int(round((s["x_m"] - X[0, 0]) / step_x))
        i = int(round((s["y_m"] - Y[0, 0]) / step_x))
        for d in range(-7, 8):
            for a, bb in ((i + d, j), (i, j + d)):
                if 0 <= a < img.shape[0] and 0 <= bb < img.shape[1]:
                    img[a, bb] = [255, 225, 70]

    Image.fromarray(img).save(path)


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--quick", action="store_true", help="вчетверо мельче сетка, для итерации")
    ap.add_argument("--poi", type=str, help="крупный подписанный план одной точки, например I2")
    ap.add_argument("--macro-only", action="store_true",
                    help="только слой 1: посмотреть на формы рельефа без точек и заглушек")
    args = ap.parse_args()

    with open(LAYOUT, encoding="utf-8") as f:
        layout = json.load(f)
    with open(TSPEC, encoding="utf-8") as f:
        tspec = json.load(f)

    w = layout["world"]
    side_m = w["land_m"]
    n = int(w["components"] * 63) + 1
    if args.quick:
        n = (n - 1) // 4 + 1
    step_m = side_m / (n - 1)

    half = side_m * 0.5
    ax = np.linspace(-half, half, n)
    X, Y = np.meshgrid(ax, ax)
    print("СЕТКА {}x{} вершин, шаг {:.2f} м, карта {:.0f} м".format(n, n, step_m, side_m))

    H = build_macro(tspec, X, Y, half)
    print("СЛОЙ 1 макро: {:.1f}..{:.1f} м, макс уклон {:.1f} град"
          .format(float(H.min()), float(H.max()), float(slope_deg(H, step_m).max())))

    if args.macro_only:
        # Только скелет высот. Заглушки-площадки и штампы точек на превью забивают формы собой, а
        # разглядеть надо именно формы: ради них слой 1 и существует отдельно от остальных.
        write_preview(H, layout, tspec, X, Y, PREVIEW,
                      np.zeros(H.shape, dtype=bool), {}, step_m, exaggerate=3.5)
        print("ТОЛЬКО МАКРО -> {}".format(PREVIEW))
        return

    cliff_mask = np.zeros(H.shape, dtype=bool)
    pinned = np.zeros(H.shape, dtype=bool)

    # Каждый слот получает рецепт: авторский, если назначен, иначе заглушка по рангу места.
    # Опора берётся с ЧИСТОГО макро, до всех правок: иначе соседняя точка сдвинет ноль этой, и
    # весь её профиль уедет вместе с ним.
    slot_recipes = tspec.get("slot_recipes", {})
    tier_defaults = tspec.get("tier_defaults", {})
    placed = []
    for s_ in layout["slots"]:
        name = slot_recipes.get(s_["id"]) or tier_defaults.get(s_["tier"])
        if not name:
            continue
        cfg = tspec["recipes"][name]
        placed.append((s_, cfg, name, float(sample(H, X, Y, s_["x_m"], s_["y_m"]))))
    authored = sum(1 for _, _, n, _ in placed if n in slot_recipes.values())
    print("СЛОЙ 2 точек: {} всего, из них авторских {}".format(len(placed), authored))

    # СБОРКА: база отдельно, каждая точка отдельно со своим весом, композит из них.
    #
    # Раньше точка впечатывалась прямо в общее поле и отдельно уже не существовала. Так её нельзя ни
    # редактировать в своём подуровне, ни складывать с соседом: композит получался единственным
    # артефактом, а слагаемых не было. Теперь каждая точка отдаёт СВОЁ поле и СВОЙ вес, а мастер
    # собирается по формуле - и его же проверяют, потому что смешивание по весу умеет породить
    # уклон, которого нет ни у одного слагаемого.
    base = H.copy()
    by_slot = {s_["id"]: s_ for s_ in layout["slots"]}
    recipe_of = {}
    layers = []
    for s_, cfg, name, h0 in placed:
        field = base.copy()
        wgt = np.zeros_like(base)
        stamp_recipe(cfg, tspec, field, X, Y, s_["x_m"], s_["y_m"],
                     cliff_mask, pinned, h0, wgt)
        recipe_of[s_["id"]] = name
        layers.append((s_["id"], field, wgt, float(cfg.get("priority", 0))))
        # Рука ложится ДО того, как поле уйдёт и в свой файл, и в композит мастера: иначе
        # вылепленное было бы видно в подуровне точки и пропадало бы на общей карте.
        apply_hand_edit(field, s_["id"], w.get("z_scale", 100.0))

        out_dir = os.path.join(HERE, "poi", s_["id"])
        os.makedirs(out_dir, exist_ok=True)
        write_heightmap(field, os.path.join(out_dir, "heightmap.png"), w.get("z_scale", 100.0))
        Image.fromarray((np.clip(wgt, 0, 1) * 255).astype(np.uint8)).save(
            os.path.join(out_dir, "weight.png"))
        print("ТОЧКА {}: своя карта высот и карта весов -> {}".format(s_["id"], out_dir))

    # ПРИОРИТЕТ: старшая точка не делится землёй с младшей.
    #
    # Без него две точки, каждая с весом единица в своём ядре, дают среднее - высоту, не нужную ни
    # одной. Первый же прогон с шестнадцатью точками это показал: обод заглушки в 210 м от крепости
    # поднял землю ровно настолько, чтобы двор стал простреливаться, и проверка П61 покраснела.
    #
    # Правило обычное для слоёв с альфой: вес младшей гасится тем, что уже заняла старшая.
    # ЯДРО обнуляет чужие веса. Внутри своего ядра точка хозяйка целиком, и никакая соседняя в
    # него не подмешивается. Без этого две точки с весом единица в одном месте усредняются, и
    # получается высота, не нужная ни одной: у крепости так пропадала половина озера.
    for sid_a, _f, _w, prio_a in layers:
        sa = by_slot[sid_a]
        core = tspec["recipes"][recipe_of[sid_a]]["core_r_m"]
        Rr = np.sqrt((X - sa["x_m"]) ** 2 + (Y - sa["y_m"]) ** 2)
        # Выход из ядра ЛИНЕЙНОЙ полосой, а не ножом. Резкое обнуление давало на границе ядра
        # ступеньку в весе - чужой вес прыгал с нуля до своих 80 процентов на одной клетке, - и в
        # рельефе из этого вырастала стена в 72 градуса. Внутри ядра по-прежнему ноль: точка там
        # хозяйка целиком.
        margin = tspec.get("core_margin_m", 60.0)
        ramp = np.clip((Rr - core) / max(margin, 1e-6), 0.0, 1.0)
        # Ядро гасит только тех, кто НЕ СТАРШЕ. Иначе заглушка с ядром в сотню метров давит
        # авторскую точку рядом с собой: на превью вокруг каждого из четырнадцати слотов вылезло
        # кольцо, а озеро крепости ужалось до полоски.
        for sid_b, _f2, w2, prio_b in layers:
            if sid_b != sid_a and prio_b <= prio_a:
                w2 *= ramp

    wsum = np.zeros_like(base)
    hsum = np.zeros_like(base)
    for _sid, field, wgt, _p in layers:
        wsum += wgt
        hsum += field * wgt

    denom = np.maximum(wsum, 1.0)
    H = (hsum + base * np.maximum(1.0 - wsum, 0.0)) / denom
    print("СБОРКА: база + {} точек по весам".format(len(layers)))

    write_heightmap(base, os.path.join(HERE, "heightmap_base.png"), w.get("z_scale", 100.0))
    print("БАЗА ОСТРОВА -> heightmap_base.png (это и есть мастер без точек)")

    print("СЛОЙ 4 детали: {}".format("включены" if tspec["detail"]["enabled"] else "выключены"))

    # Глобальной релаксации здесь НЕТ, и это решение, а не упущение. Она была написана как
    # страховка, померила себя и провалилась: послойная сборка даёт 1.3% нарушений, а после
    # релаксации их становится 12.4%. Причина в том, что макрорельеф и так идёт под 26 градусов при
    # пределе 28, поэтому любая правка расходится волной по всей карте, и рядом с обрывами
    # релаксатор сам порождает новые стены, подтягивая к ним соседей.
    #
    # Правильно чинить источник, а не последствия: если проверки ниже красные, надо править спеку
    # или конус в том слое, который нарушение породил. Отчёт показывает, где именно.
    sl_now = slope_deg(H, step_m)[~cliff_mask]
    print("УКЛОНЫ после сборки: макс {:.1f} град".format(float(sl_now.max())))

    anchors = {s_["id"]: h0 for s_, _, _n, h0 in placed}
    ok, rows = check_all(layout, tspec, H, X, Y, step_m, cliff_mask, anchors)
    print()
    print("{:<44} {:<20} {}".format("ПРОВЕРКА", "ЗНАЧЕНИЕ", "ИТОГ"))
    for name, val, good in rows:
        print("{:<44} {:<20} {}".format(name, val, "ок" if good else "ПРОВАЛ"))

    if args.poi:
        out = os.path.join(HERE, "poi_{}.png".format(args.poi))
        write_poi_closeup(H, layout, tspec, X, Y, out, cliff_mask, anchors, step_m, args.poi)
        print("ПЛАН ТОЧКИ -> {}".format(out))

    coast_path = os.path.join(HERE, "rocks_coast.json")
    n_coast = export_coast_rocks(tspec, H, X, Y, coast_path)
    print("СКАЛЫ БЕРЕГА: {} мест -> {}".format(n_coast, coast_path))

    rock_path = os.path.join(HERE, "rocks.json")
    n_rocks = export_rock_lines(layout, tspec, H, X, Y, anchors, rock_path)
    print("СКАЛЫ мешами: {} мест -> {}".format(n_rocks, rock_path))

    # ТЕСТОВАЯ карта высот: под каждой заглушкой земля выравнивается в идеальную плоскость.
    #
    # Отдельным файлом, а не правкой основной, и это важно. Выравнивание нужно, чтобы посмотреть
    # драку на шестнадцати точках, и оно временное; прод-карта такой быть не должна. Замер показал,
    # что заглушки рядом с авторскими точками съедаются их профилями - разброс доходил до 15.6 м, -
    # а поднимать заглушкам приоритет нельзя, они бы прорезали дыры в крепости и башне.
    Ht = H.copy()
    auth = set(tspec.get("slot_recipes", {}).keys())
    td = tspec.get("tier_defaults", {})
    flat_margin = tspec.get("test_flat_margin_m", 70.0)
    n_flat = 0
    flats = []
    for s_ in layout["slots"]:
        if s_["id"] in auth:
            continue
        name = td.get(s_["tier"])
        if not name:
            continue
        # Радиус выравнивания СВОЙ, а не радиус ядра рецепта. Ядра заглушек перекрываются между
        # собой - между слотами 150 м, а ядро major 110, - и жёсткая заливка по ним давала ступеньку
        # в 43 градуса там, где две площадки налезали друг на друга с разными уровнями.
        core = tspec.get("test_flat_r_m", 70.0)
        R = np.sqrt((X - s_["x_m"]) ** 2 + (Y - s_["y_m"]) ** 2)
        lvl = float(sample(Ht, X, Y, s_["x_m"], s_["y_m"]))
        # Плоско внутри ядра, дальше линейно возвращаемся к рельефу: резкий край дал бы стену.
        t = np.clip((R - core) / max(flat_margin, 1e-6), 0.0, 1.0)
        near = R <= core + flat_margin
        Ht = np.where(near, lvl * (1.0 - t) + Ht * t, Ht)
        flats.append((R, core, lvl))
        n_flat += 1

    # Второй проход: ядра доводятся точно. Полосы возврата соседних площадок перекрываются и
    # подтягивают уже выровненное - после одного прохода площадки гуляли на 1.3 м. Остаточная
    # ступенька на краю ядра при этом меньше метра на сто семьдесят, то есть доли градуса.
    for R, core, lvl in flats:
        Ht = np.where(R <= core, lvl, Ht)

    # Авторские точки защищаются от ЧУЖОГО выравнивания.
    #
    # Пропустить точку как центр выравнивания мало: полоса возврата у заглушки 70 + 240 = 310 м, а
    # соседний слот стоит в 210 м, и его юбка накрывала крепость целиком. На тестовой карте озеро с
    # островом просто исчезали, и это выглядело как "генератор их не сделал", хотя в мастере они
    # были на месте.
    #
    # Внутри удержания точка своя целиком, дальше линейно возвращаемся к выровненному - резкая
    # граница дала бы стену там, где заглушка подходит вплотную.
    for sid, cfg in _assigned(layout, tspec):
        if sid not in auth:
            continue
        s_ = next(x for x in layout["slots"] if x["id"] == sid)
        hold = float(cfg.get("hold_r_m", cfg.get("core_r_m", 200.0)))
        fade = float(cfg.get("fade_r_m", hold + 200.0))
        R = np.sqrt((X - s_["x_m"]) ** 2 + (Y - s_["y_m"]) ** 2)
        t = np.clip((R - hold) / max(fade - hold, 1e-6), 0.0, 1.0)
        Ht = np.where(R <= fade, H * (1.0 - t) + Ht * t, Ht)
        print("  ТЕСТ: {} защищён от выравнивания до {:.0f} м".format(sid, fade))

    test_path = os.path.join(HERE, "heightmap_test.png")
    write_heightmap(Ht, test_path, w.get("z_scale", 100.0))
    tsl = slope_deg(Ht, step_m)
    print("ТЕСТОВАЯ карта: {} заглушек выровнено -> {}".format(n_flat, test_path))
    print("  макс уклон на ней {:.1f} град".format(float(tsl.max())))

    lo, hi = write_heightmap(H, HEIGHTMAP, w.get("z_scale", 100.0))
    write_preview(H, layout, tspec, X, Y, PREVIEW, cliff_mask, anchors, step_m,
                  exaggerate=3.0)
    print()
    print("ВЫСОТЫ  {:.1f}..{:.1f} м".format(float(H.min()), float(H.max())))
    print("ХАЙТМАП {}".format(HEIGHTMAP))
    print("ПРЕВЬЮ  {}".format(PREVIEW))
    if not ok:
        raise SystemExit("ЕСТЬ ПРОВАЛЕННЫЕ ПРОВЕРКИ - в редактор не несём")


def write_poi_closeup(H, layout, tspec, X, Y, path, cliff_mask, anchors, step_m, sid,
                      span_m=760.0, size=1000):
    """Крупный подписанный план одной точки.

    Существует потому, что на общей карте не видно, где что: чоук это разрыв в 12 градусов на
    радиусе 300 м, и на превью всей карты он неотличим от ничего. Автор так и сказал - "не понимаю
    где чоук". Значит инструмент обязан это показывать, а не предполагать, что и так ясно.
    """
    from PIL import ImageDraw, ImageFont

    # Встроенный шрифт PIL без кириллицы: подписи выходят рядами квадратов. Берём системный.
    font = ImageFont.load_default()
    for cand in (r"C:\Windows\Fontsrial.ttf", r"C:\Windows\Fonts\segoeui.ttf"):
        try:
            font = ImageFont.truetype(cand, 17)
            break
        except Exception:
            continue

    cfg = dict(_assigned(layout, tspec)).get(sid)
    if not cfg:
        print("КРУПНЫЙ ПЛАН: у слота {} нет рецепта, пропускаю".format(sid))
        return
    slot = next(s for s in layout["slots"] if s["id"] == sid)
    cx, cy = slot["x_m"], slot["y_m"]
    H0 = anchors[sid]

    # Рисовалка старше, чем рецепты-данные: она писалась под словарь именованных радиусов, а рецепт
    # теперь список колец. Переводим здесь, чтобы не переписывать всю разметку подписей: имена ниже
    # это роли колец крепости на озере, по порядку от центра наружу.
    rr = [x["r_m"] for x in cfg["rings"]]
    names = ["courtyard_r", "island_r", "lake_bed_outer_r", "lake_outer_r", "crest_r", "hold_r"]
    r_ = {n: rr[i] for i, n in enumerate(names) if i < len(rr)}
    r_["fade_r"] = cfg.get("fade_r_m", rr[-1])
    lv = {"water": cfg.get("water_m", cfg["rings"][1]["level_m"] + 0.45)}

    half = span_m * 0.5
    j0 = int(round((cx - half - X[0, 0]) / step_m))
    i0 = int(round((cy - half - Y[0, 0]) / step_m))
    n = int(round(span_m / step_m))
    j0 = max(0, min(H.shape[1] - n - 1, j0))
    i0 = max(0, min(H.shape[0] - n - 1, i0))
    sub = H[i0:i0 + n, j0:j0 + n]
    subc = cliff_mask[i0:i0 + n, j0:j0 + n]
    subx = X[i0:i0 + n, j0:j0 + n]
    suby = Y[i0:i0 + n, j0:j0 + n]

    gy, gx = np.gradient(sub * 3.0, step_m)
    slope = np.arctan(np.hypot(gx, gy))
    aspect = np.arctan2(-gx, gy)
    az, alt = math.radians(315.0), math.radians(45.0)
    shade = np.clip(np.sin(alt) * np.cos(slope) +
                    np.cos(alt) * np.sin(slope) * np.cos(az - aspect), 0.0, 1.0)
    lo, hi = float(sub.min()), float(sub.max())
    tint = (sub - lo) / max(hi - lo, 1e-6)
    img = np.dstack([np.clip(shade * (0.55 + 0.45 * tint) * 255, 0, 255),
                     np.clip(shade * (0.58 + 0.40 * tint) * 255, 0, 255),
                     np.clip(shade * (0.62 + 0.28 * tint) * 255, 0, 255)]).astype(np.uint8)

    R = np.sqrt((subx - cx) ** 2 + (suby - cy) ** 2)
    img[(sub <= H0 + lv["water"]) & (R <= r_["lake_outer_r"] * 1.2)] = [42, 110, 195]
    img[subc] = [205, 72, 60]

    pic = Image.fromarray(img).resize((size, size), Image.NEAREST)
    d = ImageDraw.Draw(pic)
    k = size / span_m

    def to_px(bearing, radius):
        a = math.radians(bearing)
        wx = cx + math.sin(a) * radius
        wy = cy - math.cos(a) * radius
        return ((wx - (cx - half)) * k, (wy - (cy - half)) * k)

    # Подписи выносятся на поля, к ним ведут выноски. Раньше они рисовались прямо на объекте, и
    # чёрная обводка текста закрывала воду - на плане казалось, что бублик озера порван, хотя в
    # рельефе он замкнут. Превью, которое врёт про собственный предмет, хуже отсутствующего.
    slots_left, slots_right = [], []

    def label(bearing, radius, text, colour):
        x, y = to_px(bearing, radius)
        right = x >= size / 2
        col = slots_right if right else slots_left
        row = len(col)
        col.append(1)
        ly = 70 + row * 30
        lx = size - 16 if right else 16
        anchor = "rt" if right else "lt"
        d.line([(x, y), (lx + (-10 if right else 10), ly + 8)], fill=(150, 150, 150), width=1)
        d.ellipse([x - 4, y - 4, x + 4, y + 4], fill=colour, outline=(0, 0, 0))
        d.text((lx, ly), text, fill=colour, font=font, anchor=anchor,
               stroke_width=3, stroke_fill=(0, 0, 0))

    label(200, 0, "двор {:.0f} м поперёк".format(r_["courtyard_r"] * 2), (255, 210, 60))
    # Считается из колец, а не пишется числом: после правки радиусов подпись иначе врёт.
    _w = max(r_["island_r"] - r_["courtyard_r"], 0.1)
    _d = abs(cfg["rings"][0]["level_m"] - cfg["rings"][1]["level_m"])
    _peak = _math.degrees(_math.atan(1.5 * _d / _w))
    label(150, r_["island_r"], "остров, юбка {:.0f} м / до {:.0f} град".format(_w, _peak),
          (255, 210, 60))
    label(120, (r_["island_r"] + r_["lake_bed_outer_r"]) * 0.5,
          "озеро, брод {:.0f} м".format(r_["lake_bed_outer_r"] - r_["island_r"]), (90, 170, 255))
    label(70, r_["lake_outer_r"], "начало моста, берег", (200, 200, 200))
    label(55, (r_["lake_outer_r"] + r_["crest_r"]) * 0.5,
          "склон к гребню, {:.0f} м".format(r_["crest_r"] - r_["lake_outer_r"]), (200, 200, 200))
    label(35, r_["crest_r"], "ГРЕБЕНЬ: остров +7 м, ровный по кругу", (255, 210, 60))
    label(20, r_["fade_r"], "растворение в макро", (160, 160, 160))

    for sec in cfg["sectors"]:
        mid = (sec["from_deg"] + sec["to_deg"]) * 0.5
        if sec["to_deg"] < sec["from_deg"]:
            mid = ((sec["from_deg"] + sec["to_deg"] + 360.0) * 0.5) % 360.0
        if sec["kind"] == "cliff":
            label(mid, r_["crest_r"], "СКАЛЫ {:.0f}-{:.0f} град".format(
                sec["from_deg"], sec["to_deg"]), (255, 90, 80))
        elif sec["kind"] == "choke":
            label(mid, r_["crest_r"], "ЧОУК: проход {:.0f}-{:.0f} град".format(
                sec["from_deg"], sec["to_deg"]), (120, 255, 140))

    br = cfg["bridge"]["bearing_deg"]
    x0, y0 = to_px(br, r_["island_r"])
    x1, y1 = to_px(br, r_["lake_outer_r"])
    d.line([(x0, y0), (x1, y1)], fill=(255, 255, 255), width=5)
    label(br, r_["lake_outer_r"], "МОСТ 50 м (актор, не рельеф)", (255, 255, 255))

    d.line([(20, size - 24), (20 + 100 * k, size - 24)], fill=(255, 255, 255), width=3)
    d.text((20, size - 48), "100 м", fill=(255, 255, 255), font=font,
           stroke_width=3, stroke_fill=(0, 0, 0))
    d.text((20, 14), "{}  север вверх  свет с СЗ  вертикаль усилена x3".format(sid),
           fill=(255, 255, 255), font=font, stroke_width=3, stroke_fill=(0, 0, 0))
    pic.save(path)


if __name__ == "__main__":
    main()
