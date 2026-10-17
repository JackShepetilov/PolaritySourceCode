"""Клей скелета забега на L_CombatBench: PATROL -> проход -> OUTPOST (2026-10-17).

Docs/Run_Skeleton_Plan_2026-10-17.md, раздел 9.

Что ставит (всё под тегом CombatBenchRun, остальной стенд не трогает):
    проход 10 м между восточной стеной PATROL и западной стеной OUTPOST, закрытый с торцов;
    дверь (оранжевая коробка) в проёме низины PATROL: пропадает, когда патруль перебит;
    свои бои RUN PATROL и RUN OUTPOST (ABenchEncounter без кнопки, число врагов на игрока);
    комнаты ARunRoom 1 PATROL и 2 OUTPOST: зона входа, точка старта, двери;
    кнопку RUN START на стенке хаба.

Проёмы в стенах комнат задаёт build_field_rooms.py (RUN_DOOR_PATROL, RUN_DOOR_OUTPOST). На уровне,
собранном до 2026-10-17, стены целые: шаг режет их сам (одна стена в 2 куска + перемычка над проёмом,
тег и подписи те же, что дал бы step_field). После перезапуска step_field этот шаг надо прогнать снова.

Запуск: Tools/ue_step.sh CombatBench build_run_glue step_glue
Проверка без правок: Tools/ue_step.sh CombatBench build_run_glue check
"""

import unreal

FR = r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/CombatBench/build_field_rooms.py"
fr = {"__name__": "fr_lib"}
exec(compile(open(FR, encoding="utf-8").read(), FR, "exec"), fr)
cb, hb = fr["cb"], fr["hb"]
log, box, wall = hb["log"], hb["box"], hb["wall"]

LEVEL = "/Game/Prototype/CombatBench/L_CombatBench"
TAG = "CombatBenchRun"
F = "CombatBench/RUN"
SHOOTER = cb["SHOOTER"]

PX, PY, OX, OY = fr["PX"], fr["PY"], fr["OX"], fr["OY"]
WALL_H = fr["WALL_H"]
DOOR_P = fr["RUN_DOOR_PATROL"]
DOOR_O = fr["RUN_DOOR_OUTPOST"]

# Проход: между внутренними гранями стен комнат (стены 60 см по осям PX[1] и OX[0]).
LANE_X = (PX[1], OX[0])
LANE_Y = (PY[0], DOOR_P[1] + 200.0)

# Числа на одного игрока [автор, 2026-10-17]: засада на патруль 3, аванпост 3 + 2 подмога.
PATROL_PER_PLAYER = 3
OUTPOST_PER_POST_GROUP = 1        # у ворот, на вышке, в доме: по одному на игрока = 3
OUTPOST_RELIEF_PER_PLAYER = 2


def _guard():
    sub = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if sub.get_game_world():
        raise RuntimeError("идёт PIE: стоп")
    lvl = hb["_les"]().get_current_level()
    cur = lvl.get_outer().get_path_name() if lvl else ""
    if not cur.startswith(LEVEL + "."):
        raise RuntimeError("открыт {}, а не {}: стоп".format(cur, LEVEL))
    dirty = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
    others = [d for d in dirty if d != LEVEL]
    if others:
        raise RuntimeError("есть чужие несохранённые карты {}: стоп".format(others))


def _by_label():
    return {a.get_actor_label(): a for a in hb["_eas"]().get_all_level_actors()}


def _point(label, x, y, z, yaw):
    tp = hb["_eas"]().spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(x, y, z),
                                             unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
    return hb["_tag"](tp, TAG, label, F + "/Points")


def _group(points, task, per_player, reinforcement=False, hold_radius=0.0, centre=None):
    grp = unreal.BenchEncounterGroup()
    grp.set_editor_property("enemy_class", unreal.EditorAssetLibrary.load_blueprint_class(SHOOTER))
    grp.set_editor_property("spawn_points", points)
    grp.set_editor_property("task", task)
    grp.set_editor_property("reinforcement", reinforcement)
    grp.set_editor_property("hold_radius", hold_radius)
    grp.set_editor_property("count_per_player", per_player)
    if centre is not None:
        grp.set_editor_property("hold_centre", centre)
    return grp


