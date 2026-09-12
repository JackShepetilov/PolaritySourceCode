"""Расставить боевые сущности стенда по слотам прод-карты.

Точка на стенде это не форма рельефа, а набор акторов, за которые дерутся: `BP_PoiActor` с ролью,
командой и гарнизоном, ломающийся баннер, лут-якори, а для штабов `BP_FactionHq` с вылазками. Всё
это уже написано в `MapEventBench/build_bench.py` и отлажено на стенде.

Поэтому здесь НЕ копия стенда. Файл стенда исполняется как модуль, у него подменяются три вещи -
путь уровня, метка ландшафта и тег для уборки, - а дальше зовутся его же функции. Копия разошлась бы
с оригиналом через неделю, и правки в одном месте молча не доезжали бы до другого.

Роли берутся из `layout.json`: там уже лежат раскладки по seed'ам, посчитанные `layout.py` по
правилам расстановки штабов и миссий.

    python build_points.py   -> так не запускается, нужен редактор
    В редакторе: exec(compile(open(path).read(), path, "exec"), g); g["main"]()
"""

import json
import os

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
BENCH = os.path.normpath(os.path.join(HERE, "..", "MapEventBench", "build_bench.py"))
LAYOUT = os.path.join(HERE, "layout.json")

# ТЕСТОВЫЙ уровень, отдельно от прода. Всё, что здесь ставится, - проверка драки на шестнадцати
# точках, а не содержимое карты: заглушки, выровненная под ними земля, гарнизоны из стенда. Прод
# собирается из /Game/MapProd/L_ProdTerrain и об этом уровне не знает.
LEVEL_PATH = "/Game/MapProd/Test/L_FightTest"
LANDSCAPE_LABEL = "TestTerrain"
TAG = "MapProdFightTest"

# Потолок населения на фракцию. У стенда 26 при двух штабах и восьми точках; здесь шестнадцать точек
# и четыре штаба, и на 26 гарнизоны съедали лимит целиком - в логе три штаба подряд писали
# "holds its gates: 26/26", а единственная прошедшая вылазка вышла обрезанной вдвое. Масштабировано
# по числу точек, а не выбрано на глаз: 26 * 16 / 8 = 52, взято 60 с запасом на подкрепления.
POP_CAP = 60

# Навмеш строится по ИГРОВОЙ зоне, а не по всему ландшафту, и тайлом покрупнее.
#
# Считано, а не подобрано: 2394 м при тайле 2000 это 120x120 = 14328 тайлов, и Recast молотил их
# 8.06 секунды на каждом запуске. Стенд с его 640 м и тем же тайлом давал 1024 и строился мгновенно.
# Игровая зона 1372 м при тайле 4000 даёт 34x34 = 1176 - тот же порядок, что на стенде. Море, пляж и
# шельф навмеша не требуют: по ним никто не ходит.
NAV_AREA_M = 1372.0
NAV_TILE = 4000.0

# Где игрок появляется. Ставится ОДИН раз в спеке и больше не сбрасывается перестройкой: до этого
# точка входа бралась от стенда, то есть по его координатам, и на этой карте оказывалась у чёрта на
# куличках. Пусто - значит по центру карты, где HUB и куда сходятся связи.
PLAYER_START_M = None

LOG = []


def log(m):
    LOG.append(m)
    unreal.log("[MAPPROD] " + m)


def load_bench():
    """Исполнить стенд как модуль и подменить в нём то, что относится к месту, а не к смыслу.

    Именно подменить, а не скопировать: `spawn_poi`, `spawn_hq`, гарнизоны, баннеры и лут написаны
    один раз и отлажены на стенде. Копия начала бы расходиться с оригиналом с первой же правки.
    """
    g = {"__name__": "mapevent_bench", "__file__": BENCH}
    with open(BENCH, encoding="utf-8") as f:
        exec(compile(f.read(), BENCH, "exec"), g)
    g["LEVEL_PATH"] = LEVEL_PATH
    g["LANDSCAPE_LABEL"] = LANDSCAPE_LABEL
    g["TAG"] = TAG
    return g


