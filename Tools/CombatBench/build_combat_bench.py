"""Стенд боя и движения: L_CombatBench (2026-10-08).

Зачем: проверять «бойца на старте» (спринт вперёд, слабый слайд по стволам, горка, уступы) и базовые
боевые ситуации с тремя видами врагов: ShooterNPC, Kamikaze, Carrier.

Раскладка (игрок в нуле, смотрит на +X):
    X > 0   коридор и восемь комнат. У двери каждой комнаты пульты (ABenchEncounter): выстрел в
            кнопку ставит врагов, второй выстрел убирает. Время боя пишется на пульте и в лог [BENCH].
            +Y: комнаты 40x40 м (DUEL, SQUAD, FLANK, CQB)
            -Y: комнаты 40x80 м (LONG RANGE, KAMIKAZE, CARRIER, MIXED)
    X < 0   дорожки движения, бежать на -X: линейка слайда, горки 15 и 25 градусов, уступы,
            прыжки через провалы, стена.

Запуск по шагам (Tools/mcp.sh, длинный вызов режется таймаутом, шаг доделывается сам):
    g = {"__name__": "cb"}; exec(compile(open(p, encoding="utf-8").read(), p, "exec"), g)
    g["step_level"](); g["step_light"](); g["step_geo"](); g["step_game"]()
Идемпотентно: чистит и ставит заново только акторы со своими тегами.
"""

import math
import unreal

HB = r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/HomeBase/build_homebase.py"
hb = {"__name__": "hb_lib"}
exec(compile(open(HB, encoding="utf-8").read(), HB, "exec"), hb)

LEVEL = "/Game/Prototype/CombatBench/L_CombatBench"
LIGHT_LEVEL = "/Game/Variant_Shooter/Arenas/ArenaDebug/ArenaLightingDebug3"
TAG_GEO = "CombatBenchGeo"
TAG_GAME = "CombatBenchGame"
hb["LEVEL"] = LEVEL
hb["TAG_GEO"] = TAG_GEO
log = hb["log"]
box, wall, shape, CUBE = hb["box"], hb["wall"], hb["shape"], hb["CUBE"]

AI = "/Game/Variant_Shooter/Blueprints/AI/BPs/"
SHOOTER = AI + "BP_ShooterNPC"
KAMIKAZE = AI + "BP_KamikazeDrone"
CARRIER = "/Game/Prototype/HomeBase/BP_KamikazeCarrierDrone"

CORR = 600.0          # половина ширины коридора
ROOM_W = 4000.0
X0 = 1500.0           # где начинается первая комната
H = 500.0             # высота стен
NEAR_D, FAR_D = 4000.0, 8000.0
DOOR = 700.0
FLOOR_X = (-10500.0, 19500.0)
FLOOR_Y = (-9500.0, 5500.0)

S_Z, K_Z, C_Z = 100.0, 400.0, 1500.0     # высота появления: пехота, камикадзе, матка

