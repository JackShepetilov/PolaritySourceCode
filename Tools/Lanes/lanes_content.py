"""Наполнение линий и леса между ними: посёлок, поля, лес. Чистый питон, без unreal: раскладку можно
проверить и нарисовать вне редактора (python lanes_content.py), а build_lanes.step_lanes ставит её.

Лор (автор 2026-09-30): вся карта это посёлок и земля вокруг, игроку принадлежит только ферма на
холме на отшибе. Три линии это три пути из посёлка к ферме:
    Mid  центральная улица. От фермы: окраина (заправка, щит, пустыри), дальше дома в обе стороны,
         ближе к реке центр посёлка (магазины, закусочная, церковь, водонапорная башня)
    Top  просёлочная дорога через поля: столбы линии электропередачи, заборы из проволоки, выкупленные
         соседские фермы, элеватор у реки, крытый мост на броде
    Bot  тропинка через лес вдоль старой железной дороги: насыпь с рельсами, вагон, охотничья вышка,
         кладбище, стоянка, рыбацкий сарай у ручья
Между Top и Mid задворки посёлка (сараи, заборы, брошенные машины, огороды, бассейны). Между Mid и Bot
и южнее тропы природа: лес с полянами и валунами.

Только наша половина карты (X + Y < 0): вражеская это второй акт.

Каждый предмет: box / cyl / sphere в мировых координатах, z0 от земли под «якорем» группы (дом
ставится на одну отметку целиком, иначе стены разъезжаются по склону). ism=True: массовое (деревья,
столбики), editor ставит такие одним актором с экземплярами.
"""

import json
import math
import os
import random

import base_layout as bl

HERE = os.path.dirname(os.path.abspath(__file__))
SEED = 23

CROUCH = bl.CROUCH
HILL_CLEAR = 14500.0      # от центра базы: холм и подъёмы
EDGE_CLEAR = 5500.0       # от края карты: там подъём к границе
RIVER_CLEAR = 1800.0
CAMP_CLEAR = 600.0        # сверх радиуса места под кемп: места под кемпы остаются пустыми


class LaneLayout(bl.Layout):
    """Раскладка base_layout плюс якорь земли, массовые экземпляры и сферы."""

    def __init__(self):
        super().__init__()
        self.anchor = None

    def _tag(self, it, ism):
        it["ism"] = ism
        if self.anchor is not None:
            it["ax"], it["ay"] = self.anchor
        return it

    def box(self, label, x, y, z0, sx, sy, sz, yaw=0.0, mat="wood", pitch=0.0, cover=False, ism=False):
        super().box(label, x, y, z0, sx, sy, sz, yaw=yaw, mat=mat, pitch=pitch, cover=cover)
        self._tag(self.items[-1], ism)

    def cyl(self, label, x, y, z0, d, h, mat="hay", lying_yaw=None, cover=False, ism=False):
        super().cyl(label, x, y, z0, d, h, mat=mat, lying_yaw=lying_yaw, cover=cover)
        self._tag(self.items[-1], ism)

    def sphere(self, label, x, y, z0, d, h=None, mat="leaf", ism=True, collision=False):
        self.items.append(self._tag({"kind": "sphere", "label": label, "x": x, "y": y, "z0": z0, "d": d,
                                     "h": h or d, "mat": mat, "cover": False, "collision": collision}, ism))


# ==================== геометрия карты ====================

