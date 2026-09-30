"""Ферма на нашем плато: постройки и укрытия базы. Чистый питон, без unreal: раскладку можно
проверить и нарисовать вне редактора (python base_layout.py), а build_lanes.step_base ставит её.

Сеттинг (автор 2026-09-30): фермер из американской глубинки, посёлок выкуплен ИИ-компанией под
датацентр, дороги перекрыты, на ферму идут наёмники и дроны. Отсюда и набор: дом, сарай, где
собирается мех, водонапорная вышка, силос, стога, техника, и то, чем фермер сам забаррикадировался
(поддоны, мешки с кормом, бетонные блоки, покрышки).

Оси как на карте: север +X, восток +Y, азимут 0 = +X, 90 = +Y. Центр плато = центр базы. Линии
уходят с плато на азимутах 0 (Top), 45 (Mid), 90 (Bot): это фронт. Тыл (азимуты 135-315) упирается
в края карты, там постройки.

Высоты под рост игрока (MovementSettings: стоя 192 см, сидя 100 см):
    низкое укрытие 110-120 см  сидя не видно, встал и стреляешь поверх
    высокое укрытие от 220 см  не видно и стоя
"""

import math

CROUCH = 115.0          # низкое укрытие
TALL = 230.0            # высокое укрытие

CENTER_CLEAR_R = 1200.0     # место под трон (раздатчик) и проход к нему
CORRIDOR_HALF_IN = 400.0    # полуширина прохода по линии внутри двора (r < 2500): 8 м дороги
CORRIDOR_HALF_OUT = 600.0   # и дальше к пандусу: 12 м
# Проходы во дворе ведут от выхода подъёма на плато к трону (RAMP_TOP_BEARING ниже по файлу).
LANE_BEARINGS = None
T4_PADS = [(2800.0, 67.5), (2800.0, 22.5)]   # r, азимут; из terrain_lanes (T4_R)
T4_CLEAR = 500.0
PLATEAU_R = 6000.0


def polar(bearing, r):
    a = math.radians(bearing)
    return r * math.cos(a), r * math.sin(a)


# ==================== холм: край плато и подъёмы ====================
# Общее для рельефа (terrain_lanes.py) и для расстановки: один источник правды о том, где край
# плато и где дороги на него. Автор 2026-09-30: «идеальные лестницы и обрывы между ними это отстой».

HG_EDGE_MIN = 6000.0     # край плато не ближе 60 м от центра: расстановка двора рассчитана на это
HG_EDGE_AMP = 700.0      # и выпирает наружу до 7 м


def hg_edge_r(bearing, sin=math.sin):
    """Радиус края плато по азимуту: неровный, три-семь выступов по кругу, всегда наружу.
    sin=numpy.sin принимает массив азимутов (рельеф считает так всю сетку разом)."""
    a = bearing * (math.pi / 180.0)
    n = (0.45 * sin(3.0 * a + 0.7) + 0.35 * sin(7.0 * a + 2.1) + 0.20 * sin(11.0 * a + 4.0))
    return HG_EDGE_MIN + HG_EDGE_AMP * (0.5 + 0.5 * n)


def hg_face_w(bearing, sin=math.sin):
    """Ширина склона обрыва: от 3 до 6 м, 9 м высоты на них это 56-72 градуса."""
    a = bearing * (math.pi / 180.0)
    return 450.0 + 150.0 * sin(5.0 * a + 1.3)


