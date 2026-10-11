"""Комнаты VERSUS и ALLIES на L_CombatBench (2026-10-11).

VERSUS: враги дерутся друг с другом, игрок смотрит с галереи. Зачем: видеть тактику пехоты со
стороны (кто стреляет, кто бежит, кто обходит), когда по тебе самому не стреляют.

ALLIES: такая же арена южнее (от конца коридора направо, вдоль задней стены комнат). 9 синих за
игрока (команда 0) против 10 красных. Красные считают игрока врагом, синие нет. Синие появляются у
лестницы, по которой игрок спускается в арену.

Устройство арены: за концом коридора лестница на галерею высотой 4 м. Галерея идёт вдоль ближнего
края арены и вдоль её длинной стороны (+Y): видно сверху и сбоку. С галереи в арену спускается
вторая лестница. Арена 56x60 м, стороны дерутся вдоль X. Первые ряды сторон стоят в 32 м друг от
друга (зрение пехоты 35 м), за ними тыл.

Пульты стоят перед лестницей на галерею. На пульте: кто смотрит (bPlayersOnlyWatch, тогда ничей ИИ
не считает игрока врагом), команды, цвет и задача отряда: HOLD держит углы, ADVANCE идёт
возвращать угол. Бой сторон кончается, когда осталась одна. Красные MI_BenchTeam_1, синие _2.
Над головами текст, что боец делает: консоль polarity.ai.bench 1.

Обе лестницы закрыты для навмеша (NavModifierVolume, NavArea_Null): бойцам на галерее делать нечего.

Каждая арена под своим тегом (CombatBenchArena, CombatBenchArena2): остальной стенд скрипт не трогает.
Запуск: g = {"__name__": "ar"}; exec(compile(open(p, encoding="utf-8").read(), p, "exec"), g); g["step_arena"]()
"""

import unreal

CB = r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/CombatBench/build_combat_bench.py"
cb = {"__name__": "cb_lib"}
exec(compile(open(CB, encoding="utf-8").read(), CB, "exec"), cb)
hb = cb["hb"]
log, box, wall = hb["log"], hb["box"], hb["wall"]

MAT_DIR = "/Game/Prototype/CombatBench/Materials"
GAL_H = 400.0                      # высота галереи
STEP_RUN, STEP_RISE = 30.0, 20.0   # ступень лестницы
STAIR_W = 300.0
STAIR_LEN = GAL_H / STEP_RISE * STEP_RUN
STAIR_X = 18100.0                  # низ лестницы на галерею (от стены комнат 6 м: место для пультов)
GAL_X = (STAIR_X + STAIR_LEN, STAIR_X + STAIR_LEN + 600.0)   # ближняя галерея (поперёк)
AX = (GAL_X[1], GAL_X[1] + 5600.0)  # арена по X
AY = (-3000.0, 3000.0)             # арена по Y (до сдвига)
SIDE_Y = (AY[1], AY[1] + 600.0)    # боковая галерея
WALL_H = 500.0
SPAWN_U = 1200.0                   # первый ряд от своего края: между сторонами 32 м
SPAWN_ROWS = (SPAWN_U, SPAWN_U - 500.0)   # второй ряд за первым: до десяти бойцов на сторону

# Укрытия ближней половины: (u, v, размер по X, размер по Y, высота). u от края арены, v от её южного
# края. Дальняя половина зеркальна по X. Низкие 120 (сидя не видно), высокие 220 (выход вбок).
HALF = (
    (600, 700, 60, 300, 220), (600, 3000, 60, 300, 120), (600, 5300, 60, 300, 220),
    (1600, 700, 60, 300, 120), (1600, 1850, 60, 300, 120), (1600, 4150, 60, 300, 120), (1600, 5300, 60, 300, 120),
    (2000, 1000, 300, 60, 220), (2000, 5000, 300, 60, 220),
    (2200, 1850, 60, 300, 220), (2200, 3000, 60, 400, 120), (2200, 4150, 60, 300, 220),
)
CENTER = ((2800, 700, 60, 300, 220), (2800, 1850, 60, 300, 120), (2800, 3000, 60, 300, 220),
          (2800, 4150, 60, 300, 120), (2800, 5300, 60, 300, 220))
# Точки появления стоят в просветах между укрытиями: по прямой вдоль X стороны видят друг друга
# сразу. За укрытием на старте никто никого не видит и бой не начинается (проверено 2026-10-10).
SPAWN_V = (2425, 3575, 1275, 4725, 300)
TEAM_COLORS = {1: (0.80, 0.10, 0.08), 2: (0.08, 0.28, 0.90)}   # MI_BenchTeam_1 красный, _2 синий
RED, BLUE = 1, 2
ADVANCE, HOLD = "ADVANCE", "HOLD"