# Комнаты. covers: (u, v, размер по u, размер по v, высота). Точки: (u, v). u вдоль коридора от левого
# края комнаты, v в глубину от коридора. variants: (имя пульта, {вид: сколько первых точек взять}).
ROOMS = (
    dict(name="DUEL", side=1, depth=NEAR_D,
         covers=((1200, 1400, 300, 60, 120), (2800, 1400, 300, 60, 120), (2000, 2500, 60, 300, 220)),
         S=((2000, 3400), (1100, 3200), (2900, 3200)),
         variants=(("DUEL x1", dict(S=1)), ("DUEL x3", dict(S=3)))),
    dict(name="SQUAD", side=1, depth=NEAR_D,
         covers=((800, 700, 300, 60, 120), (2000, 900, 300, 60, 120), (3200, 700, 300, 60, 120),
                 (600, 3000, 300, 60, 120), (1300, 2600, 60, 300, 220), (2000, 3100, 300, 60, 120),
                 (2700, 2600, 60, 300, 220), (3400, 3000, 300, 60, 120)),
         S=((2000, 3400), (600, 3300), (3400, 3300), (1300, 2900), (2700, 2900)),
         variants=(("SQUAD x3", dict(S=3)), ("SQUAD x5", dict(S=5)))),
    dict(name="FLANK", side=1, depth=NEAR_D,
         covers=((2000, 2000, 1800, 60, 300), (700, 900, 300, 60, 120), (3300, 900, 300, 60, 120)),
         S=((1500, 3000), (2500, 3000), (700, 3400), (3300, 3400)),
         variants=(("FLANK x2", dict(S=2)), ("FLANK x4", dict(S=4)))),
    dict(name="CQB", side=1, depth=NEAR_D, building=True,
         covers=((2000, 600, 300, 60, 120),),
         S=((1300, 1900), (2700, 1900), (2000, 3000), (1300, 3100), (2700, 3100)),
         variants=(("CQB x3", dict(S=3)), ("CQB x5", dict(S=5)))),
    dict(name="LONG RANGE", side=-1, depth=FAR_D,
         covers=((1000, 800, 300, 60, 120), (2000, 1000, 300, 60, 120), (3000, 800, 300, 60, 120),
                 (1000, 6600, 300, 60, 120), (2000, 6800, 300, 60, 120), (3000, 6600, 300, 60, 120),
                 (2000, 4000, 60, 600, 220)),
         S=((1000, 7000), (3000, 7000), (2000, 7200), (500, 6200)),
         variants=(("LONG x2", dict(S=2)), ("LONG x4", dict(S=4)))),
    dict(name="KAMIKAZE", side=-1, depth=FAR_D,
         covers=((1000, 1500, 100, 100, 260), (3000, 1500, 100, 100, 260), (2000, 2800, 100, 100, 260)),
         K=((1200, 5000), (2800, 5000), (2000, 6200), (800, 6800)),
         variants=(("KAMIKAZE x2", dict(K=2)), ("KAMIKAZE x4", dict(K=4)))),
    dict(name="CARRIER", side=-1, depth=FAR_D,
         covers=((1000, 1500, 300, 60, 120), (3000, 1500, 300, 60, 120), (2000, 3000, 60, 300, 220)),
         C=((2000, 6500),), S=((1200, 5200), (2800, 5200)),
         variants=(("CARRIER", dict(C=1)), ("CARRIER + 2", dict(C=1, S=2)))),
    dict(name="MIXED", side=-1, depth=FAR_D,
         covers=((800, 900, 300, 60, 120), (2000, 1100, 300, 60, 120), (3200, 900, 300, 60, 120),
                 (1000, 4500, 300, 60, 120), (3000, 4500, 300, 60, 120), (2000, 3200, 60, 300, 220),
                 (1500, 5800, 60, 300, 220), (2500, 5800, 60, 300, 220)),
         S=((1000, 4900), (3000, 4900), (2000, 6000), (600, 6500), (3400, 6500)),
         K=((1500, 7000), (2500, 7000), (2000, 7500)), C=((2000, 7200),),
         variants=(("MIXED LIGHT", dict(S=3, K=2)), ("MIXED HEAVY", dict(S=5, K=3, C=1)))),
)


def _room_x0(i):
    """Индекс комнаты на своей стороне (0..3) -> левый край."""
    return X0 + i * ROOM_W


def _side_rooms(side):
    return [r for r in ROOMS if r["side"] == side]


def _w(room, i, u, v):
    """Комнатные (u, v) в мировые (x, y)."""
    return _room_x0(i) + u, room["side"] * (CORR + v)


def _text(label, x, y, z, msg, size=40.0, yaw=0.0, tag=TAG_GEO, folder="CombatBench/Labels"):
    return hb["text"](label, x, y, z, msg, size=size, yaw=yaw, folder=folder, tag=tag)


# ==================== уровень и свет ====================

def step_level():
    w = hb["_world"]()
    if w and w.get_path_name().startswith(LEVEL + "."):
        hb["_les"]().set_current_level_by_name(LEVEL.split("/")[-1])
    hb["step_level"]()