def _encounter(title, x, y, z, yaw, groups, delay=None):
    enc = hb["_eas"]().spawn_actor_from_class(unreal.BenchEncounter, unreal.Vector(x, y, z),
                                              unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
    hb["_tag"](enc, TAG, "Run_Enc_" + title.replace(" ", "_"), F)
    enc.set_editor_property("encounter_name", title)
    enc.set_editor_property("button_enabled", False)
    if delay is not None:
        enc.set_editor_property("reinforcement_delay", delay)
    enc.set_editor_property("groups", groups)
    return enc


def _room(index, name, x, y, z, extent, encounter, doors, start, last=False):
    room = hb["_eas"]().spawn_actor_from_class(unreal.RunRoom, unreal.Vector(x, y, z),
                                               unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    hb["_tag"](room, TAG, "Run_Room{}_{}".format(index, name), F)
    room.set_editor_property("room_index", index)
    room.set_editor_property("room_name", name)
    room.set_editor_property("entry_extent", unreal.Vector(*extent))
    room.set_editor_property("encounter", encounter)
    room.set_editor_property("exit_doors", doors)
    room.set_editor_property("start_point", start)
    room.set_editor_property("last_room", last)
    return room


# ==================== стены комнат ====================

def _split_wall(label, room_tag, axis, fixed, a0, a1, gap, folder):
    """Целую стену комнаты (старая сборка) заменить стеной с проёмом. Уже с проёмом: ничего."""
    actors = _by_label()
    whole = actors.get(label + "_0")
    if whole is None:
        raise RuntimeError("нет стены {}_0: сначала step_field".format(label))
    if (label + "_1") in actors:
        log("стена {} уже с проёмом".format(label))
        return
    if not whole.actor_has_tag(room_tag):
        raise RuntimeError("{}_0 без тега {}: чужая, не трогаю".format(label, room_tag))
    _o, ext = whole.get_actor_bounds(False)
    span = ext.y if axis == "x" else ext.x
    if abs(span * 2.0 - (a1 - a0)) > 100.0:
        raise RuntimeError("{}_0 длиной {:.0f}, а не {:.0f}: двигали руками, не трогаю".format(label, span * 2.0, a1 - a0))
    whole.destroy_actor()
    old = hb["TAG_GEO"]
    hb["TAG_GEO"] = room_tag
    try:
        wall(label, axis, fixed, a0, a1, 0.0, WALL_H, t=60.0, folder=folder, gaps=((gap[0], gap[1], 0.0, gap[2]),))
    finally:
        hb["TAG_GEO"] = old
    log("MODIFIED: {} с проёмом {:.0f}..{:.0f}".format(label, gap[0], gap[1]))


# ==================== шаги ====================

def check():
    """Что стоит в полосе прохода (кроме своего) и в каком виде стены комнат."""
    _guard()
    actors = hb["_eas"]().get_all_level_actors()
    hits = []
    for a in actors:
        if a.actor_has_tag(TAG):
            continue
        p = a.get_actor_location()
        if LANE_X[0] + 40 < p.x < LANE_X[1] - 40 and LANE_Y[0] < p.y < LANE_Y[1]:
            hits.append("{} ({:.0f}, {:.0f}, {:.0f})".format(a.get_actor_label(), p.x, p.y, p.z))
    log("в полосе прохода чужих: {} {}".format(len(hits), hits))
    labels = _by_label()
    for name in ("Patrol_WallE", "Outpost_WallW", "Hub_Wall_0"):
        log("{}: {}".format(name, [k for k in labels if k.startswith(name)]))
    return hits


def step_glue():
    _guard()
    if check():
        raise RuntimeError("в полосе прохода что-то стоит: стоп, смотреть глазами")
    hb["_clear"](TAG)
    hb["TAG_GEO"] = TAG
    sz = cb["S_Z"]

    # ---- стены комнат с проёмами ----
    _split_wall("Patrol_WallE", "CombatBenchPatrol", "x", PX[1], PY[0], PY[1], DOOR_P, "CombatBench/PATROL")
    _split_wall("Outpost_WallW", "CombatBenchOutpost", "x", OX[0], OY[0], OY[1], DOOR_O, "CombatBench/OUTPOST")

    # ---- проход: торцы и дверь ----
    wall("Run_LaneCapS", "y", LANE_Y[0], LANE_X[0], LANE_X[1], 0.0, WALL_H, t=60.0, folder=F)
    wall("Run_LaneCapN", "y", LANE_Y[1], LANE_X[0], LANE_X[1], 0.0, WALL_H, t=60.0, folder=F)
    door = box("Run_DoorPatrol", PX[1], (DOOR_P[0] + DOOR_P[1]) * 0.5, 0.0, 60.0, DOOR_P[1] - DOOR_P[0], DOOR_P[2],
               mat="console", folder=F)
    log("ADDED: проход и дверь")

    # ---- PATROL: колонна в три ряда, 12 точек (на четверых нужно 9) ----
    patrol_y = fr["PATROL_Y"]
    pts = []
    for row, dy in enumerate((0.0, 350.0, 700.0)):
        for i, px in enumerate(fr["PATROL_X"]):
            pts.append(_point("Run_Patrol_S{}_{}".format(row, i), px, patrol_y + dy, sz, 0.0))
    enc_p = _encounter("RUN PATROL", (PX[0] + PX[1]) * 0.5 - 1800.0, PY[0] + 60.0, fr["RIDGE_H"] + 130.0, 90.0,
                       [_group(pts, unreal.SquadTask.ADVANCE, PATROL_PER_PLAYER)])

    # ---- OUTPOST: посты как у пультов аванпоста, подмога 6 точек ----
    lw = fr["_lw"]
    groups = []
    for g, items in fr["POSTS"].items():
        posts = []
        for i, ((lx, ly), floor) in enumerate(items):
            x, y = lw(lx, ly)
            posts.append(_point("Run_Out_{}_{}".format(g, i), x, y, floor + sz, -90.0))
        (glx, gly), radius = fr["GROUND"][g]
        cx, cy = lw(glx, gly)
        centre = _point("Run_Out_{}_centre".format(g), cx, cy, fr["PLAT"] + sz, 0.0)
        groups.append(_group(posts, unreal.SquadTask.HOLD, OUTPOST_PER_POST_GROUP,
                             hold_radius=radius * 100.0, centre=centre))
    relief = []
    for i, (lx, ly) in enumerate(list(fr["REINFORCE"]) + [(lx, ly + 3.0) for (lx, ly) in fr["REINFORCE"]]):
        x, y = lw(lx, ly)
        relief.append(_point("Run_Out_R{}".format(i), x, y, sz, -90.0))
    groups.append(_group(relief, unreal.SquadTask.ADVANCE, OUTPOST_RELIEF_PER_PLAYER, reinforcement=True))
    enc_o = _encounter("RUN OUTPOST", LANE_X[0] + 60.0, LANE_Y[0] + 900.0, 130.0, 0.0, groups, delay=60.0)
    log("ADDED: бои RUN PATROL, RUN OUTPOST")

    # ---- комнаты ----
    mid = (PX[0] + PX[1]) * 0.5
    ridge = fr["RIDGE_H"]
    start_p = _point("Run_Start1_PATROL", mid, PY[0] + 700.0, ridge + 100.0, 90.0)
    _room(1, "PATROL", mid, (PY[0] + fr["RIDGE_Y1"]) * 0.5, ridge + 150.0,
          ((PX[1] - PX[0]) * 0.5 - 200.0, (fr["RIDGE_Y1"] - PY[0]) * 0.5 - 100.0, 250.0),
          enc_p, [door], start_p)
    lane_cx = (LANE_X[0] + LANE_X[1]) * 0.5
    start_o = _point("Run_Start2_OUTPOST", lane_cx, DOOR_O[1] + 600.0, 100.0, -90.0)
    _room(2, "OUTPOST", lane_cx, (LANE_Y[0] + LANE_Y[1]) * 0.5, 250.0,
          ((LANE_X[1] - LANE_X[0]) * 0.5 - 60.0, (LANE_Y[1] - LANE_Y[0]) * 0.5 - 60.0, 250.0),
          enc_o, [], start_o, last=True)
    log("ADDED: комнаты 1 PATROL, 2 OUTPOST")

    # ---- RUN START на стенке хаба, над рядом кнопок телепорта ----
    hub = _by_label().get("Hub_Wall_0")
    hx = hub.get_actor_location().x if hub else 650.0
    btn = hb["_eas"]().spawn_actor_from_class(unreal.RunStartButton, unreal.Vector(hx - 45.0, 0.0, 230.0),
                                              unreal.Rotator(roll=0.0, pitch=0.0, yaw=180.0))
    hb["_tag"](btn, TAG, "Run_StartButton", F)
    btn.set_editor_property("first_room", 1)
    log("ADDED: RUN START у x={:.0f}".format(hx - 45.0))

    hb["_les"]().save_current_level()
    log("DONE: клей забега")