# Пульт: (имя, смотреть ли со стороны, (ближняя сторона, дальняя)).
# Сторона: (команда, сколько, цвет, задача). Команда 0 это игроки: такие бойцы за игрока.
ARENAS = (
    dict(tag="CombatBenchArena", name="VERSUS", oy=0.0,
         title="VERSUS\nAI fights AI, you watch from the gallery",
         consoles=(
             ("VERSUS 3v3", True, ((1, 3, RED, ADVANCE), (2, 3, BLUE, ADVANCE))),
             ("VERSUS 5v5", True, ((1, 5, RED, ADVANCE), (2, 5, BLUE, ADVANCE))),
             ("VERSUS 9v3", True, ((1, 9, RED, ADVANCE), (2, 3, BLUE, ADVANCE))),
         )),
    dict(tag="CombatBenchArena2", name="ALLIES", oy=-7800.0,
         title="ALLIES\n9 blue and you against 10 red",
         consoles=(
             ("ALLIES 9 v 10", False, ((0, 9, BLUE, ADVANCE), (1, 10, RED, ADVANCE))),
             ("ALLIES 3 v 10", False, ((0, 3, BLUE, ADVANCE), (1, 10, RED, ADVANCE))),
         )),
)


def _team_materials():
    """Свой материал под скелетные меши (M_FlatCol общий, его флаги не трогаю) и по инстансу на цвет."""
    eal, at, mel = unreal.EditorAssetLibrary, unreal.AssetToolsHelpers.get_asset_tools(), unreal.MaterialEditingLibrary
    base_path = MAT_DIR + "/M_BenchTeam"
    if eal.does_asset_exist(base_path):
        base = unreal.load_asset(base_path)
    else:
        base = at.create_asset("M_BenchTeam", MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
        node = mel.create_material_expression(base, unreal.MaterialExpressionVectorParameter, -350, 0)
        node.set_editor_property("parameter_name", "Color")
        node.set_editor_property("default_value", unreal.LinearColor(0.5, 0.5, 0.5, 1.0))
        mel.connect_material_property(node, "", unreal.MaterialProperty.MP_BASE_COLOR)
        base.set_editor_property("used_with_skeletal_mesh", True)
        mel.recompile_material(base)
        log("CREATED: {} saved {}".format(base_path, eal.save_asset(base_path, False)))
    out = {}
    for team, (r, g, b) in TEAM_COLORS.items():
        path = "{}/MI_BenchTeam_{}".format(MAT_DIR, team)
        if eal.does_asset_exist(path):
            mi = unreal.load_asset(path)
        else:
            mi = at.create_asset("MI_BenchTeam_{}".format(team), MAT_DIR, unreal.MaterialInstanceConstant,
                                 unreal.MaterialInstanceConstantFactoryNew())
            mi.set_editor_property("parent", base)
            mel.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(r, g, b, 1.0))
            mel.update_material_instance(mi)
            log("CREATED: {} saved {}".format(path, eal.save_asset(path, False)))
        out[team] = mi
    return out


def _stairs(label, x_low, y, direction, folder, tag):
    """Лестница от пола до галереи. direction +1: поднимается в сторону +X, -1: в сторону -X."""
    n = int(GAL_H / STEP_RISE)
    for k in range(n):
        x = x_low + direction * (k + 0.5) * STEP_RUN
        box("{}_{}".format(label, k), x, y, 0.0, STEP_RUN, STAIR_W, STEP_RISE * (k + 1), mat="structure", folder=folder)
    # Навмеш на лестницу не пускаем.
    cx = x_low + direction * STAIR_LEN * 0.5
    vol = hb["_eas"]().spawn_actor_from_class(unreal.NavModifierVolume, unreal.Vector(cx, y, GAL_H * 0.5))
    vol.set_actor_scale3d(unreal.Vector((STAIR_LEN + 100.0) / 200.0, (STAIR_W + 100.0) / 200.0, (GAL_H + 200.0) / 200.0))
    vol.set_editor_property("area_class", unreal.NavArea_Null)
    hb["_tag"](vol, tag, label + "_NoNav", folder)