# Подъёмы на плато, по заходу. Точки от кромки плато вниз к подножию, в координатах от центра
# нашей базы (uu). У вражеской базы те же, повёрнутые на 180 (Top и Bot меняются местами).
RAMP_HALF_BY = {"Mid": 500.0, "Top": 450.0, "Bot": 250.0}
RAMP_PATHS = {
    # Подъездная дорожка с улицы: плавная S-кривая, 8-10 м в ширину.
    "Mid": [polar(45.0, 4800.0), polar(45.0, 7000.0), polar(40.0, 8200.0), polar(34.0, 9300.0),
            polar(37.0, 10600.0), polar(43.0, 12000.0)],
    # Полевой въезд с грунтовки: дорога идёт вдоль склона, врезана в холм.
    "Top": [polar(345.0, 4800.0), polar(345.0, 7000.0), polar(354.0, 7800.0), polar(3.0, 8500.0),
            polar(9.0, 9500.0), polar(4.0, 11000.0)],
    # Тропа: узкая, зигзагом, с разворотом на склоне.
    "Bot": [polar(95.0, 4800.0), polar(95.0, 7000.0), polar(106.0, 7600.0), polar(114.0, 8400.0),
            polar(101.0, 9300.0), polar(90.0, 10200.0), polar(90.0, 11500.0)],
}
# Азимут, под которым подъём выходит на плато: там проход во двор к трону.
RAMP_TOP_BEARING = {"Mid": 45.0, "Top": 345.0, "Bot": 95.0}


class Layout:
    """Список примитивов в координатах относительно центра базы (uu), z0 от верха плато."""

    def __init__(self):
        self.items = []

    def box(self, label, x, y, z0, sx, sy, sz, yaw=0.0, mat="wood", pitch=0.0, cover=False):
        self.items.append({"kind": "box", "label": label, "x": x, "y": y, "z0": z0,
                           "sx": sx, "sy": sy, "sz": sz, "yaw": yaw, "pitch": pitch, "mat": mat, "cover": cover})

    def cyl(self, label, x, y, z0, d, h, mat="hay", lying_yaw=None, cover=False):
        """Цилиндр стоя (lying_yaw None) или лёжа, ось по азимуту lying_yaw."""
        self.items.append({"kind": "cyl", "label": label, "x": x, "y": y, "z0": z0, "d": d, "h": h,
                           "lying_yaw": lying_yaw, "mat": mat, "cover": cover})

    def local(self, ax, ay, yaw, u, v):
        """Точка (u вперёд, v вправо) в рамке с началом (ax, ay), повёрнутой на yaw."""
        a = math.radians(yaw)
        return ax + u * math.cos(a) - v * math.sin(a), ay + u * math.sin(a) + v * math.cos(a)

    def lbox(self, label, ax, ay, yaw, u, v, z0, su, sv, sz, **kw):
        x, y = self.local(ax, ay, yaw, u, v)
        self.box(label, x, y, z0, su, sv, sz, yaw=yaw, **kw)

    def walls(self, label, ax, ay, yaw, depth, width, h, t=30.0, gaps=None, mat="wood"):
        """Четыре стены коробки depth (вперёд) x width. gaps: {"front"/"back"/"left"/"right":
        [(от, до, подоконник, верх проёма)]}, координаты вдоль стены от её левого (или заднего) края."""
        gaps = gaps or {}
        sides = {
            "front": (depth * 0.5, None, width),
            "back": (-depth * 0.5, None, width),
            "right": (None, width * 0.5, depth),
            "left": (None, -width * 0.5, depth),
        }
        for side, (fu, fv, length) in sides.items():
            pieces = []
            cur = 0.0
            for g0, g1, sill, head in sorted(gaps.get(side, [])):
                if g0 > cur:
                    pieces.append((cur, g0, 0.0, h))
                if sill > 0:
                    pieces.append((g0, g1, 0.0, sill))
                if head < h:
                    pieces.append((g0, g1, head, h))
                cur = g1
            if cur < length:
                pieces.append((cur, length, 0.0, h))
            for i, (p0, p1, zb, zt) in enumerate(pieces):
                mid = (p0 + p1) * 0.5 - length * 0.5
                if fu is not None:   # стена поперёк: тянется по v
                    self.lbox("{}_{}_{}".format(label, side, i), ax, ay, yaw, fu, mid, zb, t, p1 - p0, zt - zb, mat=mat)
                else:                # вдоль: тянется по u
                    self.lbox("{}_{}_{}".format(label, side, i), ax, ay, yaw, mid, fv, zb, p1 - p0, t, zt - zb, mat=mat)


def facing_center(x, y):
    """Азимут от точки на центр базы: так постройка смотрит фасадом во двор."""
    return math.degrees(math.atan2(-y, -x))


# ==================== постройки (тыл) ====================