def setup_nav(b, eas, land, o, e):
    """Коллизия ландшафта и навмеш. Без этого драка не поедет, что и случилось.

    Три вещи, каждая обязательна:
      - коллизия у ландшафта, иначе навмешу не на чем лежать;
      - NavMeshBoundsVolume на всю карту;
      - у СУЩЕСТВУЮЩЕГО RecastNavMesh поставить DYNAMIC и крупный тайл.

    Последнее именно так: стоит появиться объёму, система навигации сама заводит
    `RecastNavMesh-Default` со STATIC и тайлом 1000 uu - то есть ровно с теми двумя настройками,
    из-за которых карта строится минутами и приезжает наполовину. Свой актор рядом с ним это вторые
    данные навигации, и какие возьмёт движок, скрипт не решает. Поэтому правится тот, что есть.
    """
    unreal.LandscapeService.set_landscape_collision(LANDSCAPE_LABEL, True)

    side = NAV_AREA_M * 100.0
    nav = eas.spawn_actor_from_class(unreal.NavMeshBoundsVolume,
                                     unreal.Vector(o.x, o.y, o.z + 1000.0))
    nav.set_actor_scale3d(unreal.Vector(side / 200.0, side / 200.0, 40.0))
    b["finish"](nav, "PROD_NavBounds", "Nav")

    recast = None
    for a in eas.get_all_level_actors():
        if isinstance(a, unreal.RecastNavMesh):
            recast = a
            break
    if recast is None:
        recast = eas.spawn_actor_from_class(unreal.RecastNavMesh, unreal.Vector(0.0, 0.0, 0.0))
    # НАЧАЛО КООРДИНАТ, а не центр ландшафта. Актор навмеша задаёт начало сетки тайлов, и если оно
    # не кратно размеру тайла, движок каждый запуск пересоздаёт весь навмеш целиком:
    #   "Recreating dtNavMesh instance ... origin (X=119700 Y=119700) not being aligned with
    #    tile size (1999 uu)"
    # На этой карте это стоило 8.2 секунды на старте и валило UCrowdManager. Ландшафт стоит в
    # 119700, что на 1999 не делится; ноль делится на что угодно.
    recast.set_actor_location(unreal.Vector(0.0, 0.0, 0.0), False, False)
    recast.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)
    recast.set_editor_property("tile_size_uu", NAV_TILE)
    b["finish"](recast, "PROD_RecastNavMesh", "Nav")
    n = side / NAV_TILE
    log("навмеш: DYNAMIC, тайл {:.0f} uu, объём {:.0f} м, {:.0f}x{:.0f} = {:.0f} тайлов".format(
        NAV_TILE, side / 100.0, n, n, n * n))


def layout_slots():
    with open(LAYOUT, encoding="utf-8") as f:
        return json.load(f)["slots"]


def place_entry(b, eas, o, slots, origin):
    """Точка входа игрока. По центру карты, если не сказано иначе.

    Раньше звалась стендовая `run_entry`, а она ставит игрока по координатам стенда - на этой карте
    это оказывалось у края, и до драки надо было бежать через полкарты. Каждая перестройка возвращала
    туда же.

    Бросок из моря оставлен: стартовое оружие выдаётся в AShooterCharacter::Landed и только пока идёт
    запуск забега. Без броска игрок появляется безоружным и в войне не участвует вовсе.
    """
    if PLAYER_START_M:
        wx = origin[0] + PLAYER_START_M[0] * 100.0
        wy = origin[1] + PLAYER_START_M[1] * 100.0
    else:
        hub = next((s for s in slots if s["id"] == "HUB"), None)
        wx = origin[0] + (hub["x_m"] if hub else 0.0) * 100.0
        wy = origin[1] + (hub["y_m"] if hub else 0.0) * 100.0

    z = unreal.LandscapeService.get_height_at_location(LANDSCAPE_LABEL, float(wx), float(wy))
    gz = float(getattr(z, "height", 0.0) or 0.0)

    launch = eas.spawn_actor_from_class(
        unreal.RunLaunchPoint, unreal.Vector(wx, wy, gz + 200.0),
        unreal.Rotator(roll=0.0, pitch=25.0, yaw=-90.0))
    launch.set_editor_property("launch_from_sea", True)
    launch.set_editor_property("launch_speed", 2500.0)
    launch.set_editor_property("boss_intro", False)
    launch.set_editor_property("arena_index", 0)
    b["finish"](launch, "PROD_RunLaunchPoint", "MapEvents")

    b["finish"](eas.spawn_actor_from_class(unreal.PlayerStart,
                                           unreal.Vector(wx, wy, gz + 200.0)),
                "PROD_PlayerStart", "MapEvents")
    log("вход игрока: {:.0f},{:.0f} (высота {:.0f})".format(wx, wy, gz))