def step_light():
    hb["guard"]()
    world = hb["_world"]()
    names = [l.get_outer().get_path_name() for l in unreal.EditorLevelUtils.get_levels(world)]
    if any(n.startswith(LIGHT_LEVEL) for n in names):
        log("свет-сублевел уже подключён")
    else:
        sl = unreal.EditorLevelUtils.add_level_to_world(world, LIGHT_LEVEL, unreal.LevelStreamingDynamic)
        log("ADDED: сублевел света {} ({})".format(LIGHT_LEVEL, sl))
    hb["_les"]().set_current_level_by_name(LEVEL.split("/")[-1])
    hb["guard"]()
    hb["_les"]().save_current_level()


# ==================== геометрия ====================

def _rooms_geo():
    f = "CombatBench/Rooms"
    for side in (1, -1):
        rooms = _side_rooms(side)
        depth = rooms[0]["depth"]
        x_end = _room_x0(len(rooms))
        y_in, y_out = side * CORR, side * (CORR + depth)

        # Стена коридора с дверью в каждую комнату, задняя стена, перегородки.
        doors = [(_room_x0(i) + ROOM_W * 0.5 - DOOR * 0.5, _room_x0(i) + ROOM_W * 0.5 + DOOR * 0.5, 0.0, H)
                 for i in range(len(rooms))]
        wall("Corr{}".format("N" if side > 0 else "S"), "y", y_in, X0, x_end, 0.0, H, t=60.0, gaps=doors, folder=f)
        wall("Back{}".format("N" if side > 0 else "S"), "y", y_out, X0, x_end, 0.0, H, t=60.0, folder=f)
        for i in range(len(rooms) + 1):
            wall("Div{}{}".format("N" if side > 0 else "S", i), "x", _room_x0(i), min(y_in, y_out), max(y_in, y_out),
                 0.0, H, t=60.0, folder=f)

        for i, room in enumerate(rooms):
            cx = _room_x0(i) + ROOM_W * 0.5
            _text("Name_{}".format(room["name"]), cx, side * (CORR - 40.0), H + 40.0, room["name"], size=90.0,
                  yaw=-90.0 * side)
            for k, (u, v, su, sv, h) in enumerate(room["covers"]):
                x, y = _w(room, i, u, v)
                box("Cover_{}_{}".format(room["name"], k), x, y, 0.0, su, sv, h, mat="cover", folder="CombatBench/Covers")
            if room.get("building"):
                # Домик посреди комнаты: коробка 24x24 м с дверями на три стороны и перегородкой.
                bx0, bx1 = _room_x0(i) + 800.0, _room_x0(i) + 3200.0
                by0, by1 = side * (CORR + 1200.0), side * (CORR + 3600.0)
                lo, hi = min(by0, by1), max(by0, by1)
                mid_x, mid_y = (bx0 + bx1) * 0.5, (by0 + by1) * 0.5
                door = lambda c: (c - 150.0, c + 150.0, 0.0, 260.0)
                wall("CQB_Front", "y", by0, bx0, bx1, 0.0, 300.0, gaps=(door(mid_x),), mat="wood", folder=f)
                wall("CQB_Back", "y", by1, bx0, bx1, 0.0, 300.0, mat="wood", folder=f)
                wall("CQB_Left", "x", bx0, lo, hi, 0.0, 300.0, gaps=(door(mid_y),), mat="wood", folder=f)
                wall("CQB_Right", "x", bx1, lo, hi, 0.0, 300.0, gaps=(door(mid_y),), mat="wood", folder=f)
                wall("CQB_Inner", "y", mid_y, bx0, bx1, 0.0, 300.0, gaps=(door(mid_x - 600.0), door(mid_x + 600.0)),
                     mat="wood", folder=f)


def _ramp(label, x_high, x_low, y, top, width=500.0):
    """Наклонная плита между x_high (высота top) и x_low (ноль). Знак наклона берётся из концов."""
    run = abs(x_high - x_low)
    length = math.hypot(run, top)
    angle = math.degrees(math.atan2(top, run))
    pitch = angle if x_high > x_low else -angle
    shape(CUBE, label, ((x_high + x_low) * 0.5, y, top * 0.5 - 10.0), (length, width, 20.0), pitch=pitch,
          mat="pad", folder="CombatBench/Movement")
    return angle