def house(L):
    ax, ay = polar(225.0, 3200.0)
    yaw = facing_center(ax, ay)
    D, W, H = 1000.0, 1400.0, 350.0
    L.walls("House", ax, ay, yaw, D, W, H, gaps={
        "front": [(600.0, 800.0, 0.0, 250.0), (200.0, 400.0, 100.0, 220.0), (1000.0, 1200.0, 100.0, 220.0)],
        "back": [(600.0, 800.0, 0.0, 250.0)],
        "left": [(350.0, 650.0, 100.0, 220.0)],
        "right": [(350.0, 650.0, 100.0, 220.0)],
    }, mat="house")
    # Внутренняя стенка: две комнаты, бой в доме не простреливается насквозь.
    L.lbox("House_Inner", ax, ay, yaw, 0.0, 250.0, 0.0, 20.0, 600.0, H, mat="house")
    L.lbox("House_Roof", ax, ay, yaw, 0.0, 0.0, H, D + 60.0, W + 60.0, 30.0, mat="roof")
    # Крыша это огневая точка: парапет по краю, проём под пандус сзади.
    L.walls("House_Parapet", *L.local(ax, ay, yaw, 0.0, 0.0), yaw, D + 60.0, W + 60.0, 90.0, t=20.0,
            gaps={"back": [(150.0, 450.0, 0.0, 90.0)]}, mat="house")
    for it in L.items[-12:]:
        if it["label"].startswith("House_Parapet"):
            it["z0"] += H + 30.0
    # Пандус на крышу снаружи вдоль задней стены: верх у проёма в парапете (v ~ -430), низ дальше по
    # стене. Поднимается в сторону -v, то есть рыло коробки смотрит на yaw - 90.
    run, rise = 700.0, H + 30.0
    rx, ry = L.local(ax, ay, yaw, -D * 0.5 - 90.0, -430.0 + run * 0.5)
    L.box("House_RoofRamp", rx, ry, rise * 0.5 - 10.0, math.hypot(run, rise), 150.0, 20.0, yaw=yaw - 90.0,
          pitch=math.degrees(math.atan2(rise, run)), mat="wood")
    # Крыльцо с навесом во двор: под навесом дрон теряет цель.
    L.lbox("Porch_Roof", ax, ay, yaw, D * 0.5 + 150.0, 0.0, 280.0, 300.0, W, 20.0, mat="wood")
    for i, v in enumerate((-650.0, -220.0, 220.0, 650.0)):
        L.lbox("Porch_Post_{}".format(i), ax, ay, yaw, D * 0.5 + 285.0, v, 0.0, 20.0, 20.0, 280.0, mat="wood")
    L.lbox("Porch_Rail_L", ax, ay, yaw, D * 0.5 + 285.0, -440.0, 0.0, 15.0, 400.0, 100.0, mat="wood")
    L.lbox("Porch_Rail_R", ax, ay, yaw, D * 0.5 + 285.0, 440.0, 0.0, 15.0, 400.0, 100.0, mat="wood")