class Map:
    def __init__(self, lay):
        self.lay = lay
        self.c = lay["corner"]
        self.half = lay["half_extent"]
        self.full = {k: [(p[0], p[1]) for p in v] for k, v in lay["lanes_full"].items()}
        self.width = lay.get("lane_width", {})
        self.slots = lay["poi_slots"]
        self.pads = lay["turret_pads"]
        self.cells = {}
        self.cell = 1000.0

    # ---- вдоль линии ----
    def cross(self, lane):
        """Путь от центра базы до реки по линии."""
        pts = self.full[lane]
        acc = 0.0
        for (x0, y0), (x1, y1) in zip(pts[:-1], pts[1:]):
            s0, s1 = x0 + y0, x1 + y1
            L = math.hypot(x1 - x0, y1 - y0)
            if (s0 <= 0.0 <= s1) or (s1 <= 0.0 <= s0):
                return acc + L * (s0 / (s0 - s1) if s0 != s1 else 0.0)
            acc += L
        return acc

    def frame(self, lane, s):
        """Точка на оси линии, направление и нормаль влево (от направления против часовой)."""
        pts = self.full[lane]
        acc = 0.0
        for (x0, y0), (x1, y1) in zip(pts[:-1], pts[1:]):
            L = math.hypot(x1 - x0, y1 - y0)
            if s <= acc + L:
                t = (s - acc) / L
                ux, uy = (x1 - x0) / L, (y1 - y0) / L
                return (x0 + (x1 - x0) * t, y0 + (y1 - y0) * t), (ux, uy), (-uy, ux)
            acc += L
        (x0, y0), (x1, y1) = pts[-2], pts[-1]
        L = math.hypot(x1 - x0, y1 - y0)
        return (x1, y1), ((x1 - x0) / L, (y1 - y0) / L), (-(y1 - y0) / L, (x1 - x0) / L)

    def at(self, lane, s, lateral):
        """Точка сбоку от линии: lateral > 0 влево, < 0 вправо. И азимут линии."""
        (x, y), (ux, uy), (nx, ny) = self.frame(lane, s)
        return x + nx * lateral, y + ny * lateral, math.degrees(math.atan2(uy, ux))

    # ---- свободно ли место ----
    def lane_dist(self, x, y):
        best = 1e18
        for name, pts in self.full.items():
            half = self.width.get(name, 1400.0) * 0.5
            for (x0, y0), (x1, y1) in zip(pts[:-1], pts[1:]):
                vx, vy = x1 - x0, y1 - y0
                t = max(0.0, min(1.0, ((x - x0) * vx + (y - y0) * vy) / max(vx * vx + vy * vy, 1.0)))
                best = min(best, math.hypot(x - x0 - t * vx, y - y0 - t * vy) - half)
        return best

    def terrain_ok(self, x, y, r, lane_margin=300.0):
        c = self.c
        if x + y > -RIVER_CLEAR * math.sqrt(2.0):
            return False                                   # наша половина и не в реке
        if min(math.hypot(x + c, y + c), math.hypot(x - c, y - c)) < HILL_CLEAR + r:
            return False
        if self.half - max(abs(x), abs(y)) < EDGE_CLEAR + r:
            return False
        for sl in self.slots:
            if math.hypot(x - sl["x"], y - sl["y"]) < sl["r"] + CAMP_CLEAR + r:
                return False
            # Подъём на холм тяжёлого кемпа тоже свободен: по нему заходят и охрана, и игрок.
            kn = sl.get("knoll")
            if kn:
                (x0, y0), (x1, y1) = kn["ramp"]
                vx, vy = x1 - x0, y1 - y0
                t = max(0.0, min(1.0, ((x - x0) * vx + (y - y0) * vy) / max(vx * vx + vy * vy, 1.0)))
                if math.hypot(x - x0 - t * vx, y - y0 - t * vy) < kn["ramp_half"] + 800.0 + r:
                    return False
        for p in self.pads:
            if math.hypot(x - p["x"], y - p["y"]) < 900.0 + r:
                return False
        return self.lane_dist(x, y) >= lane_margin + r

    def take(self, x, y, r, spacing=0.0):
        """Занять круг, если не пересекает занятое (с зазором spacing). True = занято нами."""
        cx, cy = int(x // self.cell), int(y // self.cell)
        reach = int((r + spacing) // self.cell) + 2
        for ix in range(cx - reach, cx + reach + 1):
            for iy in range(cy - reach, cy + reach + 1):
                for (ox, oy, orr) in self.cells.get((ix, iy), ()):
                    if math.hypot(x - ox, y - oy) < r + orr + spacing:
                        return False
        self.cells.setdefault((cx, cy), []).append((x, y, r))
        return True

    def place(self, x, y, r, spacing=0.0, lane_margin=300.0):
        return self.terrain_ok(x, y, r, lane_margin) and self.take(x, y, r, spacing)


# ==================== постройки и предметы ====================

def house(L, M, x, y, yaw, rng, name, enterable=False, floors=1):
    """Дом фасадом на yaw. Двухэтажный выше и на него лезут хуком; некоторые с проходом насквозь."""
    D, W = rng.uniform(900.0, 1100.0), rng.uniform(900.0, 1200.0)
    H = 320.0 * floors
    if not M.place(x, y, 0.5 * math.hypot(D, W) + 100.0, spacing=300.0):
        return False
    L.anchor = (x, y)
    gaps = {"front": [(W * 0.5 - 100.0, W * 0.5 + 100.0, 0.0, 230.0) if enterable else (W * 0.5 - 100.0, W * 0.5 + 100.0, 0.0, 0.0)],
            "left": [(250.0, 450.0, 100.0, 220.0)], "right": [(D - 450.0, D - 250.0, 100.0, 220.0)]}
    if enterable:
        gaps["back"] = [(W * 0.5 - 100.0, W * 0.5 + 100.0, 0.0, 230.0)]
    if floors > 1:
        for side in ("left", "right"):
            gaps[side].append((400.0, 600.0, 420.0, 540.0))
    L.walls(name, x, y, yaw, D, W, H, gaps=gaps, mat=rng.choice(("house", "siding_blue", "siding_yellow")))
    L.lbox(name + "_Plinth", x, y, yaw, 0.0, 0.0, -150.0, D + 40.0, W + 40.0, 150.0, mat="concrete")
    # Двускатная крыша: две наклонные плиты.
    pitch = 28.0
    run = W * 0.5 + 40.0
    rise = run * math.tan(math.radians(pitch))
    for side in (-1.0, 1.0):
        rx, ry = L.local(x, y, yaw, 0.0, side * run * 0.5)
        L.box(name + "_Roof{}".format("L" if side < 0 else "R"), rx, ry, H + rise * 0.5 - 20.0,
              run / math.cos(math.radians(pitch)) + 10.0, D + 60.0, 20.0, yaw=yaw + 90.0 * side, pitch=pitch, mat="roof")
        L.items[-1]["yaw"] = yaw - 90.0 if side > 0 else yaw + 90.0
    if floors > 1:
        L.lbox(name + "_Floor2", x, y, yaw, 0.0, 0.0, 320.0, D - 60.0, W - 60.0, 20.0, mat="wood")
    # Крыльцо с навесом к улице.
    L.lbox(name + "_Porch", x, y, yaw, D * 0.5 + 150.0, 0.0, 0.0, 300.0, W * 0.6, 40.0, mat="wood")
    L.lbox(name + "_PorchRoof", x, y, yaw, D * 0.5 + 150.0, 0.0, 270.0, 300.0, W * 0.6, 15.0, mat="roof")
    for i, v in enumerate((-W * 0.28, W * 0.28)):
        L.lbox(name + "_PorchPost_{}".format(i), x, y, yaw, D * 0.5 + 280.0, v, 0.0, 15.0, 15.0, 270.0, mat="wood")
    L.anchor = None
    return True


def shop(L, M, x, y, yaw, rng, name, mat="brick"):
    D, W, H = rng.uniform(1200.0, 1500.0), rng.uniform(1200.0, 1800.0), rng.uniform(450.0, 550.0)
    if not M.place(x, y, 0.5 * math.hypot(D, W) + 100.0, spacing=200.0):
        return False
    L.anchor = (x, y)
    L.walls(name, x, y, yaw, D, W, H, gaps={
        "front": [(W * 0.15, W * 0.45, 80.0, 300.0), (W * 0.5 - 100.0, W * 0.5 + 100.0, 0.0, 250.0), (W * 0.55, W * 0.85, 80.0, 300.0)],
        "back": [(W * 0.5 - 100.0, W * 0.5 + 100.0, 0.0, 250.0)]}, mat=mat)
    L.lbox(name + "_Plinth", x, y, yaw, 0.0, 0.0, -150.0, D + 40.0, W + 40.0, 150.0, mat="concrete")
    L.lbox(name + "_Roof", x, y, yaw, 0.0, 0.0, H, D + 40.0, W + 40.0, 30.0, mat="roof")
    # Парапет с фальшфасадом на улицу: крыша магазина это огневая точка над улицей.
    L.lbox(name + "_FalseFront", x, y, yaw, D * 0.5 + 10.0, 0.0, H, 30.0, W + 40.0, 200.0, mat=mat)
    L.lbox(name + "_ParapetBack", x, y, yaw, -D * 0.5, 0.0, H + 30.0, 20.0, W, 90.0, mat=mat)
    L.lbox(name + "_Awning", x, y, yaw, D * 0.5 + 170.0, 0.0, 300.0, 300.0, W * 0.8, 15.0, mat="awning")
    # Лестница на крышу сзади.
    L.lbox(name + "_Ladder", x, y, yaw, -D * 0.5 - 30.0, W * 0.35, 0.0, 20.0, 60.0, H + 60.0, mat="metal")
    L.anchor = None
    return True


def car(L, M, x, y, yaw, rng, name, lane_margin=-10000.0):
    """Брошенная машина: кузов по пояс (сидя за ним прячутся), кабина выше."""
    if not (M.take(x, y, 280.0, spacing=100.0)):
        return False
    L.anchor = (x, y)
    kind = rng.random()
    mat = rng.choice(("truck", "car_red", "car_white", "car_green"))
    if kind < 0.35:   # пикап
        L.box(name + "_Body", x, y, 20.0, 540.0, 210.0, 100.0, yaw=yaw, mat=mat, cover=True)
        L.lbox(name + "_Cab", x, y, yaw, 100.0, 0.0, 120.0, 200.0, 200.0, 80.0, mat=mat)
    else:             # легковушка
        L.box(name + "_Body", x, y, 20.0, 460.0, 190.0, 90.0, yaw=yaw, mat=mat, cover=True)
        L.lbox(name + "_Cab", x, y, yaw, -20.0, 0.0, 110.0, 240.0, 180.0, 60.0, mat=mat)
    L.anchor = None
    return True


def streetlight(L, x, y, yaw, name):
    L.box(name, x, y, 0.0, 25.0, 25.0, 700.0, mat="metal")
    L.lbox(name + "_Arm", x, y, yaw, 0.0, -120.0, 680.0, 20.0, 260.0, 15.0, mat="metal")


def power_pole(L, x, y, yaw, name):
    L.box(name, x, y, 0.0, 30.0, 30.0, 950.0, mat="wood")
    L.box(name + "_Arm", x, y, 860.0, 25.0, 280.0, 25.0, yaw=yaw, mat="wood")


def tree(L, x, y, rng, name, big=1.0):
    h = rng.uniform(600.0, 1100.0) * big
    L.cyl(name + "_T", x, y, 0.0, rng.uniform(35.0, 70.0) * big, h, mat="trunk", ism=True)
    cd = rng.uniform(350.0, 600.0) * big
    L.sphere(name + "_C", x, y, h - cd * 0.35, cd, cd * rng.uniform(0.9, 1.3), mat=rng.choice(("leaf", "leaf_dark")))


def pine(L, x, y, rng, name):
    h = rng.uniform(900.0, 1500.0)
    L.cyl(name + "_T", x, y, 0.0, 50.0, h, mat="trunk", ism=True)
    for k, (f, d) in enumerate(((0.35, 420.0), (0.55, 320.0), (0.75, 210.0))):
        L.sphere(name + "_C{}".format(k), x, y, h * f, d, d * 0.8, mat="pine")


def farmstead(L, M, x, y, yaw, rng, name, ruined=False):
    """Выкупленная соседская ферма: дом, сарай, силос, техника во дворе."""
    if not M.place(x, y, 2600.0, spacing=500.0, lane_margin=800.0):
        return False
    M.cells  # занято
    hx, hy = L.local(x, y, yaw, -300.0, -1200.0)
    L.anchor = (x, y)
    L.walls(name + "_House", hx, hy, yaw, 900.0, 1000.0, 320.0,
            gaps={"front": [(400.0, 600.0, 0.0, 230.0)], "back": [(400.0, 600.0, 0.0, 230.0)],
                  "left": [(300.0, 500.0, 100.0, 220.0)]}, mat="house")
    L.lbox(name + "_HouseRoof", hx, hy, yaw, 0.0, 0.0, 320.0, 960.0, 1060.0, 25.0, mat="roof")
    bx, by = L.local(x, y, yaw, -200.0, 900.0)
    if ruined:
        # Сарай наполовину сложился: стены стоят, крыша съехала одной плитой.
        L.walls(name + "_Barn", bx, by, yaw, 1100.0, 1500.0, 600.0, gaps={"front": [(400.0, 1100.0, 0.0, 600.0)],
                                                                           "right": [(0.0, 700.0, 0.0, 600.0)]}, mat="barn")
        L.lbox(name + "_BarnRoofFallen", bx, by, yaw, 100.0, 200.0, 250.0, 1000.0, 1300.0, 25.0, mat="roof")
        L.items[-1]["pitch"] = 22.0
    else:
        L.walls(name + "_Barn", bx, by, yaw, 1100.0, 1500.0, 600.0, gaps={"front": [(450.0, 1050.0, 0.0, 450.0)]}, mat="barn")
        L.lbox(name + "_BarnRoof", bx, by, yaw, 0.0, 0.0, 600.0, 1160.0, 1560.0, 25.0, mat="roof")
    sx, sy = L.local(x, y, yaw, -900.0, 2100.0)
    L.cyl(name + "_Silo", sx, sy, 0.0, 450.0, 1000.0, mat="metal")
    L.anchor = None
    for k in range(3):
        u, v = rng.uniform(300.0, 1000.0), rng.uniform(-600.0, 600.0)
        tx, ty = L.local(x, y, yaw, u, v)
        L.cyl(name + "_Hay_{}".format(k), tx, ty, 0.0, 180.0, 160.0, lying_yaw=rng.uniform(0, 180), mat="hay", cover=True)
    return True


# ==================== Mid: центральная улица ====================

def mid_street(L, M, rng):
    lane = "Mid"
    s_end = M.cross(lane) - 2500.0
    half = M.width.get(lane, 1400.0) * 0.5
    # Тротуары и бордюры: улица читается как улица.
    s = 13000.0
    while s < s_end:
        for side in (-1.0, 1.0):
            x, y, b = M.at(lane, s + 750.0, side * (half + 150.0))
            L.box("Mid_Sidewalk_{:05d}_{}".format(int(s), int(side)), x, y, 0.0, 1520.0, 300.0, 15.0, yaw=b, mat="concrete")
        s += 1500.0
    # Фонари вдоль улицы, через один с каждой стороны: вертикаль для хука по всей улице.
    s, k = 14000.0, 0
    while s < s_end:
        side = 1.0 if k % 2 == 0 else -1.0
        x, y, b = M.at(lane, s, side * (half + 250.0))
        streetlight(L, x, y, b + (90.0 if side > 0 else -90.0), "Mid_Light_{:02d}".format(k))
        s += 3200.0
        k += 1
    # Окраина: заправка, щит, указатель, пустыри.
    x, y, b = M.at(lane, 19000.0, -(half + 1400.0))
    if M.place(x, y, 1100.0, spacing=200.0):
        L.anchor = (x, y)
        L.lbox("Mid_GasCanopy", x, y, b, 0.0, 0.0, 450.0, 1100.0, 1600.0, 40.0, mat="awning")
        for i, (u, v) in enumerate(((-450.0, -700.0), (-450.0, 700.0), (450.0, -700.0), (450.0, 700.0))):
            L.lbox("Mid_GasPost_{}".format(i), x, y, b, u, v, 0.0, 30.0, 30.0, 450.0, mat="metal")
        for i, v in enumerate((-350.0, 350.0)):
            L.lbox("Mid_GasPump_{}".format(i), x, y, b, 0.0, v, 0.0, 120.0, 70.0, 170.0, mat="fuel", cover=True)
        L.anchor = None
        kx, ky, _ = M.at(lane, 19000.0, -(half + 3000.0))
        shop(L, M, kx, ky, b + 90.0, rng, "Mid_GasKiosk", mat="house")
    x, y, b = M.at(lane, 16500.0, half + 900.0)
    if M.place(x, y, 400.0):
        L.box("Mid_Billboard_PostA", x, y, 0.0, 40.0, 40.0, 600.0, mat="metal")
        bx2, by2 = L.local(x, y, b, 0.0, 500.0)
        L.box("Mid_Billboard_PostB", bx2, by2, 0.0, 40.0, 40.0, 600.0, mat="metal")
        cx, cy = L.local(x, y, b, 0.0, 250.0)
        L.box("Mid_Billboard", cx, cy, 380.0, 20.0, 700.0, 300.0, yaw=b, mat="awning")
    x, y, b = M.at(lane, 13500.0, half + 500.0)
    L.box("Mid_TownSign", x, y, 0.0, 30.0, 220.0, 180.0, yaw=b, mat="wood")
    # Дома: чем ближе к центру посёлка, тем плотнее. Центр у реки: магазины.
    s, k = 22000.0, 0
    while s < s_end:
        core = s > s_end - 13000.0
        for side in (-1.0, 1.0):
            if rng.random() < (0.15 if core else 0.3):
                continue
            lat = side * (half + 1300.0 + rng.uniform(0.0, 300.0))
            x, y, b = M.at(lane, s, lat)
            face = b + (-90.0 if side > 0 else 90.0)   # фасадом на улицу
            if core and rng.random() < 0.7:
                shop(L, M, *M.at(lane, s, side * (half + 1400.0))[:2], face, rng, "Mid_Shop_{:02d}_{}".format(k, int(side)),
                     mat=rng.choice(("brick", "brick_dark", "house")))
            else:
                ok = house(L, M, x, y, face, rng, "Mid_House_{:02d}_{}".format(k, int(side)),
                           enterable=(k % 3 == 0), floors=2 if rng.random() < 0.35 else 1)
                if ok:
                    # Штакетник вдоль тротуара с калиткой.
                    for part, (v0, v1) in enumerate(((-650.0, -120.0), (120.0, 650.0))):
                        fx, fy, _ = M.at(lane, s + (v0 + v1) * 0.5, side * (half + 500.0))
                        L.box("Mid_Picket_{:02d}_{}_{}".format(k, int(side), part), fx, fy, 0.0, v1 - v0, 10.0, 100.0,
                              yaw=b, mat="fence", cover=True)
        s += rng.uniform(2600.0, 3300.0)
        k += 1
    # Церковь со шпилем и водонапорная башня посёлка: ориентиры центра, высокие якоря.
    x, y, b = M.at(lane, s_end - 9000.0, half + 2600.0)
    if M.place(x, y, 1300.0, spacing=300.0):
        L.anchor = (x, y)
        L.walls("Mid_Church", x, y, b - 90.0, 1600.0, 1000.0, 700.0, gaps={"front": [(400.0, 600.0, 0.0, 350.0)],
                                                                          "left": [(400.0, 1200.0, 200.0, 550.0)]}, mat="house")
        L.lbox("Mid_ChurchRoof", x, y, b - 90.0, 0.0, 0.0, 700.0, 1660.0, 1060.0, 30.0, mat="roof")
        tx, ty = L.local(x, y, b - 90.0, 800.0 + 200.0, 0.0)
        L.box("Mid_Steeple", tx, ty, 0.0, 400.0, 400.0, 1500.0, yaw=b, mat="house")
        L.box("Mid_SteepleTop", tx, ty, 1500.0, 200.0, 200.0, 400.0, yaw=b, mat="roof")
        L.anchor = None
    x, y, b = M.at(lane, s_end - 4000.0, -(half + 3200.0))
    if M.place(x, y, 700.0, spacing=200.0):
        for i, (du, dv) in enumerate(((-300.0, -300.0), (-300.0, 300.0), (300.0, -300.0), (300.0, 300.0))):
            L.box("Mid_WaterTower_Leg_{}".format(i), x + du, y + dv, 0.0, 50.0, 50.0, 1800.0, mat="metal")
        L.box("Mid_WaterTower_Deck", x, y, 1800.0, 900.0, 900.0, 25.0, mat="metal")
        L.cyl("Mid_WaterTower_Tank", x, y, 1825.0, 750.0, 600.0, mat="metal")
    # Брошенные машины на улице и у обочин: укрытия в уличном бою, ось улицы свободна.
    s, k = 15000.0, 0
    while s < s_end:
        side = rng.choice((-1.0, 1.0))
        x, y, b = M.at(lane, s, side * rng.uniform(half - 250.0, half + 100.0))
        car(L, M, x, y, b + rng.uniform(-25.0, 25.0), rng, "Mid_Car_{:02d}".format(k))
        s += rng.uniform(2200.0, 4200.0)
        k += 1
    # Школьный автобус поперёк части улицы у центра: большое укрытие, проход рядом.
    x, y, b = M.at(lane, s_end - 6000.0, half - 200.0)
    if M.take(x, y, 700.0):
        L.anchor = (x, y)
        L.box("Mid_Bus", x, y, 30.0, 1100.0, 250.0, 280.0, yaw=b + 35.0, mat="bus", cover=True)
        L.anchor = None


# ==================== Top: просёлочная дорога ====================

def top_road(L, M, rng):
    lane = "Top"
    s_end = M.cross(lane) - 2000.0
    half = M.width.get(lane, 900.0) * 0.5
    # Столбы ЛЭП по левой обочине и провода между ними.
    s, k, prev = 13000.0, 0, None
    while s < s_end:
        x, y, b = M.at(lane, s, half + 700.0)
        power_pole(L, x, y, b + 90.0, "Top_Pole_{:02d}".format(k))
        if prev:
            px, py = prev
            L_ = math.hypot(x - px, y - py)
            L.box("Top_Wire_{:02d}".format(k), (x + px) * 0.5, (y + py) * 0.5, 900.0, L_, 4.0, 4.0,
                  yaw=math.degrees(math.atan2(y - py, x - px)), mat="tire")
            L.items[-1]["collision"] = False
        prev = (x, y)
        s += 4000.0
        k += 1
    # Проволочные заборы полей с обеих сторон: столбики экземплярами, проволока без коллизии.
    for side in (-1.0, 1.0):
        s = 12500.0
        while s < s_end:
            x, y, b = M.at(lane, s, side * (half + 350.0))
            L.box("Top_FencePost", x, y, 0.0, 12.0, 12.0, 120.0, mat="wood", ism=True)
            s += 500.0
        s = 12500.0
        while s < s_end - 2500.0:
            x, y, b = M.at(lane, s + 1250.0, side * (half + 350.0))
            for h in (50.0, 100.0):
                L.box("Top_FenceWire", x, y, h, 2500.0, 3.0, 3.0, yaw=b, mat="metal", ism=True)
                L.items[-1]["collision"] = False
            s += 2500.0
    # Выкупленные соседские фермы: две слева (к задворкам), одна справа, одна полуразрушенная.
    for i, (s, lat, ruined) in enumerate(((0.35, 4500.0, False), (0.6, -4800.0, False), (0.8, 4200.0, True))):
        x, y, b = M.at(lane, s * s_end, lat)
        farmstead(L, M, x, y, b + (-90.0 if lat > 0 else 90.0), rng, "Top_Farm_{}".format(i), ruined=ruined)
    # Элеватор у реки: ориентир всей западной стороны, высокий якорь.
    x, y, b = M.at(lane, s_end - 3500.0, -(half + 2500.0))
    if M.place(x, y, 1600.0, spacing=300.0, lane_margin=500.0):
        L.box("Top_Elevator", x, y, 0.0, 700.0, 700.0, 2600.0, yaw=b, mat="concrete")
        L.box("Top_ElevatorHead", x, y, 2600.0, 500.0, 900.0, 400.0, yaw=b, mat="metal")
        for i in range(3):
            bx_, by_ = L.local(x, y, b, -900.0 - i * 800.0, 0.0)
            L.cyl("Top_Bin_{}".format(i), bx_, by_, 0.0, 700.0, 1100.0, mat="metal")
    # Поля: рулоны сена, брошенная техника.
    for i in range(40):
        s = rng.uniform(14000.0, s_end)
        lat = rng.choice((-1.0, 1.0)) * rng.uniform(half + 900.0, half + 6000.0)
        x, y, b = M.at(lane, s, lat)
        if M.place(x, y, 150.0, spacing=500.0, lane_margin=400.0):
            L.cyl("Top_HayRoll_{:02d}".format(i), x, y, 0.0, 180.0, 160.0, lying_yaw=rng.uniform(0, 180), mat="hay", cover=True)
    for i, (s, lat) in enumerate(((0.25, 1800.0), (0.5, -2000.0), (0.7, 2200.0))):
        x, y, b = M.at(lane, s * s_end, lat)
        if M.place(x, y, 450.0, spacing=200.0):
            L.anchor = (x, y)
            L.box("Top_Tractor_{}".format(i), x, y, 40.0, 380.0, 190.0, 160.0, yaw=b + rng.uniform(-40, 40), mat="tractor", cover=True)
            L.box("Top_TractorCab_{}".format(i), x, y, 200.0, 150.0, 150.0, 130.0, yaw=b, mat="tractor")
            L.anchor = None


# ==================== Bot: тропинка через лес ====================

def bot_trail(L, M, rng):
    lane = "Bot"
    s_end = M.cross(lane) - 2000.0
    half = M.width.get(lane, 400.0) * 0.5
    # Старая железная дорога вдоль тропы, к югу (вправо от линии... у Bot влево это юг).
    rail_lat = half + 2200.0
    s = 14000.0
    while s < s_end:
        x, y, b = M.at(lane, s, rail_lat)
        L.box("Bot_Sleeper", x, y, 40.0, 60.0, 260.0, 18.0, yaw=b, mat="wood", ism=True)
        s += 80.0
    s = 14000.0
    while s < s_end:
        for off in (-72.0, 72.0):
            x, y, b = M.at(lane, s + 500.0, rail_lat + off)
            L.box("Bot_Rail", x, y, 58.0, 1000.0, 8.0, 14.0, yaw=b, mat="metal", ism=True)
        s += 1000.0
    s = 14000.0
    while s < s_end:
        x, y, b = M.at(lane, s + 500.0, rail_lat)
        L.box("Bot_Ballast", x, y, -20.0, 1000.0, 420.0, 60.0, yaw=b, mat="rock", ism=True)
        s += 1000.0
    for i in range(int((s_end - 14000.0) / 500.0)):
        M.take(*M.at(lane, 14000.0 + i * 500.0, rail_lat)[:2], 250.0)
    # Брошенный товарный вагон на путях: высокое укрытие, крыша это точка для хука.
    x, y, b = M.at(lane, s_end * 0.55, rail_lat)
    L.anchor = (x, y)
    L.box("Bot_Boxcar", x, y, 100.0, 1400.0, 300.0, 320.0, yaw=b, mat="car_red", cover=True)
    L.anchor = None
    # Кладбище у фермы: низкие надгробия (сидя прятаться) и ограда.
    x0, y0, b = M.at(lane, 19000.0, -(half + 2200.0))
    if M.place(x0, y0, 1100.0, spacing=200.0, lane_margin=400.0):
        for r_ in range(3):
            for c_ in range(5):
                gx, gy = L.local(x0, y0, b, -600.0 + c_ * 300.0, -400.0 + r_ * 400.0)
                L.box("Bot_Grave_{}_{}".format(r_, c_), gx, gy, 0.0, 60.0, 20.0, rng.uniform(70.0, 110.0), yaw=b, mat="concrete", cover=True)
        L.walls("Bot_CemeteryFence", x0, y0, b, 1500.0, 1300.0, 110.0, t=8.0,
                gaps={"front": [(550.0, 750.0, 0.0, 110.0)]}, mat="metal")
    # Охотничья вышка: площадка на 4 м с перилами.
    x, y, b = M.at(lane, s_end * 0.35, -(half + 900.0))
    if M.place(x, y, 250.0, lane_margin=300.0):
        for i, (du, dv) in enumerate(((-90.0, -90.0), (-90.0, 90.0), (90.0, -90.0), (90.0, 90.0))):
            L.box("Bot_DeerStand_Leg_{}".format(i), x + du, y + dv, 0.0, 15.0, 15.0, 400.0, mat="wood")
        L.box("Bot_DeerStand_Deck", x, y, 400.0, 220.0, 220.0, 15.0, mat="wood")
        L.box("Bot_DeerStand_Rail", x, y, 415.0, 220.0, 10.0, 100.0, yaw=b, mat="wood")
    # Стоянка: кострище, палатка, бревно-скамья.
    x, y, b = M.at(lane, s_end * 0.7, -(half + 700.0))
    if M.place(x, y, 400.0, lane_margin=200.0):
        L.cyl("Bot_FireRing", x, y, 0.0, 150.0, 30.0, mat="rock")
        tx, ty = L.local(x, y, b, 300.0, 0.0)
        L.box("Bot_Tent", tx, ty, 0.0, 250.0, 200.0, 130.0, yaw=b, mat="awning", cover=True)
        lx, ly = L.local(x, y, b, -250.0, 0.0)
        L.cyl("Bot_LogBench", lx, ly, 0.0, 50.0, 250.0, lying_yaw=b + 90.0, mat="trunk")
    # Рыбацкий сарай у ручья.
    x, y, b = M.at(lane, s_end - 800.0, half + 1500.0)
    if M.place(x, y, 500.0, lane_margin=300.0):
        L.anchor = (x, y)
        L.walls("Bot_FishShack", x, y, b, 400.0, 500.0, 260.0, gaps={"front": [(150.0, 300.0, 0.0, 210.0)]}, mat="wood")
        L.box("Bot_FishShackRoof", x, y, 260.0, 440.0, 540.0, 20.0, yaw=b, mat="roof")
        L.anchor = None
    # Поваленные деревья и валуны вдоль тропы: укрытия на обочине, тропа свободна.
    for i in range(36):
        s = rng.uniform(13500.0, s_end)
        lat = rng.choice((-1.0, 1.0)) * rng.uniform(half + 300.0, half + 1500.0)
        x, y, b = M.at(lane, s, lat)
        if not M.place(x, y, 250.0, spacing=200.0, lane_margin=150.0):
            continue
        if rng.random() < 0.5:
            L.cyl("Bot_Log_{:02d}".format(i), x, y, 0.0, rng.uniform(60.0, 100.0), rng.uniform(500.0, 900.0),
                  lying_yaw=b + rng.uniform(-40.0, 40.0), mat="trunk", cover=True)
        else:
            sz = rng.uniform(120.0, 260.0)
            L.box("Bot_Boulder_{:02d}".format(i), x, y, -sz * 0.3, sz * 1.4, sz * 1.1, sz, yaw=rng.uniform(0, 360),
                  pitch=rng.uniform(-12, 12), mat="rock", cover=True)


# ==================== между линиями ====================

def poisson(M, rng, region, spacing, tries):
    """Точки в регионе (функция x, y -> bool) не ближе spacing друг к другу."""
    xs = []
    lo, hi = -M.c - 8000.0, M.c + 8000.0
    for _ in range(tries):
        x, y = rng.uniform(lo, hi), rng.uniform(lo, hi)
        if region(x, y):
            xs.append((x, y))
    return xs


def jungle_top_backyards(L, M, rng):
    """Задворки посёлка между Top и Mid: сараи, заборы, машины, огороды, бассейны, собачьи будки."""
    c = M.c

    def region(x, y):
        # Между Top (ось x, y = -c) и Mid (диагональ) на нашей половине.
        return (y + c) > 0 and (y + c) < (x + c) and x + y < 0

    k = 0
    # Второй ряд домов за улицей: задворки это окраина посёлка, а не пустое поле.
    for i, (x, y) in enumerate(poisson(M, rng, region, 0.0, 700)):
        if M.lane_dist(x, y) < 6000.0 and rng.random() < 0.6:
            house(L, M, x, y, rng.uniform(0, 360), rng, "YardHouse_{:02d}".format(i), enterable=rng.random() < 0.5)
    for (x, y) in poisson(M, rng, region, 0.0, 4000):
        if not M.place(x, y, 450.0, spacing=1000.0, lane_margin=900.0):
            continue
        yaw = rng.uniform(0, 360)
        pick = rng.random()
        name = "Yard_{:03d}".format(k)
        L.anchor = (x, y)
        if pick < 0.25:        # сарай для инструмента
            L.walls(name + "_Shed", x, y, yaw, 300.0, 400.0, 250.0, gaps={"front": [(130.0, 270.0, 0.0, 200.0)]}, mat="wood")
            L.box(name + "_ShedRoof", x, y, 250.0, 340.0, 440.0, 15.0, yaw=yaw, mat="roof")
        elif pick < 0.45:      # кусок забора с воротами
            for part, v in enumerate((-350.0, 350.0)):
                fx, fy = L.local(x, y, yaw, 0.0, v)
                L.box(name + "_Fence_{}".format(part), fx, fy, 0.0, 500.0, 12.0, 180.0, yaw=yaw + 90.0, mat="fence")
        elif pick < 0.62:      # брошенная машина
            L.anchor = None
            car(L, M, x + 1.0, y + 1.0, yaw, rng, name)
        elif pick < 0.75:      # огород: грядки по колено, сидя за ними не спрятаться, зато видно
            for r_ in range(3):
                gx, gy = L.local(x, y, yaw, r_ * 160.0 - 160.0, 0.0)
                L.box(name + "_Bed_{}".format(r_), gx, gy, 0.0, 90.0, 400.0, 40.0, yaw=yaw, mat="soil")
        elif pick < 0.85:      # надувной бассейн: круглое низкое укрытие
            L.cyl(name + "_Pool", x, y, 0.0, 400.0, 110.0, mat="pool", cover=True)
        elif pick < 0.93:      # лодка на прицепе
            L.box(name + "_Boat", x, y, 60.0, 550.0, 200.0, 120.0, yaw=yaw, mat="car_white", cover=True)
        else:                  # будка и бочки
            L.box(name + "_DogHouse", x, y, 0.0, 120.0, 100.0, 110.0, yaw=yaw, mat="wood", cover=True)
            bx_, by_ = L.local(x, y, yaw, 250.0, 0.0)
            L.cyl(name + "_Barrel", bx_, by_, 0.0, 70.0, 100.0, mat="metal", cover=True)
        L.anchor = None
        k += 1
    # Деревья во дворах, редко.
    for i, (x, y) in enumerate(poisson(M, rng, region, 3500.0, 500)):
        if M.place(x, y, 300.0, spacing=2500.0, lane_margin=700.0):
            tree(L, x, y, rng, "YardTree_{:03d}".format(i))


def jungle_bot_nature(L, M, rng):
    """Природа между Mid и Bot и к югу от тропы: лес с полянами и валунами."""
    c = M.c

    def region(x, y):
        return (x + c) < (y + c) and x + y < 0

    # Поляны: круги без деревьев, чтобы лес не был однородным (П26: разная дальность видимости).
    clearings = [(x, y, rng.uniform(1500.0, 3000.0)) for (x, y) in poisson(M, rng, region, 0.0, 60)[:8]]
    n = 0
    for (x, y) in poisson(M, rng, region, 0.0, 26000):
        if any(math.hypot(x - cx, y - cy) < cr for cx, cy, cr in clearings):
            continue
        if not M.place(x, y, 120.0, spacing=450.0, lane_margin=250.0):
            continue
        if rng.random() < 0.35:
            pine(L, x, y, rng, "Pine_{:04d}".format(n))
        else:
            tree(L, x, y, rng, "Tree_{:04d}".format(n), big=rng.uniform(0.9, 1.3))
        n += 1
    for i, (x, y) in enumerate(poisson(M, rng, region, 0.0, 900)):
        if M.place(x, y, 200.0, spacing=300.0, lane_margin=300.0):
            sz = rng.uniform(150.0, 400.0)
            L.box("Boulder_{:03d}".format(i), x, y, -sz * 0.3, sz * 1.3, sz, sz, yaw=rng.uniform(0, 360),
                  pitch=rng.uniform(-15, 15), mat="rock", cover=True)
    for i, (x, y) in enumerate(poisson(M, rng, region, 0.0, 1200)):
        if M.place(x, y, 150.0, spacing=150.0, lane_margin=200.0):
            L.sphere("Bush_{:03d}".format(i), x, y, 0.0, rng.uniform(150.0, 260.0), rng.uniform(100.0, 160.0),
                     mat="leaf_dark")


def build(lay):
    rng = random.Random(SEED)
    M = Map(lay)
    L = LaneLayout()
    mid_street(L, M, rng)
    top_road(L, M, rng)
    bot_trail(L, M, rng)
    jungle_top_backyards(L, M, rng)
    jungle_bot_nature(L, M, rng)
    return L.items


def preview(items, lay, path):
    import numpy as np
    import struct
    import zlib
    half = lay["half_extent"]
    n = 1260
    s = 2 * half / n
    img = np.full((n, n, 3), 70, dtype=np.float64)
    colors = {"leaf": (60, 120, 50), "leaf_dark": (40, 90, 40), "pine": (30, 80, 50), "trunk": (90, 60, 40),
              "rock": (110, 105, 100), "hay": (215, 185, 80), "roof": (80, 80, 90), "house": (220, 215, 200),
              "brick": (150, 70, 50), "brick_dark": (110, 55, 45), "barn": (160, 40, 35), "metal": (150, 160, 170),
              "concrete": (180, 180, 180), "fence": (240, 240, 240), "wood": (140, 95, 55), "tractor": (60, 150, 60)}
    for it in items:
        c = colors.get(it["mat"], (200, 200, 60))
        r = max(1, int(({"box": max(it.get("sx", 0), it.get("sy", 0)), "cyl": it.get("d", 0), "sphere": it.get("d", 0)}[it["kind"]]) / s / 2))
        px = int((it["y"] + half) / s)
        py = int((half - it["x"]) / s)
        img[max(0, py - r):py + r + 1, max(0, px - r):px + r + 1] = c
    for pts in lay["lanes_full"].values():
        for (x0, y0, _), (x1, y1, _) in zip(pts[:-1], pts[1:]):
            for t in np.linspace(0, 1, 200):
                x, y = x0 + (x1 - x0) * t, y0 + (y1 - y0) * t
                img[int((half - x) / s) % n, int((y + half) / s) % n] = (230, 180, 90)
    raw = b"".join(b"\x00" + img[r].clip(0, 255).astype(np.uint8).tobytes() for r in range(n))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", n, n, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    with open(path, "wb") as fh:
        fh.write(png)


if __name__ == "__main__":
    with open(os.path.join(HERE, "layout.json"), encoding="utf-8") as fh:
        lay = json.load(fh)
    items = build(lay)
    ism = sum(1 for i in items if i.get("ism"))
    print("[LANESDRESS] {} примитивов: {} отдельными акторами, {} экземплярами".format(len(items), len(items) - ism, ism))
    from collections import Counter
    print("[LANESDRESS] по группам:", Counter(i["label"].split("_")[0] for i in items).most_common(12))
    preview(items, lay, os.path.join(HERE, "lanes_preview.png"))
    print("[LANESDRESS] WROTE lanes_preview.png")