def _movement_geo():
    f = "CombatBench/Movement"
    _text("Move_Title", -600.0, 0.0, 260.0, "MOVEMENT LANES", size=90.0)

    # 1. Линейка слайда: разбег 20 м, дальше метки через метр.
    y = 2500.0
    _text("Slide_Title", -900.0, y, 200.0, "SLIDE RULER\nsprint, crouch at the red line", size=44.0)
    box("Slide_Start", -1000.0, y, 0.0, 14.0, 500.0, 1.0, mat="road", folder=f, collision=False)
    box("Slide_Crouch", -3000.0, y, 0.0, 20.0, 500.0, 2.0, mat="console", folder=f, collision=False)
    for m in range(1, 31):
        x = -3000.0 - m * 100.0
        big = (m % 5 == 0)
        box("Slide_Tick_{}".format(m), x, y, 0.0, 8.0 if big else 4.0, 500.0 if big else 250.0, 1.0, mat="road",
            folder=f, collision=False)
        if big:
            _text("Slide_M_{}".format(m), x, y + 320.0, 20.0, "{} m".format(m), size=40.0)
    # Перебежка между укрытиями: 8 м.
    _text("Run_Title", -6600.0, y - 700.0, 200.0, "COVER TO COVER 8 m", size=44.0)
    box("Run_CoverA", -6800.0, y - 700.0, 0.0, 60.0, 300.0, 120.0, mat="cover", folder=f)
    box("Run_CoverB", -7600.0, y - 700.0, 0.0, 60.0, 300.0, 120.0, mat="cover", folder=f)

    # 2. Горки: крутой подъём, площадка, пологий спуск. Скатиться можно всегда.
    for n, (y, down_deg) in enumerate(((1000.0, 15.0), (-500.0, 25.0))):
        top = 400.0
        x_a = -1500.0
        x_b = x_a - top / math.tan(math.radians(30.0))
        x_c = x_b - 600.0
        x_d = x_c - top / math.tan(math.radians(down_deg))
        _ramp("Hill{}_Up".format(n), x_b, x_a, y, top)
        box("Hill{}_Top".format(n), (x_b + x_c) * 0.5, y, 0.0, 600.0, 500.0, top, mat="structure", folder=f)
        a = _ramp("Hill{}_Down".format(n), x_c, x_d, y, top)
        _text("Hill{}_Title".format(n), x_a + 300.0, y, 200.0, "SLOPE {:.0f} deg".format(a), size=44.0)

    # 3. Уступы.
    y = -2000.0
    _text("Mantle_Title", -900.0, y, 200.0, "LEDGES", size=44.0)
    for k, h in enumerate((100.0, 150.0, 200.0, 250.0, 300.0)):
        x = -1500.0 - k * 800.0
        box("Ledge_{}".format(int(h)), x, y, 0.0, 400.0, 500.0, h, mat="structure", folder=f)
        _text("Ledge_T_{}".format(int(h)), x + 260.0, y, h + 60.0, "{:.1f} m".format(h / 100.0), size=36.0)

    # 4. Прыжки через провалы: площадки 4 м на высоте 1.5 м.
    y = -3500.0
    _text("Gap_Title", -900.0, y, 260.0, "JUMP GAPS", size=44.0)
    x = -1500.0
    box("Gap_P0", x - 200.0, y, 0.0, 400.0, 500.0, 150.0, mat="structure", folder=f)
    x -= 400.0
    for k, gap in enumerate((200.0, 300.0, 400.0, 500.0, 600.0)):
        x -= gap
        box("Gap_P{}".format(k + 1), x - 200.0, y, 0.0, 400.0, 500.0, 150.0, mat="structure", folder=f)
        _text("Gap_T_{}".format(k), x + gap * 0.5, y, 220.0, "{:.0f} m".format(gap / 100.0), size=36.0)
        x -= 400.0

    # 5. Стена: бег по стене на старте выключен, здесь это видно.
    y = -5000.0
    _text("Wall_Title", -900.0, y + 300.0, 200.0, "WALL (wallrun is a manual)", size=44.0)
    wall("Wall_Run", "y", y, -4500.0, -1500.0, 0.0, 500.0, t=60.0, folder=f)