def barn(L):
    """Сарай, где собирается мех: большой, пустой внутри, широкие ворота во двор."""
    ax, ay = polar(275.0, 3900.0)
    yaw = facing_center(ax, ay)
    D, W, H = 1400.0, 2200.0, 800.0
    L.walls("Barn", ax, ay, yaw, D, W, H, gaps={
        "front": [(700.0, 1500.0, 0.0, 650.0)],        # ворота 8 x 6.5 м
        "back": [(950.0, 1250.0, 0.0, 250.0)],
        "left": [(300.0, 500.0, 450.0, 600.0), (900.0, 1100.0, 450.0, 600.0)],
        "right": [(300.0, 500.0, 450.0, 600.0), (900.0, 1100.0, 450.0, 600.0)],
    }, mat="barn")
    L.lbox("Barn_Roof", ax, ay, yaw, 0.0, 0.0, H, D + 80.0, W + 80.0, 30.0, mat="roof")
    # Сеновал у задней стены: второй этаж для боя в сарае, пандус к нему.
    L.lbox("Barn_Loft", ax, ay, yaw, -D * 0.5 + 250.0, 0.0, 400.0, 500.0, W - 60.0, 20.0, mat="wood")
    L.lbox("Barn_LoftRail", ax, ay, yaw, -D * 0.5 + 490.0, 250.0, 420.0, 15.0, W - 700.0, 100.0, mat="wood")
    # Пандус на сеновал вдоль левой стены: низ во дворе сарая, верх у края сеновала.
    run, rise = 800.0, 410.0
    rx, ry = L.local(ax, ay, yaw, -D * 0.5 + 500.0 + run * 0.5, -W * 0.5 + 180.0)
    L.box("Barn_LoftRamp", rx, ry, rise * 0.5 - 10.0, math.hypot(run, rise), 150.0, 20.0, yaw=yaw + 180.0,
          pitch=math.degrees(math.atan2(rise, run)), mat="wood")
    # Верстак и бочки у стены: укрытия внутри, центр пустой под мех.
    L.lbox("Barn_Bench", ax, ay, yaw, 0.0, W * 0.5 - 150.0, 0.0, 500.0, 100.0, CROUCH, mat="metal", cover=True)
    for i, u in enumerate((-250.0, -150.0)):
        L.cyl("Barn_Barrel_{}".format(i), *L.local(ax, ay, yaw, u, -W * 0.5 + 200.0), 0.0, 70.0, 100.0, mat="metal")


def water_tower(L):
    """Водонапорная вышка: наблюдательный пункт над линиями, туман режет обзор на 150 м."""
    ax, ay = polar(168.0, 3900.0)
    for i, (du, dv) in enumerate(((-250.0, -250.0), (-250.0, 250.0), (250.0, -250.0), (250.0, 250.0))):
        L.box("Tower_Leg_{}".format(i), ax + du, ay + dv, 0.0, 40.0, 40.0, 1200.0, mat="metal")
    L.box("Tower_Deck", ax, ay, 1200.0, 700.0, 700.0, 25.0, mat="wood")
    L.walls("Tower_Rail", ax, ay, 0.0, 700.0, 700.0, 100.0, t=12.0, gaps={"back": [(300.0, 400.0, 0.0, 100.0)]}, mat="metal")
    for it in L.items[-6:]:
        if it["label"].startswith("Tower_Rail"):
            it["z0"] += 1225.0
    L.cyl("Tower_Tank", ax, ay, 1225.0, 440.0, 400.0, mat="metal")


def silo_and_sheds(L):
    L.cyl("Silo", *polar(250.0, 5000.0), 0.0, 600.0, 1300.0, mat="metal")
    L.cyl("Silo_B", *polar(242.0, 5200.0), 0.0, 450.0, 1000.0, mat="metal")
    # Курятник: низкая будка, прятаться сидя за ней.
    ax, ay = polar(195.0, 4700.0)
    L.box("Coop", ax, ay, 0.0, 400.0, 600.0, 180.0, yaw=facing_center(ax, ay), mat="barn", cover=True)
    # Сортир во дворе за домом.
    ax, ay = polar(222.0, 5300.0)
    L.box("Outhouse", ax, ay, 0.0, 130.0, 130.0, 240.0, yaw=facing_center(ax, ay), mat="wood")
    # Ветряк-водокачка: вертикаль, видная из тумана, ориентир базы.
    ax, ay = polar(305.0, 4800.0)
    L.box("Windmill_Mast", ax, ay, 0.0, 50.0, 50.0, 1500.0, mat="metal")
    L.box("Windmill_Head", ax, ay, 1450.0, 150.0, 500.0, 500.0, yaw=45.0, mat="metal")


# ==================== укрытия ====================

