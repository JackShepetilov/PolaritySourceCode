"""Центр стенда L_CombatBench с телепортами (2026-10-11).

Стенд вырос, пешком долго. У старта игрока стоит стенка с кнопками (ABenchTeleport): выстрел в кнопку
переносит в комнату. В каждой комнате у пультов есть кнопка BACK TO HUB.

Куда переносит:
    комнаты боя     в коридор перед дверью комнаты, лицом в комнату (пульты справа от двери)
    VERSUS, ALLIES  к пультам перед лестницей на галерею
    MOVEMENT        к началу дорожек движения

Свои акторы под тегом CombatBenchHub, остальной стенд не трогает.
Запуск: g = {"__name__": "hub"}; exec(compile(open(p, encoding="utf-8").read(), p, "exec"), g); g["step_hub"]()
"""

import unreal

AR = r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/CombatBench/build_arena_room.py"
ar = {"__name__": "ar_lib"}
exec(compile(open(AR, encoding="utf-8").read(), AR, "exec"), ar)
cb, hb = ar["cb"], ar["hb"]
FR = r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/CombatBench/build_field_rooms.py"
fr = {"__name__": "fr_lib"}
exec(compile(open(FR, encoding="utf-8").read(), FR, "exec"), fr)
log, box, wall = hb["log"], hb["box"], hb["wall"]

TAG = "CombatBenchHub"
HUB_X = 650.0          # стенка с кнопками перед стартом игрока (старт в нуле, смотрит на +X)
STEP = 200.0           # шаг кнопок


def _point(label, x, y, yaw, folder, z=100.0):
    tp = hb["_eas"]().spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(x, y, z),
                                             unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
    return hb["_tag"](tp, TAG, label, folder)


def _button(label, title, x, y, z, yaw, dest, folder):
    b = hb["_eas"]().spawn_actor_from_class(unreal.BenchTeleport, unreal.Vector(x, y, z),
                                            unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
    hb["_tag"](b, TAG, label, folder)
    b.set_editor_property("title", title)
    b.set_editor_property("destination", dest)
    return b


def step_hub():
    hb["guard"]()
    hb["TAG_GEO"] = TAG
    hb["_clear"](TAG)
    f = "CombatBench/Hub"
    corr, room_w, door = cb["CORR"], cb["ROOM_W"], cb["DOOR"]

    home = _point("Hub_Home", 0.0, 0.0, 0.0, f)

    # (название, точка прибытия (x, y, yaw), кнопка возврата (x, y, z, yaw))
    stops = []
    for side in (1, -1):
        for i, room in enumerate(cb["_side_rooms"](side)):
            cx = cb["_room_x0"](i) + room_w * 0.5
            stops.append((room["name"], (cx, side * (corr - 320.0), 90.0 * side),
                          (cx - door * 0.5 - 300.0, side * (corr - 45.0), 130.0, -90.0 * side)))
    for spec in ar["ARENAS"]:
        oy = spec["oy"]
        stops.append((spec["name"], (ar["STAIR_X"] - 450.0, oy, 0.0),
                      (ar["STAIR_X"] - 60.0, oy, 330.0, 180.0)))
    stops.append(("MOVEMENT", (-700.0, 0.0, 180.0), (-1000.0, 350.0, 130.0, 0.0)))
    # Кирпичи карты (build_field_rooms.py): у точки прибытия своя высота, PATROL стоит на уступе.
    stops.extend(fr["STOPS"])

    # Стенка с кнопками. Игрок обходит её с любой стороны, если хочет идти пешком.
    half = (len(stops) - 1) * STEP * 0.5
    wall("Hub_Wall", "x", HUB_X, -half - 250.0, half + 250.0, 0.0, 320.0, t=40.0, folder=f)
    hb["text"]("Hub_Title", HUB_X - 40.0, 0.0, 360.0, "HUB: shoot a button to go there", size=50.0, yaw=180.0,
               folder=f, tag=TAG)

    for n, (name, arrive, (rx, ry, rz, ryaw)) in enumerate(stops):
        key = name.replace(" ", "")
        dx, dy, dyaw = arrive[:3]
        dest = _point("Hub_To_" + key, dx, dy, dyaw, f + "/Points", z=arrive[3] if len(arrive) > 3 else 100.0)
        _button("Hub_Btn_" + key, name, HUB_X - 45.0, -half + n * STEP, 130.0, 180.0, dest, f + "/Buttons")
        _button("Hub_Back_" + key, "BACK TO HUB", rx, ry, rz, ryaw, home, f + "/Back")
        log("ADDED: {} -> ({:.0f}, {:.0f})".format(name, dx, dy))

    hb["_les"]().save_current_level()
    log("DONE: hub, {} stops".format(len(stops)))