def _arena(spec, mats, shooter):
    tag, oy, name = spec["tag"], spec["oy"], spec["name"]
    hb["TAG_GEO"] = tag
    hb["_clear"](tag)
    eas = hb["_eas"]()
    f = "CombatBench/" + name
    ay = (AY[0] + oy, AY[1] + oy)
    side_y = (SIDE_Y[0] + oy, SIDE_Y[1] + oy)
    length = AX[1] - AX[0]

    # Пол под новым куском. Старый пол: X до 19500, Y от -9500 до 5500. Поверх него не кладём.
    fx1 = AX[1] + 600.0
    fy0, fy1 = ay[0] - 600.0, side_y[1] + 600.0
    box(name + "_Floor", (19500.0 + fx1) * 0.5, (fy0 + fy1) * 0.5, -50.0, fx1 - 19500.0, fy1 - fy0, 50.0, folder=f)
    if fy0 < -9500.0:
        box(name + "_FloorSouth", (17500.0 + 19500.0) * 0.5, (fy0 - 9500.0) * 0.5, -50.0, 2000.0, -9500.0 - fy0, 50.0,
            folder=f)

    # Лестница на галерею и лестница с галереи в арену (вдоль боковой галереи, у ближней стороны).
    _stairs(name + "_StairUp", STAIR_X, oy, 1, f + "/Stairs", tag)
    _stairs(name + "_StairDown", AX[0] + STAIR_LEN, ay[1] - STAIR_W * 0.5, -1, f + "/Stairs", tag)

    box(name + "_GalNear", (GAL_X[0] + GAL_X[1]) * 0.5, (ay[0] + side_y[1]) * 0.5, 0.0,
        GAL_X[1] - GAL_X[0], side_y[1] - ay[0], GAL_H, mat="structure", folder=f)
    box(name + "_GalSide", (AX[0] + AX[1]) * 0.5, (side_y[0] + side_y[1]) * 0.5, 0.0,
        length, side_y[1] - side_y[0], GAL_H, mat="structure", folder=f)
    # Стены с двух глухих сторон, чтобы пули не летели в комнаты.
    wall(name + "_WallFar", "x", AX[1], ay[0], side_y[1], 0.0, WALL_H, t=60.0, folder=f)
    wall(name + "_WallSouth", "y", ay[0], GAL_X[0], AX[1], 0.0, WALL_H, t=60.0, folder=f)

    covers = [(u, v, su, sv, h) for (u, v, su, sv, h) in HALF]
    covers += [(length - u, v, su, sv, h) for (u, v, su, sv, h) in HALF]
    covers += list(CENTER)
    for k, (u, v, su, sv, h) in enumerate(covers):
        box("{}_Cover{}".format(name, k), AX[0] + u, ay[0] + v, 0.0, su, sv, h, mat="cover", folder=f + "/Covers")

    hb["text"](name + "_Title", STAIR_X - 100.0, oy, 560.0, spec["title"], size=60.0, yaw=180.0, folder=f, tag=tag)
    hb["text"](name + "_Hint", STAIR_X - 100.0, oy, 470.0, "console: polarity.ai.bench 1 shows what each one is doing",
               size=30.0, yaw=180.0, folder=f, tag=tag)

    nav = eas.spawn_actor_from_class(unreal.NavMeshBoundsVolume,
                                     unreal.Vector((17500.0 + fx1) * 0.5, (fy0 + fy1) * 0.5, 300.0))
    nav.set_actor_scale3d(unreal.Vector((fx1 - 17500.0) / 200.0, (fy1 - fy0) / 200.0, 10.0))
    hb["_tag"](nav, tag, name + "_NavBounds", f)

    # Точки: сторона 0 ближняя (у галереи и лестницы), сторона 1 дальняя. По десять на сторону.
    points = {0: [], 1: []}
    for side in (0, 1):
        for row in SPAWN_ROWS:
            u = row if side == 0 else length - row
            for v in SPAWN_V:
                tp = eas.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(AX[0] + u, ay[0] + v, cb["S_Z"]),
                                                unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0 if side == 0 else 180.0))
                hb["_tag"](tp, tag, "{}_S{}_{}".format(name, side, len(points[side])), f + "/SpawnPoints")
                points[side].append(tp)

    tasks = {ADVANCE: unreal.SquadTask.ADVANCE, HOLD: unreal.SquadTask.HOLD}
    consoles = spec["consoles"]
    for n, (title, watch, sides) in enumerate(consoles):
        y = oy + (n - (len(consoles) - 1) * 0.5) * 520.0
        enc = eas.spawn_actor_from_class(unreal.BenchEncounter, unreal.Vector(STAIR_X - 60.0, y, 130.0),
                                         unreal.Rotator(roll=0.0, pitch=0.0, yaw=180.0))
        hb["_tag"](enc, tag, "{}_Enc_{}".format(name, title.replace(" ", "_")), f)
        enc.set_editor_property("encounter_name", title)
        enc.set_editor_property("players_only_watch", watch)
        groups = []
        for side, (team, count, color, task) in enumerate(sides):
            grp = unreal.BenchEncounterGroup()
            grp.set_editor_property("enemy_class", shooter)
            grp.set_editor_property("spawn_points", points[side][:count])
            grp.set_editor_property("team", team)
            grp.set_editor_property("body_material", mats[color])
            grp.set_editor_property("task", tasks[task])
            groups.append(grp)
        enc.set_editor_property("groups", groups)
        got = enc.get_editor_property("groups")
        log("ADDED: {}: {}".format(title, ", ".join("team {} x{} {} {}".format(
            g.get_editor_property("team"), len(g.get_editor_property("spawn_points")),
            g.get_editor_property("body_material").get_name(), g.get_editor_property("task")) for g in got)))
    log("DONE: {} {}x{} m, {} covers".format(name, int(length / 100), int((AY[1] - AY[0]) / 100), len(covers)))


def step_arena():
    hb["guard"]()
    mats = _team_materials()
    shooter = unreal.EditorAssetLibrary.load_blueprint_class(cb["SHOOTER"])
    if shooter is None:
        raise RuntimeError("нет класса " + cb["SHOOTER"])
    for spec in ARENAS:
        _arena(spec, mats, shooter)
    hb["_les"]().save_current_level()