def front_yard(L):
    """Двор между линиями: бой за трон. Всё вне проходов, см. check()."""
    # Круглые рулоны сена: лёжа, 1.8 м в диаметре (стоя не видно, сверху можно стоять).
    for i, (b, r, ly) in enumerate(((14.0, 3700.0, 80.0), (19.0, 3900.0, 60.0), (27.0, 4700.0, 110.0),
                                    (72.0, 3700.0, 10.0), (70.0, 4200.0, 40.0), (72.0, 5200.0, 150.0))):
        L.cyl("HayRoll_{}".format(i), *polar(b, r), 0.0, 180.0, 160.0, lying_yaw=ly, mat="hay", cover=True)
    # Прямоугольные тюки: 2 в высоту (высокое укрытие) и 1 (низкое).
    for i, (b, r, yaw, high) in enumerate(((29.0, 2200.0, 120.0, True), (61.0, 2200.0, 150.0, True),
                                           (20.0, 5000.0, 20.0, False), (67.0, 3800.0, 70.0, False))):
        x, y = polar(b, r)
        L.box("HayBales_{}".format(i), x, y, 0.0, 240.0, 120.0, CROUCH, yaw=yaw, mat="hay", cover=True)
        if high:
            L.box("HayBales_{}_Top".format(i), x, y, CROUCH, 240.0, 120.0, CROUCH, yaw=yaw + 90.0, mat="hay", cover=True)
    # Пикап (кузов низкий, кабина высокая) и трактор.
    ax, ay = polar(31.0, 5000.0)
    L.box("Pickup_Bed", ax, ay, 0.0, 520.0, 210.0, 110.0, yaw=105.0, mat="truck", cover=True)
    L.lbox("Pickup_Cab", ax, ay, 105.0, 120.0, 0.0, 110.0, 180.0, 200.0, 80.0, mat="truck")
    ax, ay = polar(75.0, 3400.0)
    L.box("Tractor_Body", ax, ay, 40.0, 360.0, 170.0, 150.0, yaw=-20.0, mat="tractor", cover=True)
    L.lbox("Tractor_Cab", ax, ay, -20.0, -80.0, 0.0, 190.0, 150.0, 150.0, 130.0, mat="tractor")
    for i, (u, v, d) in enumerate(((-110.0, -110.0, 170.0), (-110.0, 110.0, 170.0), (130.0, -100.0, 100.0), (130.0, 100.0, 100.0))):
        L.cyl("Tractor_Wheel_{}".format(i), *L.local(ax, ay, -20.0, u, v), 0.0, d, 50.0, lying_yaw=-20.0 + 90.0, mat="tire")
    # Поленница и корыто: низкие.
    L.box("WoodPile", *polar(15.0, 5200.0), 0.0, 320.0, 110.0, CROUCH, yaw=110.0, mat="wood", cover=True)
    L.box("Trough", *polar(115.0, 2400.0), 0.0, 280.0, 90.0, 100.0, yaw=150.0, mat="metal", cover=True)
    # Топливная ёмкость на опорах: высокое укрытие, под ней видно ноги.
    ax, ay = polar(30.0, 3500.0)
    L.cyl("FuelTank", ax, ay, 90.0, 180.0, 380.0, lying_yaw=130.0, mat="fuel", cover=True)
    L.box("FuelTank_Stand", ax, ay, 0.0, 250.0, 120.0, 90.0, yaw=130.0, mat="metal")


def anchors(L):
    """Вертикаль средней высоты во дворе (автор 2026-09-30: «плоско, не за что цепляться хуком»).
    По якорю в каждом клине двора: забраться хуком, встать сверху, сменить высоту в бою за трон."""
    # Клин Top-Mid: бункер для корма на ножках, плоский верх на 7.5 м.
    ax, ay = polar(22.0, 4200.0)
    for i, (du, dv) in enumerate(((-110.0, -110.0), (-110.0, 110.0), (110.0, -110.0), (110.0, 110.0))):
        L.box("FeedBin_Leg_{}".format(i), ax + du, ay + dv, 0.0, 25.0, 25.0, 250.0, mat="metal")
    L.cyl("FeedBin", ax, ay, 250.0, 300.0, 500.0, mat="metal")
    L.box("FeedBin_Chute", ax, ay, 90.0, 60.0, 60.0, 160.0, mat="metal")
    # Клин Mid-Bot: навес на столбах (крыша 4.5 м, под ней бой в тени от дронов) и столб линии
    # электропередачи рядом (9 м, перекладина сверху: цепляться хуком).
    ax, ay = polar(60.0, 5000.0)
    yaw = facing_center(ax, ay)
    L.lbox("LeanTo_Roof", ax, ay, yaw, 0.0, 0.0, 450.0, 600.0, 800.0, 25.0, mat="roof")
    for i, (u, v) in enumerate(((-280.0, -380.0), (-280.0, 380.0), (280.0, -380.0), (280.0, 380.0))):
        L.lbox("LeanTo_Post_{}".format(i), ax, ay, yaw, u, v, 0.0, 25.0, 25.0, 450.0, mat="wood")
    L.lbox("LeanTo_Hay", ax, ay, yaw, -150.0, 0.0, 0.0, 240.0, 600.0, CROUCH, mat="hay", cover=True)
    px, py = polar(63.0, 5500.0)
    L.box("PowerPole", px, py, 0.0, 35.0, 35.0, 900.0, mat="wood")
    L.box("PowerPole_Arm", px, py, 820.0, 30.0, 260.0, 25.0, yaw=63.0, mat="wood")


