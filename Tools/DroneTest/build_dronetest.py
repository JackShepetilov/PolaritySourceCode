"""Комната для отладки полёта дронов: закрытый зал, пара препятствий, спавнер, который держит в
живых ровно одного дрона (ADroneTestSpawner: взорвался один, сразу следующий).

Примитивы (угловой кит SM_Bld_Block_1x1_01, материалы M_FlatCol, гард уровня, теги) берутся из
скрипта базы, чтобы не дублировать: он исполняется как библиотека, и ему подменяются уровень и теги.

Запуск через execute_python_code, по шагам:
    import unreal
    p = r"C:/.../Source/Tools/DroneTest/build_dronetest" + "." + "py"
    g = {"__name__": "dt"}; exec(compile(open(p, encoding="utf-8").read(), p, "exec"), g)
    g["step_level"](); g["step_room"]()      до ребилда
    g["step_game"]()                          после ребилда, когда класс спавнера уже в бинарнике
Света скрипт не ставит: свет автор держит отдельным сублевелом.
"""

import math

import unreal

HB = r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/HomeBase/build_homebase.py"
hb = {"__name__": "hb_lib"}
exec(compile(open(HB, encoding="utf-8").read(), HB, "exec"), hb)

LEVEL = "/Game/Prototype/DroneTest/L_DroneTestRoom"
TAG_ROOM = "DroneTestGen"
TAG_GAME = "DroneTestGame"
hb["LEVEL"] = LEVEL
hb["TAG_GEO"] = TAG_ROOM

SPAWNER_CLASS = "/Script/Polarity.DroneTestSpawner"
DRONE_BP = "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_KamikazeDrone"

# Зал 60 x 60 м, потолок 15 м: у дрона в блюпринте орбита 25 м в стороне и 8 м над целью,
# в зале меньше он бился бы о стены ещё до атаки.
HALF = 3000.0
HEIGHT = 1500.0
WALL_T = 50.0

log = hb["log"]


def step_level():
    hb["step_level"]()


def step_room():
    hb["guard"]()
    hb["_clear"](TAG_ROOM)
    hb["_count"]["n"] = 0
    del hb["_checks"][:]
    box, wall, text = hb["box"], hb["wall"], hb["text"]
    f = "DroneTest/Room"

    span = HALF + WALL_T
    box("Floor", 0.0, 0.0, -WALL_T, 2.0 * span, 2.0 * span, WALL_T, folder=f)
    box("Ceiling", 0.0, 0.0, HEIGHT, 2.0 * span, 2.0 * span, WALL_T, mat="fence", folder=f)
    wall("Wall_N", "x", HALF + WALL_T * 0.5, -span, span, 0.0, HEIGHT, t=WALL_T, mat="fence", folder=f)
    wall("Wall_S", "x", -HALF - WALL_T * 0.5, -span, span, 0.0, HEIGHT, t=WALL_T, mat="fence", folder=f)
    wall("Wall_E", "y", HALF + WALL_T * 0.5, -HALF, HALF, 0.0, HEIGHT, t=WALL_T, mat="fence", folder=f)
    wall("Wall_W", "y", -HALF - WALL_T * 0.5, -HALF, HALF, 0.0, HEIGHT, t=WALL_T, mat="fence", folder=f)

    # Препятствия: колонны до потолка (дрон должен огибать, а не проходить), низкая стенка
    # (укрытие по пояс), куб 3 м и площадка 4 м (проверка ухода вверх грепплом).
    o = "DroneTest/Obstacles"
    box("Pillar_A", 900.0, 900.0, 0.0, 200.0, 200.0, HEIGHT, mat="structure", folder=o)
    box("Pillar_B", -900.0, -900.0, 0.0, 200.0, 200.0, HEIGHT, mat="structure", folder=o)
    box("LowWall", 0.0, -1600.0, 0.0, 600.0, 40.0, 120.0, mat="cover", folder=o)
    box("Crate", 1300.0, -1300.0, 0.0, 300.0, 300.0, 300.0, mat="wood", folder=o)
    box("Platform", -1800.0, 1800.0, 0.0, 800.0, 800.0, 400.0, mat="cover", folder=o)

    text("Title", HALF - 60.0, 0.0, 900.0, "DRONE TEST", size=150.0, yaw=180.0, folder=f, tag=TAG_ROOM)
    log("ADDED: зал {} x {} м, потолок {} м, {} мешей".format(
        int(2 * HALF / 100), int(2 * HALF / 100), int(HEIGHT / 100), hb["_count"]["n"]))
    if not hb["check_kit_centers"]():
        raise RuntimeError("угловой пивот кита посчитан неверно, уровень НЕ сохранён")
    hb["_les"]().save_current_level()


def step_game():
    hb["guard"]()
    hb["_clear"](TAG_GAME)
    eas = hb["_eas"]()
    f = "DroneTest/Gameplay"

    ps = eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-2400.0, 0.0, 100.0),
                                    unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    hb["_tag"](ps, TAG_GAME, "DT_PlayerStart", f)

    cls = unreal.load_class(None, SPAWNER_CLASS)
    if cls is None:
        raise RuntimeError("класса {} нет в бинарнике: сначала полный ребилд".format(SPAWNER_CLASS))
    # Напротив игрока, на высоте 8 м: там же, где дрон и так держит орбиту.
    sp = eas.spawn_actor_from_class(cls, unreal.Vector(2400.0, 0.0, 800.0),
                                    unreal.Rotator(roll=0.0, pitch=0.0, yaw=180.0))
    sp.set_editor_property("drone_class", unreal.EditorAssetLibrary.load_blueprint_class(DRONE_BP))
    hb["_tag"](sp, TAG_GAME, "DT_DroneSpawner", f)
    log("ADDED: старт игрока, спавнер дронов ({})".format(DRONE_BP.split("/")[-1]))
    hb["_les"]().save_current_level()
