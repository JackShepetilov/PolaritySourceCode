"""Стенд турели и осады: ядро, турель перед ним, поле укрытий, двое стрелков и двое гранатомётчиков,
которые возрождаются, пока идёт прогон. Смотровая вышка для игрока сбоку-сзади, откуда видно всё поле.

Зачем: поведение пехоты против турели и ядра (пик из-за угла, присед, стрельба по постройке) надо
гонять одинаково и много раз подряд, а не руками на боевой карте. Прогон целиком делает
run_bench.sh: открыть уровень, запустить игру, скормить турели ствол, подождать, снять картинку,
остановить, собрать сводку из лога.

Раскладка (ось X это направление атаки, враги с -X, ядро на +X):
    -3000  спавнеры (KeepAliveSpawner, по 2 на класс)
     -400  низкие стенки (присед) и угол из двух стен
        0  три колонны: основные углы для пика по турели (1900 см до неё)
      700  два передовых блока
     1900  турель, смотрит на -X
     2900  ядро осады
   (3300, -2000)  смотровая вышка 7 м

Примитивы, материалы, гард уровня и теги берутся из скрипта базы (как в DroneTest): он исполняется
как библиотека, и ему подменяются уровень и тег геометрии.

Запуск (через Tools/mcp.sh или execute_python_code; длинный код тул режет, поэтому только так):
    import unreal
    p = r"C:/.../Source/Tools/TurretBench/build_turret_bench" + "." + "py"
    g = {"__name__": "tb"}; exec(compile(open(p, encoding="utf-8").read(), p, "exec"), g)
    g["main"]()
Света скрипт не ставит (правило автора): свет даёт подуровень ArenaLightingDebug3, добавленный
автором руками. Скрипт чистит и ставит заново только акторы со своими тегами, подуровень не трогает.
"""

import math

import unreal

HB = r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/HomeBase/build_homebase.py"
hb = {"__name__": "hb_lib"}
exec(compile(open(HB, encoding="utf-8").read(), HB, "exec"), hb)

LEVEL = "/Game/Prototype/TurretBench/L_TurretSiegeBench"
TAG_ROOM = "TurretBenchGen"
TAG_GAME = "TurretBenchGame"
hb["LEVEL"] = LEVEL
hb["TAG_GEO"] = TAG_ROOM

TURRET_BP = "/Game/Variant_Shooter/Buildables/BP_Buildable_Turret"
RIFLE_BP = "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_ShooterNPC"
GRENADIER_BP = "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_GrenadierNPC"

# Теги, по которым run_bench находит акторов в мире игры (метки акторов в PIE не существуют).
TAG_TURRET = "BenchTurret"
TAG_CORE = "BenchCore"
TAG_RIFLES = "BenchRifles"
TAG_GRENADIERS = "BenchGrenadiers"
TAG_TOWER = "BenchTower"

FLOOR_HX, FLOOR_HY = 3600.0, 2600.0
GRID_STEP = 500.0
TURRET_AT = (1900.0, 0.0)
CORE_AT = (2900.0, 0.0)
CORE_SIZE = 300.0
TOWER_AT = (3300.0, -2000.0)
TOWER_H = 700.0
SPAWN_X = -3000.0
KEEP_ALIVE = 2
RESPAWN_DELAY = 4.0

log = hb["log"]


def _text(label, x, y, z, msg, size=70.0, yaw=180.0, tag=TAG_ROOM, folder="TurretBench/Labels"):
    # Тег всегда явно: у hb["text"] значение по умолчанию вычислено при определении и равно тегу базы.
    # Подписи латиницей: у шрифта TextRender по умолчанию нет кириллицы, буквы рисуются квадратами.
    return hb["text"](label, x, y, z, msg, size=size, yaw=yaw, folder=folder, tag=tag)


def _tags(actor, *tags):
    actor.set_editor_property("tags", [unreal.Name(t) for t in tags])


def step_level():
    # Карта может быть уже открыта, но с подуровнем света автора в роли текущего. Всё, что скрипт
    # спавнит, ложится в ТЕКУЩИЙ уровень, так что геометрия уехала бы в подуровень света. Вернуть
    # текущим основной уровень без перезагрузки; гард базы потом это же и проверит.
    w = hb["_world"]()
    if w and w.get_path_name().startswith(LEVEL + "."):
        hb["_les"]().set_current_level_by_name(LEVEL.split("/")[-1])
    hb["step_level"]()