def barricades(L):
    """То, чем фермер перегородил подъёмы на плато: у каждого пандуса стенки из поддонов и
    бетонные блоки уступом, проход в середине остаётся (крипам нужен путь к трону)."""
    for name, b in RAMP_TOP_BEARING.items():
        # Бровка пандуса на r ~ 5700. Два блока по краям прохода и уступ поддонов внутри.
        for side in (-1.0, 1.0):
            x, y = L.local(0.0, 0.0, b, 5300.0, side * 850.0)
            L.box("Jersey_{}_{}".format(name, "L" if side < 0 else "R"), x, y, 0.0, 60.0, 380.0, 110.0,
                  yaw=b, mat="concrete", cover=True)
            x, y = L.local(0.0, 0.0, b, 4500.0, side * 1100.0)
            L.box("Pallets_{}_{}".format(name, "L" if side < 0 else "R"), x, y, 0.0, 120.0, 300.0, CROUCH,
                  yaw=b + side * 25.0, mat="wood", cover=True)
        # Покрышки стопкой: низкая точка на выходе с пандуса.
        x, y = L.local(0.0, 0.0, b, 3900.0, 750.0)
        L.cyl("Tires_{}".format(name), x, y, 0.0, 110.0, CROUCH, mat="tire", cover=True)


def inner_ring(L):
    """Мешки с кормом полукругом у трона: последний рубеж. Короткие куски, между ними проходы."""
    for i, b in enumerate((22.5, 67.5, 135.0, 200.0, 252.0, 312.0)):
        x, y = polar(b, 1600.0)
        L.box("FeedSacks_{}".format(i), x, y, 0.0, 70.0, 280.0, CROUCH, yaw=b, mat="sacks", cover=True)


