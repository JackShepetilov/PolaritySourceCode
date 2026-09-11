"""Полигон для отладки полёта дронов: открытая площадка с разметкой каждые 5 м, вышки под
греппл, стена и навес для проверки столкновений, спавнер, который держит в живых ровно одного
дрона (ADroneTestSpawner: взорвался один, сразу следующий). Стен по периметру и потолка нет
намеренно: дрону нужно небо, игроку нужно видеть его со всех сторон.

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

# Открытая площадка без стен и потолка: у дрона орбита 25 м в стороне и 8 м над целью, и ему
# нужно небо, а игроку видно дрона со всех сторон. Пол 120 x 120 м, чтобы не упасть за край.
FLOOR_HALF = 6000.0
GRID_HALF = 2500.0     # разметка 50 x 50 м вокруг старта, линия каждые 5 м
GRID_STEP = 500.0
TOWER_R = 1800.0       # вышки под греппл: 18 м от центра, дальность крюка 21.5 м
TOWER_H = 1500.0

log = hb["log"]


def step_level():
    hb["step_level"]()


def step_room():
    hb["guard"]()
    hb["_clear"](TAG_ROOM)
    hb["_count"]["n"] = 0
    del hb["_checks"][:]
    box, text = hb["box"], hb["text"]
    f = "DroneTest/Ground"

    box("Floor", 0.0, 0.0, -50.0, 2.0 * FLOOR_HALF, 2.0 * FLOOR_HALF, 50.0, folder=f)

    # Разметка для замеров: тёмные полосы каждые 5 м, без коллизии, на 1 см над полом.
    g = "DroneTest/Grid"
    n = int(2 * GRID_HALF / GRID_STEP) + 1
    for i in range(n):
        c = -GRID_HALF + i * GRID_STEP
        width = 30.0 if abs(c) < 1.0 else 10.0     # оси через старт толще
        box("GridX_{:02d}".format(i), c, 0.0, 0.0, width, 2.0 * GRID_HALF, 1.0, mat="road", folder=g, collision=False)
        box("GridY_{:02d}".format(i), 0.0, c, 0.0, 2.0 * GRID_HALF, width, 1.0, mat="road", folder=g, collision=False)

    # Вышки под греппл по кругу: уйти крюком можно в любую сторону от дрона.
    t = "DroneTest/GrappleTowers"
    for k, a in enumerate((45.0, 135.0, 225.0, 315.0)):
        x, y = hb["polar"](a, TOWER_R)
        box("Tower_{}".format(k), x, y, 0.0, 200.0, 200.0, TOWER_H, mat="structure", folder=t)

    # Геометрия для проверки «сквозь не пролетает»: стена, навес, за которым прячутся, и ящик.
    o = "DroneTest/Obstacles"
    box("Wall", 0.0, -3500.0, 0.0, 2000.0, 50.0, 800.0, mat="fence", folder=o)
    box("Canopy_Roof", 3500.0, 3500.0, 400.0, 800.0, 800.0, 30.0, mat="wood", folder=o)
    for i, (dx, dy) in enumerate(((-380.0, -380.0), (-380.0, 380.0), (380.0, -380.0), (380.0, 380.0))):
        box("Canopy_Post_{}".format(i), 3500.0 + dx, 3500.0 + dy, 0.0, 30.0, 30.0, 400.0, mat="wood", folder=o)
    box("Crate", -3000.0, 3000.0, 0.0, 150.0, 150.0, 150.0, mat="cover", folder=o)

    log("ADDED: площадка {} x {} м, разметка каждые 5 м, 4 вышки, стена, навес, ящик, {} мешей".format(
        int(2 * FLOOR_HALF / 100), int(2 * FLOOR_HALF / 100), hb["_count"]["n"]))
    if not hb["check_kit_centers"]():
        raise RuntimeError("угловой пивот кита посчитан неверно, уровень НЕ сохранён")
    hb["_les"]().save_current_level()


def step_game():
    hb["guard"]()
    hb["_clear"](TAG_GAME)
    eas = hb["_eas"]()
    f = "DroneTest/Gameplay"

    # Игрок в центре разметки, лицом к спавнеру.
    ps = eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0.0, 0.0, 100.0),
                                    unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    hb["_tag"](ps, TAG_GAME, "DT_PlayerStart", f)

    cls = unreal.load_class(None, SPAWNER_CLASS)
    if cls is None:
        raise RuntimeError("класса {} нет в бинарнике: сначала полный ребилд".format(SPAWNER_CLASS))
    # В 25 м перед игроком и на 8 м вверх: там, где дрон и так держит орбиту.
    sp = eas.spawn_actor_from_class(cls, unreal.Vector(2500.0, 0.0, 800.0),
                                    unreal.Rotator(roll=0.0, pitch=0.0, yaw=180.0))
    sp.set_editor_property("drone_class", unreal.EditorAssetLibrary.load_blueprint_class(DRONE_BP))
    hb["_tag"](sp, TAG_GAME, "DT_DroneSpawner", f)
    log("ADDED: старт игрока, спавнер дронов ({})".format(DRONE_BP.split("/")[-1]))
    hb["_les"]().save_current_level()


TAG_CARRIER = "DroneTestCarrier"
CARRIER_BP = "/Game/Prototype/HomeBase/BP_KamikazeCarrierDrone"


def step_carrier_spawner():
    """Спавнер маток отдельным шагом: пересоздаёт только себя, настройки остальных акторов не трогает.
    KeepAlive 0, то есть выключен: замеры одиночных дронов идут без маток, включать руками."""
    hb["guard"]()
    hb["_clear"](TAG_CARRIER)
    cls = unreal.load_class(None, "/Script/Polarity.KeepAliveSpawner")
    if cls is None:
        raise RuntimeError("класса KeepAliveSpawner нет в бинарнике: сначала полный ребилд")
    # Справа впереди, 51 м от старта, 15 м вверх: матка залетает из-за края разметки.
    sp = hb["_eas"]().spawn_actor_from_class(cls, unreal.Vector(4500.0, 2500.0, 1500.0),
                                             unreal.Rotator(roll=0.0, pitch=0.0, yaw=210.0))
    sp.set_editor_property("drone_class", unreal.EditorAssetLibrary.load_blueprint_class(CARRIER_BP))
    sp.set_editor_property("keep_alive", 0)
    hb["_tag"](sp, TAG_CARRIER, "DT_CarrierSpawner", "DroneTest/Gameplay")
    log("ADDED: спавнер маток (KeepAlive 0, выключен)")
    hb["_les"]().save_current_level()