def step_room():
    hb["guard"]()
    hb["_clear"](TAG_ROOM)
    hb["_count"]["n"] = 0
    del hb["_checks"][:]
    box = hb["box"]

    box("Floor", 0.0, 0.0, -50.0, 2.0 * FLOOR_HX, 2.0 * FLOOR_HY, 50.0, folder="TurretBench/Ground")

    # Разметка каждые 5 м: дистанции на картинке читаются без линейки. Оси через ноль толще.
    g = "TurretBench/Grid"
    nx = int(2 * FLOOR_HX / GRID_STEP) + 1
    ny = int(2 * FLOOR_HY / GRID_STEP) + 1
    for i in range(nx):
        x = -FLOOR_HX + i * GRID_STEP
        if abs(x) > FLOOR_HX - 1.0:
            continue
        box("GridX_{:02d}".format(i), x, 0.0, 0.0, 30.0 if abs(x) < 1.0 else 10.0, 2.0 * FLOOR_HY, 1.0,
            mat="road", folder=g, collision=False)
    for j in range(ny):
        y = -FLOOR_HY + j * GRID_STEP
        if abs(y) > FLOOR_HY - 1.0:
            continue
        box("GridY_{:02d}".format(j), 0.0, y, 0.0, 2.0 * FLOOR_HX, 30.0 if abs(y) < 1.0 else 10.0, 1.0,
            mat="road", folder=g, collision=False)

    # Поле укрытий. Всё в пределах 800..2500 см от турели: ближе и дальше этого CoverFinder точки
    # укрытия не выбирает (MinPeekDistance / MaxPeekDistance), и угол просто не будет использован.
    c = "TurretBench/Cover"
    for k, y in enumerate((-1100.0, 0.0, 1100.0)):
        box("Pillar_{}".format(k), 0.0, y, 0.0, 200.0, 200.0, 320.0, mat="structure", folder=c)
    for k, y in enumerate((-550.0, 550.0)):
        box("LowWall_{}".format(k), -400.0, y, 0.0, 60.0, 400.0, 110.0, mat="cover", folder=c)
    box("Corner_A", -400.0, 1900.0, 0.0, 60.0, 500.0, 300.0, mat="fence", folder=c)
    box("Corner_B", -250.0, 2150.0, 0.0, 300.0, 60.0, 300.0, mat="fence", folder=c)
    for k, y in enumerate((-500.0, 600.0)):
        box("Forward_{}".format(k), 700.0, y, 0.0, 200.0, 200.0, 250.0, mat="rock", folder=c)

    # Смотровая вышка: отсюда в кадр влезает всё поле, ядро и турель.
    t = box("Tower", TOWER_AT[0], TOWER_AT[1], 0.0, 400.0, 400.0, TOWER_H, mat="wood", folder="TurretBench/Tower")
    _tags(t, TAG_ROOM, TAG_TOWER)

    _text("Label_Cover", 0.0, 0.0, 420.0, "COVER", size=60.0)
    _text("Label_Tower", TOWER_AT[0], TOWER_AT[1], TOWER_H + 260.0, "TOWER", size=50.0)

    log("ADDED: пол {} x {} м, разметка 5 м, 3 колонны, 2 низкие стенки, угол, 2 передовых блока, вышка; {} мешей".format(
        int(2 * FLOOR_HX / 100), int(2 * FLOOR_HY / 100), hb["_count"]["n"]))
    if not hb["check_kit_centers"]():
        raise RuntimeError("угловой пивот кита посчитан неверно, уровень НЕ сохранён")
    hb["_les"]().save_current_level()