def rear_yard(L):
    for i, (b, r) in enumerate(((150.0, 2500.0), (185.0, 2300.0))):
        L.box("Crates_{}".format(i), *polar(b, r), 0.0, 150.0, 150.0, 150.0, yaw=b + 15.0, mat="wood", cover=True)
    # Старая легковушка на блоках за сараем.
    ax, ay = polar(300.0, 3000.0)
    L.box("CarWreck", ax, ay, 0.0, 450.0, 190.0, 140.0, yaw=20.0, mat="truck", cover=True)
    L.box("Firewood_Back", *polar(210.0, 5000.0), 0.0, 400.0, 110.0, CROUCH, yaw=300.0, mat="wood", cover=True)
    # Комбайн: самое большое укрытие двора, корпус высокий, жатка спереди низкая.
    ax, ay = polar(148.0, 3700.0)
    yaw = facing_center(ax, ay) + 70.0
    L.box("Combine_Body", ax, ay, 60.0, 700.0, 300.0, 300.0, yaw=yaw, mat="tractor", cover=True)
    L.lbox("Combine_Cab", ax, ay, yaw, 150.0, 0.0, 360.0, 200.0, 200.0, 180.0, mat="tractor")
    L.lbox("Combine_Header", ax, ay, yaw, 470.0, 0.0, 0.0, 180.0, 700.0, 110.0, mat="metal", cover=True)
    # Стенка из тюков у вышки: два ряда в высоту, с проломом посередине.
    ax, ay = polar(186.0, 3000.0)
    yaw = facing_center(ax, ay) + 90.0
    for i, v in enumerate((-420.0, -180.0, 180.0, 420.0)):
        L.lbox("BaleWall_{}".format(i), ax, ay, yaw, 0.0, v, 0.0, 120.0, 240.0, CROUCH, mat="hay", cover=True)
        if abs(v) > 300.0:
            L.lbox("BaleWall_{}_Top".format(i), ax, ay, yaw, 0.0, v, CROUCH, 120.0, 240.0, CROUCH, mat="hay", cover=True)
    # Теплица у ветряка: низкие стенки по периметру (сидя за ними прячутся) и стойки каркаса.
    ax, ay = polar(318.0, 4200.0)
    yaw = facing_center(ax, ay)
    L.walls("Greenhouse", ax, ay, yaw, 500.0, 900.0, 100.0, t=20.0,
            gaps={"front": [(350.0, 550.0, 0.0, 100.0)], "back": [(350.0, 550.0, 0.0, 100.0)]}, mat="concrete")
    for i, (u, v) in enumerate(((-250.0, -450.0), (-250.0, 450.0), (250.0, -450.0), (250.0, 450.0), (0.0, -450.0), (0.0, 450.0))):
        L.lbox("Greenhouse_Post_{}".format(i), ax, ay, yaw, u, v, 0.0, 12.0, 12.0, 280.0, mat="metal")


def fence(L):
    """Штакетник по краю плато, кроме пандусов: читается как граница фермы и не даёт упасть с обрыва."""
    step = 6.0
    a = step * 0.5
    n = 0
    while a < 360.0:
        in_ramp = any(abs((a - b + 180.0) % 360.0 - 180.0) < 7.0 for b in RAMP_TOP_BEARING.values())
        r = hg_edge_r(a) - 250.0
        if not in_ramp:
            x, y = polar(a, r)
            L.box("Fence_{:03d}".format(int(a * 10)), x, y, 0.0, 2.0 * r * math.sin(math.radians(step * 0.5)) + 8.0,
                  12.0, 110.0, yaw=a + 90.0, mat="fence")
            n += 1
        a += step


def build():
    L = Layout()
    house(L)
    barn(L)
    water_tower(L)
    silo_and_sheds(L)
    front_yard(L)
    anchors(L)
    barricades(L)
    inner_ring(L)
    rear_yard(L)
    fence(L)
    return L.items


# ==================== проверки ====================

def footprint(it):
    """Радиус, который предмет занимает на земле (грубо, по диагонали)."""
    if it["kind"] == "box":
        return 0.5 * math.hypot(it["sx"], it["sy"])
    if it["lying_yaw"] is not None:
        return 0.5 * math.hypot(it["d"], it["h"])
    return it["d"] * 0.5


def check(items):
    """Проходы по линиям, место под трон, площадки T4 и край плато свободны. Возвращает список проблем."""
    bad = []
    for it in items:
        if it["label"].startswith("Fence_") or it["z0"] > 50.0:
            continue
        x, y, fp = it["x"], it["y"], footprint(it)
        r = math.hypot(x, y)
        if r - fp < CENTER_CLEAR_R:
            bad.append("{}: в месте под трон (r {:.0f})".format(it["label"], r))
        if r + fp > hg_edge_r(math.degrees(math.atan2(y, x)) % 360.0) - 300.0:
            bad.append("{}: у края плато (r {:.0f})".format(it["label"], r))
        for name, b in RAMP_TOP_BEARING.items():
            a = math.radians(b)
            along = x * math.cos(a) + y * math.sin(a)
            lat = abs(-x * math.sin(a) + y * math.cos(a))
            half = CORRIDOR_HALF_IN if along < 2500.0 else CORRIDOR_HALF_OUT
            if along > 0.0 and lat - fp < half and not it["label"].startswith(("Jersey_", "Pallets_", "Tires_")):
                bad.append("{}: в проходе линии {} (сбоку {:.0f})".format(it["label"], name, lat))
        for pr, pb in T4_PADS:
            px, py = polar(pb, pr)
            if math.hypot(x - px, y - py) - fp < T4_CLEAR:
                bad.append("{}: на площадке T4".format(it["label"]))
    return bad