def step_geo():
    hb["guard"]()
    hb["_clear"](TAG_GEO)
    hb["_count"]["n"] = 0
    del hb["_checks"][:]
    cx, cy = (FLOOR_X[0] + FLOOR_X[1]) * 0.5, (FLOOR_Y[0] + FLOOR_Y[1]) * 0.5
    box("Floor", cx, cy, -50.0, FLOOR_X[1] - FLOOR_X[0], FLOOR_Y[1] - FLOOR_Y[0], 50.0, folder="CombatBench/Ground")
    _rooms_geo()
    _movement_geo()
    if not hb["check_kit_centers"]():
        raise RuntimeError("угловой пивот кита посчитан неверно, уровень НЕ сохранён")
    hb["_les"]().save_current_level()
    log("ADDED: геометрия, {} мешей".format(hb["_count"]["n"]))


# ==================== игра: старт, навмеш, точки, пульты ====================

def step_game():
    hb["guard"]()
    hb["_clear"](TAG_GAME)
    eas = hb["_eas"]()

    ps = eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0.0, 0.0, 100.0),
                                    unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    hb["_tag"](ps, TAG_GAME, "CB_PlayerStart", "CombatBench/Gameplay")

    nav = eas.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector(
        (FLOOR_X[0] + FLOOR_X[1]) * 0.5, (FLOOR_Y[0] + FLOOR_Y[1]) * 0.5, 300.0))
    nav.set_actor_scale3d(unreal.Vector((FLOOR_X[1] - FLOOR_X[0]) / 200.0, (FLOOR_Y[1] - FLOOR_Y[0]) / 200.0, 10.0))
    hb["_tag"](nav, TAG_GAME, "CB_NavBounds", "CombatBench/Gameplay")

    classes = {"S": unreal.EditorAssetLibrary.load_blueprint_class(SHOOTER),
               "K": unreal.EditorAssetLibrary.load_blueprint_class(KAMIKAZE),
               "C": unreal.EditorAssetLibrary.load_blueprint_class(CARRIER)}
    heights = {"S": S_Z, "K": K_Z, "C": C_Z}
    for kind, cls in classes.items():
        if cls is None:
            raise RuntimeError("нет класса врага {}".format(kind))

    for side in (1, -1):
        for i, room in enumerate(_side_rooms(side)):
            face = -90.0 * side          # лицом к коридору
            points = {}
            for kind in ("S", "K", "C"):
                points[kind] = []
                for k, (u, v) in enumerate(room.get(kind, ())):
                    x, y = _w(room, i, u, v)
                    tp = eas.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(x, y, heights[kind]),
                                                    unreal.Rotator(roll=0.0, pitch=0.0, yaw=face))
                    hb["_tag"](tp, TAG_GAME, "CB_{}_{}{}".format(room["name"].replace(" ", ""), kind, k),
                               "CombatBench/SpawnPoints/" + room["name"])
                    points[kind].append(tp)

            cx = _room_x0(i) + ROOM_W * 0.5
            for n, (name, take) in enumerate(room["variants"]):
                x = cx + DOOR * 0.5 + 300.0 + n * 420.0
                enc = eas.spawn_actor_from_class(unreal.BenchEncounter, unreal.Vector(x, side * (CORR - 45.0), 130.0),
                                                 unreal.Rotator(roll=0.0, pitch=0.0, yaw=face))
                hb["_tag"](enc, TAG_GAME, "CB_Enc_{}".format(name.replace(" ", "_")), "CombatBench/Encounters")
                enc.set_editor_property("encounter_name", name)
                groups = []
                for kind, count in take.items():
                    grp = unreal.BenchEncounterGroup()
                    grp.set_editor_property("enemy_class", classes[kind])
                    grp.set_editor_property("spawn_points", points[kind][:count])
                    groups.append(grp)
                enc.set_editor_property("groups", groups)
                got = enc.get_editor_property("groups")
                log("ADDED: {}: {}".format(name, ", ".join("{} x{}".format(
                    g.get_editor_property("enemy_class").get_name(), len(g.get_editor_property("spawn_points"))) for g in got)))

    recast = [a for a in eas.get_all_level_actors() if isinstance(a, unreal.RecastNavMesh)]
    for r in recast:
        r.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)
    log("RecastNavMesh: {}".format(len(recast)))
    hb["_les"]().save_current_level()
    log("DONE: {}".format(LEVEL))