def step_game():
    hb["guard"]()
    hb["_clear"](TAG_GAME)
    eas = hb["_eas"]()
    f = "TurretBench/Gameplay"

    # Старт игрока в 2.6 м СБОКУ от турели, лицом к ней: run_bench скармливает турели ствол прямо
    # отсюда (у кормёжки радиус 3 м), потом поднимает игрока на вышку. Не сзади: враги бьют по
    # турели с -X, и всё, что пролетело мимо неё, летело в игрока (2026-09-22, игрока убивали).
    ps = eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(TURRET_AT[0], TURRET_AT[1] + 260.0, 100.0),
                                    unreal.Rotator(roll=0.0, pitch=0.0, yaw=-90.0))
    hb["_tag"](ps, TAG_GAME, "TB_PlayerStart", f)

    # Ядро: постройка, поставленная прямо в уровень, стоит готовой (ABuildableActor::BeginPlay).
    kit = hb["kit_pivot"]((CORE_AT[0], CORE_AT[1], CORE_SIZE * 0.5), (CORE_SIZE, CORE_SIZE, CORE_SIZE), unreal.Rotator())
    core = eas.spawn_actor_from_class(unreal.SiegeCoreBuildable, kit, unreal.Rotator())
    core.mesh.set_static_mesh(hb["_mesh"](hb["KIT_BOX"]))
    core.mesh.set_material(0, hb["material"]("console"))
    core.set_actor_scale3d(unreal.Vector(CORE_SIZE / 100.0, CORE_SIZE / 100.0, CORE_SIZE / 100.0))
    hb["_tag"](core, TAG_GAME, "TB_SiegeCore", f)
    _tags(core, TAG_GAME, TAG_CORE)
    _text("Label_Core", CORE_AT[0], CORE_AT[1], CORE_SIZE + 150.0, "CORE", tag=TAG_GAME, folder=f)

    # Турель: тоже готовая, но без оружия. Ствол ей даёт игрок (polarity.turret.feed).
    tcls = unreal.EditorAssetLibrary.load_blueprint_class(TURRET_BP)
    if tcls is None:
        raise RuntimeError("нет {}".format(TURRET_BP))
    turret = eas.spawn_actor_from_class(tcls, unreal.Vector(TURRET_AT[0], TURRET_AT[1], 0.0),
                                        unreal.Rotator(roll=0.0, pitch=0.0, yaw=180.0))
    hb["_tag"](turret, TAG_GAME, "TB_Turret", f)
    _tags(turret, TAG_GAME, TAG_TURRET)
    _text("Label_Turret", TURRET_AT[0], TURRET_AT[1], 330.0, "TURRET", tag=TAG_GAME, folder=f)

    # Враги: по спавнеру на класс, каждый держит KEEP_ALIVE живых и заменяет убитых.
    scls = unreal.load_class(None, "/Script/Polarity.KeepAliveSpawner")
    if scls is None:
        raise RuntimeError("класса KeepAliveSpawner нет в бинарнике")
    for label, bp, y, tag, caption in (
            ("TB_Spawner_Rifles", RIFLE_BP, -700.0, TAG_RIFLES, "RIFLES x{}".format(KEEP_ALIVE)),
            ("TB_Spawner_Grenadiers", GRENADIER_BP, 700.0, TAG_GRENADIERS, "GRENADIERS x{}".format(KEEP_ALIVE))):
        ncls = unreal.EditorAssetLibrary.load_blueprint_class(bp)
        if ncls is None:
            raise RuntimeError("нет {}".format(bp))
        sp = eas.spawn_actor_from_class(scls, unreal.Vector(SPAWN_X, y, 120.0),
                                        unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
        sp.set_editor_property("drone_class", ncls)
        sp.set_editor_property("keep_alive", KEEP_ALIVE)
        sp.set_editor_property("respawn_delay", RESPAWN_DELAY)
        hb["_tag"](sp, TAG_GAME, label, f)
        _tags(sp, TAG_GAME, tag)
        _text(label + "_Label", SPAWN_X, y, 330.0, caption, size=55.0, yaw=0.0, tag=TAG_GAME, folder=f)

    nav = eas.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector(0.0, 0.0, 300.0))
    nav.set_actor_scale3d(unreal.Vector((2.0 * FLOOR_HX + 200.0) / 200.0, (2.0 * FLOOR_HY + 200.0) / 200.0, 1200.0 / 200.0))
    hb["_tag"](nav, TAG_GAME, "TB_NavBounds", f)

    log("ADDED: старт игрока у турели, ядро, турель, спавнеры стрелков и гранатомётчиков (по {}), навмеш".format(KEEP_ALIVE))
    hb["_les"]().save_current_level()


def step_navdata():
    """Навданные движок заводит сам (STATIC). Делаем их динамическими, чтобы постройки вырезали
    навмеш и уровень не требовал ручного Build Paths. Спавнить свои нельзя: будут вторые."""
    hb["guard"]()
    recast = [a for a in hb["_eas"]().get_all_level_actors() if isinstance(a, unreal.RecastNavMesh)]
    log("RecastNavMesh на уровне: {}".format(len(recast)))
    for r in recast:
        r.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)
        log("MODIFIED: {} DYNAMIC".format(r.get_actor_label()))
    hb["_les"]().save_current_level()
    return len(recast)


def main():
    step_level()
    step_room()
    step_game()
    log("DONE: {}".format(LEVEL))