def preview(items, path, px=4.0):
    """Картинка сверху, 1 пиксель = px см... то есть px uu. Север вверх, восток вправо."""
    import numpy as np
    import struct
    import zlib
    R = PLATEAU_R + HG_EDGE_AMP + 500.0
    n = int(2 * R / px / 10)   # 10 uu на пиксель при px=1
    s = 2 * R / n
    img = np.full((n, n, 3), 40, dtype=np.float64)
    yy, xx = np.mgrid[0:n, 0:n]
    wx = R - (yy + 0.5) * s       # строки: север сверху
    wy = -R + (xx + 0.5) * s      # столбцы: восток вправо
    rr = np.hypot(wx, wy)
    edge = hg_edge_r(np.degrees(np.arctan2(wy, wx)) % 360.0, sin=np.sin)
    img[rr <= edge] = (95, 95, 80)
    for b in RAMP_TOP_BEARING.values():
        a = math.radians(b)
        along = wx * math.cos(a) + wy * math.sin(a)
        lat = np.abs(-wx * math.sin(a) + wy * math.cos(a))
        m = (along > 0) & (lat < 700.0)
        img[m] = (150, 120, 75)
    img[rr < CENTER_CLEAR_R] = (70, 130, 70)
    colors = {"wood": (140, 95, 55), "house": (220, 215, 200), "barn": (160, 40, 35), "roof": (90, 90, 95),
              "metal": (150, 160, 170), "hay": (215, 185, 80), "truck": (60, 90, 150), "tractor": (60, 150, 60),
              "tire": (25, 25, 25), "fuel": (200, 200, 60), "concrete": (180, 180, 180), "sacks": (190, 170, 130),
              "fence": (240, 240, 240)}
    for it in sorted(items, key=lambda i: i["z0"]):
        c = colors.get(it["mat"], (255, 0, 255))
        if it["kind"] == "box":
            a = math.radians(it["yaw"])
            du = (wx - it["x"]) * math.cos(a) + (wy - it["y"]) * math.sin(a)
            dv = -(wx - it["x"]) * math.sin(a) + (wy - it["y"]) * math.cos(a)
            su = it["sx"] * math.cos(math.radians(it.get("pitch", 0.0)))
            m = (np.abs(du) <= su * 0.5) & (np.abs(dv) <= it["sy"] * 0.5)
        elif it["lying_yaw"] is None:
            m = np.hypot(wx - it["x"], wy - it["y"]) <= it["d"] * 0.5
        else:
            a = math.radians(it["lying_yaw"])
            du = (wx - it["x"]) * math.cos(a) + (wy - it["y"]) * math.sin(a)
            dv = -(wx - it["x"]) * math.sin(a) + (wy - it["y"]) * math.cos(a)
            m = (np.abs(du) <= it["h"] * 0.5) & (np.abs(dv) <= it["d"] * 0.5)
        if it["cover"] and it["kind"] == "box" and it["sz"] <= CROUCH + 1.0 and it["z0"] < 1.0:
            c = tuple(min(255, int(v * 0.8)) for v in c)
        img[m] = c
    for pr, pb in T4_PADS:
        px_, py_ = polar(pb, pr)
        img[np.hypot(wx - px_, wy - py_) <= 300.0] = (255, 230, 40)
    raw = b"".join(b"\x00" + img[r].clip(0, 255).astype(np.uint8).tobytes() for r in range(n))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", n, n, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    with open(path, "wb") as fh:
        fh.write(png)


if __name__ == "__main__":
    import os
    import sys
    items = build()
    bad = check(items)
    print("[BASE] {} примитивов, {} из них укрытия".format(len(items), sum(1 for i in items if i["cover"])))
    for b in bad:
        print("[BASE] FAIL " + b)
    preview(items, os.path.join(os.path.dirname(os.path.abspath(__file__)), "base_preview.png"))
    print("[BASE] WROTE base_preview.png")
    sys.exit(1 if bad else 0)