def world_of(slot, origin):
    """Слот в метрах от центра карты -> мировые юниты. Ландшафт стоит не в нуле мира."""
    return (origin[0] + slot["x_m"] * 100.0, origin[1] + slot["y_m"] * 100.0)


def main(seed_index=0, save=False):
    """save=False по умолчанию: непросохранённая перестройка это бесплатный откат."""
    del LOG[:]
    b = load_bench()

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    # Гард на уровень. Без него скрипт однажды снёс ландшафты в TestLevel: редактор после запуска
    # показывает свой уровень по умолчанию, а не тот, о котором думает автор скрипта.
    cur = les.get_current_level()
    tail = LEVEL_PATH.rsplit("/", 1)[-1]
    if not (cur and cur.get_outer().get_name().endswith(tail)):
        if not les.load_level(LEVEL_PATH):
            raise RuntimeError("не открылся " + LEVEL_PATH)
    cur = les.get_current_level()
    got = cur.get_outer().get_name() if cur else ""
    if not got.endswith(tail):
        raise RuntimeError("не в целевом уровне: " + got)
    log("уровень " + got)

    b["clear_tagged"](eas)

    # Гейммод и точка входа. Без них уровень, созданный скриптом, берёт шаблонный
    # BP_FirstPersonGameMode из DefaultEngine.ini: спавнится не тот пешка и не работает ничего от
    # ран-обвязки. Это записано в Docs/Gotchas/Python_Editor.md и стоило одного захода.
    b["world_settings"](les)

    land = next(a for a in eas.get_all_level_actors() if isinstance(a, unreal.Landscape))
    o, _e = land.get_actor_bounds(False)
    origin = (o.x, o.y)
    log("центр ландшафта {:.0f},{:.0f}".format(*origin))

    setup_nav(b, eas, land, o, _e)
    place_entry(b, eas, o, layout_slots(), origin)

    with open(LAYOUT, encoding="utf-8") as f:
        layout = json.load(f)
    run = layout["runs"][seed_index]
    roles = run["roles"]
    log("раскладка seed {}".format(run["seed"]))

    TEAM_A, TEAM_B, TEAM_N = b["TEAM_A"], b["TEAM_B"], b["TEAM_NEUTRAL"]

    # Фракции берутся из авторских ассетов: A это люди, B это машины. Стенд обесценивается в тот
    # момент, когда две стороны выглядят на земле одинаково.
    people = b["copy_members"](b["SQUAD_A_SOURCE"])
    machines = b["copy_members"](b["SQUAD_B_SOURCE"])
    mk = b["make_loadout"]
    entry = b["entry"]
    task = unreal.SquadInitialTask

    gar_a = mk("DA_Prod_Garrison_A", TEAM_A, task.DEFEND, people)
    sor_a = mk("DA_Prod_Sortie_A", TEAM_A, task.ATTACK, people)
    gar_b = mk("DA_Prod_Garrison_B", TEAM_B, task.DEFEND,
               [entry(b["DRONE_BP"], 3, commander=True)])
    sor_b = mk("DA_Prod_Sortie_B", TEAM_B, task.ATTACK, machines)
    weak_a = mk("DA_Prod_Weak_A", TEAM_A, task.ATTACK,
                [entry(b["SHOOTER_BP"], 2, commander=True)])
    weak_b = mk("DA_Prod_Weak_B", TEAM_B, task.ATTACK,
                [entry(b["DRONE_BP"], 2, commander=True)])
    # Подкрепление это СОСТАВ гарнизона с МАРШЕВОЙ задачей. Гарнизонный ассет, посланный
    # подкреплением, не уходит от ворот: подсистема двигает только отряды с задачей Attack.
    rel_a = mk("DA_Prod_Relief_A", TEAM_A, task.ATTACK, people)
    rel_b = mk("DA_Prod_Relief_B", TEAM_B, task.ATTACK,
               [entry(b["DRONE_BP"], 3, commander=True)])

    b["ensure_sheet_bp"](b["SHEET_MONEY"],
                         [b["loot_entry"](b["CURRENCY_BP"], 1, True),
                          b["loot_entry"](b["AMMO_BP"], 2, False)])
    b["ensure_sheet_bp"](b["SHEET_AMMO"], [b["loot_entry"](b["AMMO_BP"], 3, False)])
    rich = [b["sheet_option"](b["SHEET_MONEY"], 1.0), b["sheet_option"](b["SHEET_AMMO"], 2.0)]
    poor = [b["sheet_option"](b["SHEET_AMMO"], 1.0)]

    reward = unreal.FinalConditions()
    reward.set_editor_property("wave_delta", -1)
    reward.set_editor_property("arrival_delay_seconds", 20.0)
    reward.set_editor_property("entry_quality", 1)

    b["make_banner_assets"]()

    counts = {"hq": 0, "mission": 0, "plain": 0, "final": 0}
    for slot in layout["slots"]:
        sid = slot["id"]
        role = roles.get(sid, "NEUTRAL")
        pos = world_of(slot, origin)
        # Радиус влияния по рангу места: узел больше буфера, это и на рельефе так.
        radius = {"hub": 5000.0, "major": 4000.0, "buffer": 2600.0}.get(slot["tier"], 4000.0)

        if role == "HQ_A":
            hq = b["spawn_hq"](eas, sid, pos, TEAM_A, sor_a, weak_a, gar_a,
                               anchors=3, anchor_chance=0.7, sheet_mix=poor, relief_loadout=rel_a)
            hq.set_editor_property("faction_population_cap", POP_CAP)
            counts["hq"] += 1
        elif role == "HQ_B":
            hq = b["spawn_hq"](eas, sid, pos, TEAM_B, sor_b, weak_b, gar_b,
                               anchors=3, anchor_chance=0.7, sheet_mix=poor, relief_loadout=rel_b)
            hq.set_editor_property("faction_population_cap", POP_CAP)
            counts["hq"] += 1
        elif role == "MISSION":
            # Миссии достаются нейтральными и с гарнизоном той стороны, что ближе по раскладке:
            # точка, стоящая пустой, не даёт войне повода начаться.
            b["spawn_poi"](eas, sid, pos, unreal.PoiRole.MISSION, TEAM_N,
                           garrison=gar_a if slot["sector"] is not None and slot["sector"] % 2 == 0
                           else gar_b,
                           radius=radius, banner=True, anchors=4, anchor_chance=0.55,
                           sheet_mix=rich, mission_kind=unreal.MissionKind.ELIMINATION,
                           reward=reward)
            counts["mission"] += 1
        elif sid == "HUB":
            b["spawn_poi"](eas, sid, pos, unreal.PoiRole.FINAL, TEAM_N, radius=radius)
            counts["final"] += 1
        else:
            # Эвакуации и нейтральные точки: лут и повод драться, без баннера.
            b["spawn_poi"](eas, sid, pos, unreal.PoiRole.PLAIN, TEAM_N, radius=radius,
                           anchors=4 if role == "EXTRACT" else 3, anchor_chance=0.45,
                           sheet_mix=rich if role == "EXTRACT" else poor)
            counts["plain"] += 1

    log("поставлено: штабов {hq}, миссий {mission}, обычных {plain}, финал {final}".format(**counts))

    if save:
        les.save_current_level()
        log("уровень сохранён")
    else:
        log("НЕ сохранено - посмотри и сохрани сам, если годится")
    return "\n".join(LOG)
