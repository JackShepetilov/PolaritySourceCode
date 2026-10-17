"""Комнаты-кирпичи будущей карты на L_CombatBench (2026-10-11).

Севернее старого пола (Y от 5500), свой пол и свой навмеш.

PATROL   патруль в низине. Игрок появляется наверху, на уступе 3 м, патруль стоит колонной внизу в
         28 м боком к игроку. Первый ход за игроком. С уступа два спуска: бойцы могут подняться.
OUTPOST  аванпост с тревогой. Гарнизон (задача HOLD) сидит за стенами. Первый выстрел, ранение или
         смерть: тревога. Через ReinforcementDelay (60 с) с севера приходит подмога и остаётся.
         Всех убил до подмоги: взят.

Свои акторы под тегами CombatBenchPatrol и CombatBenchOutpost, остальной стенд не трогает.
Запуск: g = {"__name__": "fr"}; exec(compile(open(p, encoding="utf-8").read(), p, "exec"), g); g["step_field"]()
Потом перезапустить build_bench_hub.py (step_hub): он берёт отсюда STOPS для кнопок телепорта.
"""

import unreal

CB = r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/CombatBench/build_combat_bench.py"
cb = {"__name__": "cb_lib"}
exec(compile(open(CB, encoding="utf-8").read(), CB, "exec"), cb)
hb = cb["hb"]
log, box, wall = hb["log"], hb["box"], hb["wall"]

WALL_H = 500.0
FLOOR = (0.0, 17000.0, 5500.0, 15300.0)      # x0, x1, y0, y1

# ---------------- PATROL ----------------
PX = (500.0, 7500.0)
PY = (6200.0, 12200.0)
RIDGE_H = 300.0
RIDGE_Y1 = PY[0] + 1500.0                   # край уступа
STEP_RISE, STEP_RUN, STAIR_W = 15.0, 40.0, 400.0
PATROL_Y = RIDGE_Y1 + 2800.0                # патруль в 28 м от края
PATROL_X = (3200.0, 3750.0, 4300.0, 4850.0)
# Укрытия низины: (x, y, размер по X, размер по Y, высота). Тонкой стороной к уступу.
PATROL_COVERS = (
    (2500, 9800, 300, 60, 120), (4000, 9600, 300, 60, 120), (5500, 9800, 300, 60, 120),
    (3200, 11200, 300, 60, 220), (4800, 11200, 300, 60, 220),
    (1500, 10500, 300, 60, 220), (6500, 10500, 300, 60, 220),
    (4000, 8700, 300, 60, 120), (2000, 8800, 300, 60, 120), (6000, 8800, 300, 60, 120),
)
PATROL_CONSOLES = (("PATROL x2", 2), ("PATROL x3", 3), ("PATROL x4", 4))

# ---------------- SNIPER ----------------
# Один снайпер на помосте в конце дорожки 57 м [автор, 2026-10-12: комната для проверки и отладки
# блика и правил стрельбы ствола]. Ствол задан пультом, а не жребием. Укрытия игрока 120 см и стоят
# широкой стороной к снайперу: от одного до другого 10-12 м по открытому.
SX = (500.0, 7500.0)
SY = (12400.0, 15100.0)
NEST_H = 300.0
NEST = (6600.0, 7400.0, 13250.0, 14250.0)      # x0, x1, y0, y1 помоста
SNIPER_GUN = "/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation/Marksman/BP_L96X"
SNIPER_COVERS = ((1900, 13750), (3000, 13250), (3000, 14250), (4200, 13750), (5300, 13350), (5300, 14150))

# ---------------- OUTPOST ----------------
# Вторая версия [автор, 2026-10-11: первая была коробкой 20x20 м на одном уровне, все в одном
# укрытии, одна граната на всех]. По образцу аванпостов Far Cry: несколько построек вразброс, три
# уровня высоты, три входа, холм для разведки снаружи, подмога с известной стороны.
#
# Всё ниже задано в метрах от середины площадки: lx на восток, ly на север. Картинка плана:
# Docs/AI_Tactics_2026-10-10/outpost_plan_v2.png.
OX = (8500.0, 16500.0)
OY = (6200.0, 15100.0)
CX, BASE_Y = 12500.0, 11000.0
PLAT = 150.0                                # площадка поднята на 1.5 м
ROOF = 450.0                                # крыша дома (над полом уровня)
TOWER = 550.0                               # настил вышки
FENCE_H = 220.0
# Подоконник и бруствер ниже груди стоящего бойца (106 см): тогда это низкое укрытие, из-за которого
# встают и стреляют поверх. При 120 грудь закрыта, код считал окно высокой стеной и искал выход вбок.
# Low cover. 120: a crouched soldier's head ends at 115 (measured 2026-10-11) and a standing one's
# muzzle is at 129 to 136, so it hides the one and is shot over by the other. It was 100.
SILL_H = 120.0
# Up on the roof and the tower the one looking is below, and the one shooting fires steeply down:
# 115 hides the head from below and leaves the muzzle a line over the edge.
HIGH_SILL_H = 115.0
RISE, RUN = 15.0, 40.0                      # ступень: по ней бойцы ходят (проверено на уступе PATROL)

# Посты: (имя группы, (lx, ly), высота пола под постом). Заселение задаёт пульт: какие посты заняты.
POSTS = {
    # У самого бруствера, а не у мешков в 7 м за ним: с поднятой площадки из глубины не видно того,
    # кто стоит под склоном (мёртвая зона; запись 2026-10-11, 39 выходов из 40 кончились "никого не вижу").
    "gate":  (((-10.0, -17.2), PLAT), ((10.0, -17.2), PLAT), ((-16.0, -17.2), PLAT)),
    "tower": (((-24.5, -5.2), TOWER),),            # вплотную к брустверу: с полутора метров ствол в него упирается
    "house": (((-5.0, 9.1), PLAT), ((3.0, 9.0), ROOF), ((5.0, 9.1), PLAT)),
}
# Земля группы: середина (lx, ly) и радиус в метрах. Группы стоят в 15-25 м друг от друга.
GROUND = {"gate": ((0.0, -13.0), 17.0), "tower": ((-24.5, -3.5), 3.5), "house": ((0.0, 14.0), 11.0)}
REINFORCE = ((-20.0, 36.0), (0.0, 36.0), (20.0, 36.0))
# Пульт: (имя, сколько постов занято в каждой группе, сколько в подмоге). Один и тот же аванпост в
# забеге может быть заселён по-разному.
OUTPOST_CONSOLES = (
    ("OUTPOST 5 +3", {"gate": 2, "tower": 1, "house": 2}, 3),
    ("OUTPOST 7 +3", {"gate": 3, "tower": 1, "house": 3}, 3),
)
APPROACH_COVERS = ((-10.0, -30.0), (8.0, -28.0), (-2.0, -34.0), (18.0, -34.0), (-22.0, -27.0), (26.0, -26.0))

# Остановки для стенки телепортов: (имя, (x, y, yaw, z прибытия), (x, y, z, yaw кнопки возврата)).
STOPS = (
    ("PATROL", ((PX[0] + PX[1]) * 0.5, PY[0] + 700.0, -90.0, RIDGE_H + 100.0),
     ((PX[0] + PX[1]) * 0.5 + 900.0, PY[0] + 60.0, RIDGE_H + 130.0, 90.0)),
    ("OUTPOST", (CX, OY[0] + 700.0, -90.0, 100.0), (CX + 900.0, OY[0] + 60.0, 130.0, 90.0)),
    ("SNIPER", (SX[0] + 600.0, SY[0] + 700.0, -90.0, 100.0), (SX[0] + 1500.0, SY[0] + 60.0, 130.0, 90.0)),
)

# ---------------- CORNER ----------------
# [автор, 2026-10-14] Бой в упор из-за угла: выходишь из-за стены, враг в двух метрах лицом к углу.
# Комната 40x40 м севернее поля, свой пол и навмеш. Стена-угол идёт с запада на восток и обрывается
# в середине комнаты. Север стены: старт (игрок или атакующий NPC). Юг: защитник у самого угла,
# смотрит на угол. Пульты (поворот задаётся тут, не как у _console):
#   CORNER 1v1     защитник против игрока
#   CORNER NPC     защитник (сторона 1) против атакующего (сторона 2), игрок смотрит. Обоим один
#                  ствол, атакующему сказано, где защитник: он сам идёт за угол.
KX = (500.0, 4500.0)
KY = (15800.0, 19800.0)
K_WALL_Y = 17800.0          # стена-угол
K_TIP_X = 2500.0            # конец стены, сам угол
K_WALL_H = 300.0
K_DEFENDER = (K_TIP_X - 200.0, K_WALL_Y - 90.0)    # вплотную к стене: видно только с самого угла, 2-3 м
                                                   # (на 130/160 видели друг друга с 5 м, замер 2026-10-14)
K_START = (1300.0, K_WALL_Y + 500.0)               # старт с севера, лицом на восток
K_GUN = "/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation/ARs/BP_AKX203"
K_MATS = "/Game/Prototype/CombatBench/Materials/MI_BenchTeam_"

CORNER_STOP = ("CORNER", (K_START[0], K_START[1], 0.0, 100.0), (KX[0] + 45.0, K_START[1] + 250.0, 130.0, 0.0))
STOPS = STOPS + (CORNER_STOP,)


def _point(tag, label, x, y, z, yaw, folder):
    tp = hb["_eas"]().spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(x, y, z),
                                             unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
    return hb["_tag"](tp, tag, label, folder)


def _console(tag, title, x, y, z, folder, groups, delay=None):
    enc = hb["_eas"]().spawn_actor_from_class(unreal.BenchEncounter, unreal.Vector(x, y, z),
                                              unreal.Rotator(roll=0.0, pitch=0.0, yaw=90.0))
    hb["_tag"](enc, tag, "Enc_" + title.replace(" ", "_").replace("+", "p"), folder)
    enc.set_editor_property("encounter_name", title)
    if delay is not None:
        enc.set_editor_property("reinforcement_delay", delay)
    enc.set_editor_property("groups", groups)
    return enc


def _group(shooter, points, task, reinforcement=False, hold_radius=0.0, centre=None, weapon=None):
    grp = unreal.BenchEncounterGroup()
    grp.set_editor_property("enemy_class", shooter)
    grp.set_editor_property("spawn_points", points)
    grp.set_editor_property("task", task)
    grp.set_editor_property("reinforcement", reinforcement)
    grp.set_editor_property("hold_radius", hold_radius)
    if centre is not None:
        grp.set_editor_property("hold_centre", centre)
    if weapon is not None:
        grp.set_editor_property("weapon", weapon)      # этот ствол вместо жребия (AShooterNPC::ForceWeapon)
    return grp


# Проёмы скелета забега [автор, 2026-10-17]: из низины PATROL на восток в проход, из прохода в
# аванпост с запада. Дверь в проёме PATROL ставит build_run_glue.py. (от y, до y, верх проёма)
RUN_DOOR_PATROL = (8800.0, 9200.0, 300.0)
RUN_DOOR_OUTPOST = (6400.0, 6800.0, 300.0)


def _room_walls(name, x, y, folder, gaps=None):
    """gaps: {"E": (от, до, верх проёма), ...} проём от пола по стороне."""
    gaps = gaps or {}

    def g(side):
        if side not in gaps:
            return ()
        a, b, head = gaps[side]
        return ((a, b, 0.0, head),)

    wall(name + "_WallS", "y", y[0], x[0], x[1], 0.0, WALL_H, t=60.0, folder=folder, gaps=g("S"))
    wall(name + "_WallN", "y", y[1], x[0], x[1], 0.0, WALL_H, t=60.0, folder=folder, gaps=g("N"))
    wall(name + "_WallW", "x", x[0], y[0], y[1], 0.0, WALL_H, t=60.0, folder=folder, gaps=g("W"))
    wall(name + "_WallE", "x", x[1], y[0], y[1], 0.0, WALL_H, t=60.0, folder=folder, gaps=g("E"))


def _patrol(shooter):
    tag, f = "CombatBenchPatrol", "CombatBench/PATROL"
    hb["TAG_GEO"] = tag
    hb["_clear"](tag)
    eas = hb["_eas"]()

    # Пол и навмеш на оба кирпича кладёт этот шаг (он первый).
    fx0, fx1, fy0, fy1 = FLOOR
    box("Field_Floor", (fx0 + fx1) * 0.5, (fy0 + fy1) * 0.5, -50.0, fx1 - fx0, fy1 - fy0, 50.0, folder=f)
    nav = eas.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector((fx0 + fx1) * 0.5, (fy0 + fy1) * 0.5, 300.0))
    nav.set_actor_scale3d(unreal.Vector((fx1 - fx0) / 200.0, (fy1 - fy0) / 200.0, 10.0))
    hb["_tag"](nav, tag, "Field_NavBounds", f)

    _room_walls("Patrol", PX, PY, f, gaps={"E": RUN_DOOR_PATROL})
    mid = (PX[0] + PX[1]) * 0.5

    # Уступ, бруствер с просветами и два пологих спуска (ступень 15 см: бойцы поднимаются).
    box("Patrol_Ridge", mid, (PY[0] + RIDGE_Y1) * 0.5, 0.0, PX[1] - PX[0], RIDGE_Y1 - PY[0], RIDGE_H, mat="structure", folder=f)
    x = PX[0] + 900.0
    k = 0
    while x + 300.0 < PX[1] - 600.0:
        box("Patrol_Parapet{}".format(k), x + 150.0, RIDGE_Y1 - 40.0, RIDGE_H, 300.0, 60.0, 110.0, mat="cover", folder=f + "/Covers")
        x += 600.0
        k += 1
    steps = int(RIDGE_H / STEP_RISE)
    for side, sx in (("W", PX[0] + STAIR_W * 0.5 + 60.0), ("E", PX[1] - STAIR_W * 0.5 - 60.0)):
        for s in range(steps - 1):
            box("Patrol_Stair{}_{}".format(side, s), sx, RIDGE_Y1 + (s + 0.5) * STEP_RUN, 0.0, STAIR_W, STEP_RUN,
                RIDGE_H - STEP_RISE * (s + 1), mat="structure", folder=f + "/Stairs")

    for k, (cx, cy, sx, sy, h) in enumerate(PATROL_COVERS):
        box("Patrol_Cover{}".format(k), cx, cy, 0.0, sx, sy, h, mat="cover", folder=f + "/Covers")

    # Колонна идёт вдоль X: к уступу боком.
    points = [_point(tag, "Patrol_S{}".format(i), px, PATROL_Y, cb["S_Z"], 0.0, f + "/SpawnPoints")
              for i, px in enumerate(PATROL_X)]

    hb["text"]("Patrol_Title", mid, PY[0] + 80.0, RIDGE_H + 330.0,
               "PATROL\nyou are above, they are in the low ground: you shoot first", size=45.0, yaw=90.0, folder=f, tag=tag)
    for n, (title, count) in enumerate(PATROL_CONSOLES):
        cx = mid + (n - (len(PATROL_CONSOLES) - 1) * 0.5) * 520.0
        _console(tag, title, cx, PY[0] + 60.0, RIDGE_H + 130.0, f,
                 [_group(shooter, points[:count], unreal.SquadTask.ADVANCE)])
        log("ADDED: {}".format(title))
    log("DONE: PATROL")


def _lw(lx, ly):
    """Метры от середины площадки -> мировые сантиметры."""
    return CX + lx * 100.0, BASE_Y + ly * 100.0


def _lbox(label, lx0, ly0, lx1, ly1, z0, h, folder, mat="structure"):
    """Коробка по двум углам в метрах от середины площадки."""
    x0, y0 = _lw(lx0, ly0)
    x1, y1 = _lw(lx1, ly1)
    box(label, (x0 + x1) * 0.5, (y0 + y1) * 0.5, z0, abs(x1 - x0), abs(y1 - y0), h, mat=mat, folder=folder)


def _steps(label, lx, ly, dx, dy, width, top, base, folder, run=RUN):
    """Ступени вниз от края (lx, ly) на высоте top в сторону (dx, dy) до высоты base. width в см."""
    n = int(round((top - base) / RISE))
    ex, ey = _lw(lx, ly)
    for k in range(n - 1):
        cx = ex + dx * (k + 0.5) * run
        cy = ey + dy * (k + 0.5) * run
        sx, sy = (run, width) if dx else (width, run)
        box("{}_{}".format(label, k), cx, cy, 0.0, sx, sy, top - RISE * (k + 1), mat="structure", folder=folder)


def _lwall(label, axis, fixed, a0, a1, z0, h, folder, t=30.0, gaps=()):
    """Стена в метрах от середины площадки. gaps: (от, до, подоконник см, верх проёма см)."""
    if axis == "y":
        fx = BASE_Y + fixed * 100.0
        conv = lambda v: CX + v * 100.0
    else:
        fx = CX + fixed * 100.0
        conv = lambda v: BASE_Y + v * 100.0
    wall(label, axis, fx, conv(a0), conv(a1), z0, h, t=t, folder=folder, mat="structure",
         gaps=tuple((conv(g0), conv(g1), sill, head) for (g0, g1, sill, head) in gaps))


def _outpost(shooter):
    tag, f = "CombatBenchOutpost", "CombatBench/OUTPOST"
    hb["TAG_GEO"] = tag
    hb["_clear"](tag)

    _room_walls("Outpost", OX, OY, f, gaps={"W": RUN_DOOR_OUTPOST})

    # ---- площадка и четыре подъёма на неё ----
    _lbox("Out_Platform", -30, -18, 30, 26, 0.0, PLAT, f)
    _steps("Out_RampS", 0, -18, 0, -1, 6000.0, PLAT, 0.0, f + "/Stairs", run=60.0)     # пологий, во всю ширину
    _steps("Out_RampE", 30, 10, 1, 0, 800.0, PLAT, 0.0, f + "/Stairs")                 # дыра в заборе
    _steps("Out_RampW", -30, -4, -1, 0, 1200.0, PLAT, 0.0, f + "/Stairs")              # из оврага
    _steps("Out_RampN", 0, 26, 0, 1, 800.0, PLAT, 0.0, f + "/Stairs")                  # дорога подмоги

    # ---- забор: ворота на юг, дыра на восток, проход к оврагу на запад, дорога на север ----
    # Южная сторона это бруствер в метр, а не глухой забор: за забором 2.2 м гарнизон сам не видел
    # подхода (запись 2026-10-11: трое из пяти весь бой без линии огня).
    _lwall("Out_FenceS", "y", -18, -30, 30, PLAT, SILL_H, f + "/Fence", gaps=((-6, 6, 0.0, SILL_H),))
    _lwall("Out_FenceN", "y", 26, -30, 30, PLAT, FENCE_H, f + "/Fence", gaps=((-4, 4, 0.0, FENCE_H),))
    _lwall("Out_FenceE", "x", 30, -18, 26, PLAT, FENCE_H, f + "/Fence", gaps=((6, 14, 0.0, FENCE_H),))
    _lwall("Out_FenceW", "x", -30, -18, 26, PLAT, FENCE_H, f + "/Fence", gaps=((-10, 2, 0.0, FENCE_H),))
    # Овраг: стенка вдоль западного края, за ней игрок доходит до западного подъёма незаметно.
    _lwall("Out_Gully", "x", -36, -40, -11, 0.0, FENCE_H, f + "/Fence")

    # ---- дом: первый этаж с окнами и дверьми, плоская крыша с бруствером, лестница снаружи ----
    hh = ROOF - PLAT - 15.0
    _lwall("Out_HouseS", "y", 8, -8, 8, PLAT, hh, f + "/House",
           gaps=((-6.5, -3.5, SILL_H, hh), (-1.5, 1.5, 0.0, hh), (3.5, 6.5, SILL_H, hh)))
    _lwall("Out_HouseN", "y", 20, -8, 8, PLAT, hh, f + "/House", gaps=((-1.5, 1.5, 0.0, hh),))
    _lwall("Out_HouseW", "x", -8, 8, 20, PLAT, hh, f + "/House", gaps=((12, 15, SILL_H, hh),))
    _lwall("Out_HouseE", "x", 8, 8, 20, PLAT, hh, f + "/House")
    _lbox("Out_Roof", -8.15, 7.85, 8.15, 20.15, ROOF - 15.0, 15.0, f + "/House")
    _lwall("Out_RoofParS", "y", 8.1, -8, 8, ROOF, HIGH_SILL_H, f + "/House", t=20.0)
    _lwall("Out_RoofParN", "y", 19.9, -8, 8, ROOF, HIGH_SILL_H, f + "/House", t=20.0)
    _lwall("Out_RoofParW", "x", -7.9, 8, 20, ROOF, HIGH_SILL_H, f + "/House", t=20.0)
    _lwall("Out_RoofParE", "x", 7.9, 8, 17, ROOF, HIGH_SILL_H, f + "/House", t=20.0)
    _lbox("Out_RoofLanding", 8.15, 17, 11, 20, 0.0, ROOF, f + "/House")
    _steps("Out_RoofStair", 9.55, 17, 0, -1, 280.0, ROOF, PLAT, f + "/Stairs")

    # ---- вышка 4 м над площадкой, лестница на восток ----
    _lbox("Out_Tower", -27, -6, -22, -1, 0.0, TOWER, f + "/Tower")
    _lwall("Out_TowerParS", "y", -5.9, -27, -22, TOWER, HIGH_SILL_H, f + "/Tower", t=20.0)
    _lwall("Out_TowerParN", "y", -1.1, -27, -22, TOWER, HIGH_SILL_H, f + "/Tower", t=20.0)
    _lwall("Out_TowerParW", "x", -26.9, -6, -1, TOWER, HIGH_SILL_H, f + "/Tower", t=20.0)
    _steps("Out_TowerStair", -22, -3.5, 1, 0, 250.0, TOWER, PLAT, f + "/Stairs")

    # ---- сарай, контейнеры, мешки у ворот ----
    _lbox("Out_Shed", 16, -4, 26, 2, PLAT, 280.0, f + "/Props")
    for k, (a, b, c, d) in enumerate(((-16, 4, -10, 6.5), (-14, 7, -11.5, 13), (12, 12, 18, 14.5), (2, -3, 8, -0.5))):
        _lbox("Out_Container{}".format(k), a, b, c, d, PLAT, 260.0, f + "/Props")
    for k, lx in enumerate((-12, -4, 4, 12)):
        _lbox("Out_Sandbag{}".format(k), lx - 2, -13, lx + 2, -12.3, PLAT, SILL_H, f + "/Covers", mat="cover")

    # ---- подход: укрытия игрока и холм, с которого аванпост видно до боя ----
    for k, (lx, ly) in enumerate(APPROACH_COVERS):
        _lbox("Out_Approach{}".format(k), lx - 1.5, ly, lx + 1.5, ly + 0.6, 0.0, 120.0, f + "/Covers", mat="cover")
    _lbox("Out_Hill", -34, -44, -14, -36, 0.0, 250.0, f)
    _steps("Out_HillStair", -14, -40, 1, 0, 400.0, 250.0, 0.0, f + "/Stairs")

    # ---- посты, середины земель, подмога ----
    posts, centres = {}, {}
    for group, items in POSTS.items():
        posts[group] = []
        for i, ((lx, ly), floor) in enumerate(items):
            x, y = _lw(lx, ly)
            posts[group].append(_point(tag, "Out_{}_{}".format(group, i), x, y, floor + cb["S_Z"], -90.0, f + "/SpawnPoints"))
        (glx, gly), _r = GROUND[group]
        x, y = _lw(glx, gly)
        centres[group] = _point(tag, "Out_{}_centre".format(group), x, y, PLAT + cb["S_Z"], 0.0, f + "/SpawnPoints")
    relief = []
    for i, (lx, ly) in enumerate(REINFORCE):
        x, y = _lw(lx, ly)
        relief.append(_point(tag, "Out_R{}".format(i), x, y, cb["S_Z"], -90.0, f + "/SpawnPoints"))

    hb["text"]("Outpost_Title", CX, OY[0] + 80.0, 430.0,
               "OUTPOST\nradio alarm on contact: reinforcement from the north in 60 s, and it stays", size=45.0,
               yaw=90.0, folder=f, tag=tag)
    for n, (title, filled, r) in enumerate(OUTPOST_CONSOLES):
        cx = CX + (n - (len(OUTPOST_CONSOLES) - 1) * 0.5) * 520.0
        groups = [_group(shooter, posts[g][:count], unreal.SquadTask.HOLD,
                         hold_radius=GROUND[g][1] * 100.0, centre=centres[g])
                  for g, count in filled.items() if count > 0]
        groups.append(_group(shooter, relief[:r], unreal.SquadTask.ADVANCE, reinforcement=True))
        _console(tag, title, cx, OY[0] + 60.0, 130.0, f, groups, delay=60.0)
        log("ADDED: {}".format(title))
    log("DONE: OUTPOST")


def _sniper(shooter):
    tag, f = "CombatBenchSniper", "CombatBench/SNIPER"
    hb["TAG_GEO"] = tag
    hb["_clear"](tag)
    gun = unreal.EditorAssetLibrary.load_blueprint_class(SNIPER_GUN)
    if gun is None:
        raise RuntimeError("нет ствола " + SNIPER_GUN)

    _room_walls("Sniper", SX, SY, f)
    x0, x1, y0, y1 = NEST
    mid_y = (y0 + y1) * 0.5
    box("Sniper_Nest", (x0 + x1) * 0.5, mid_y, 0.0, x1 - x0, y1 - y0, NEST_H, mat="structure", folder=f)
    # Бруствер к игроку и с севера; с юга лестница (ступень 15 см, бойцы ходят).
    # Толщина 20 см, а не 60: через толстый бруствер вниз не выстрелить. Замер 2026-10-12: плечо 138 см,
    # бруствер 115, цель в 12 м и на 3 м ниже: линия проходит дальний край бруствера на 114 см, то есть
    # упирается в него. При 20 см мёртвая зона начинается ближе 9 м от стрелка, под самой стеной.
    box("Sniper_ParapetW", x0 + 10.0, mid_y, NEST_H, 20.0, y1 - y0, HIGH_SILL_H, mat="cover", folder=f + "/Covers")
    box("Sniper_ParapetN", (x0 + x1) * 0.5, y1 - 10.0, NEST_H, x1 - x0, 20.0, HIGH_SILL_H, mat="cover", folder=f + "/Covers")
    steps = int(NEST_H / STEP_RISE)
    for s in range(steps - 1):
        box("Sniper_Stair_{}".format(s), (x0 + x1) * 0.5, y0 - (s + 0.5) * STEP_RUN, 0.0, 300.0, STEP_RUN,
            NEST_H - STEP_RISE * (s + 1), mat="structure", folder=f + "/Stairs")

    for k, (cx, cy) in enumerate(SNIPER_COVERS):
        box("Sniper_Cover{}".format(k), cx, cy, 0.0, 60.0, 300.0, 120.0, mat="cover", folder=f + "/Covers")

    post = _point(tag, "Sniper_S0", x0 + 62.0, mid_y, NEST_H + cb["S_Z"], 180.0, f + "/SpawnPoints")
    centre = _point(tag, "Sniper_centre", (x0 + x1) * 0.5, mid_y, NEST_H + cb["S_Z"], 0.0, f + "/SpawnPoints")

    hb["text"]("Sniper_Title", SX[0] + 600.0, SY[0] + 80.0, 430.0,
               "SNIPER\none bolt rifle on the nest, 57 m: it holds on you before the shot", size=45.0,
               yaw=90.0, folder=f, tag=tag)
    _console(tag, "SNIPER x1", SX[0] + 600.0, SY[0] + 60.0, 130.0, f,
             [_group(shooter, [post], unreal.SquadTask.HOLD, hold_radius=450.0, centre=centre, weapon=gun)])
    log("ADDED: SNIPER x1")
    log("DONE: SNIPER")


def _corner_console(tag, title, x, y, z, yaw, folder, groups, watch=False):
    enc = hb["_eas"]().spawn_actor_from_class(unreal.BenchEncounter, unreal.Vector(x, y, z),
                                              unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
    hb["_tag"](enc, tag, "Enc_" + title.replace(" ", "_"), folder)
    enc.set_editor_property("encounter_name", title)
    enc.set_editor_property("players_only_watch", watch)
    enc.set_editor_property("groups", groups)
    return enc


def _corner(shooter):
    import math
    tag, f = "CombatBenchCorner", "CombatBench/CORNER"
    hb["TAG_GEO"] = tag
    hb["_clear"](tag)
    eas = hb["_eas"]()
    gun = unreal.EditorAssetLibrary.load_blueprint_class(K_GUN)
    if gun is None:
        raise RuntimeError("нет ствола " + K_GUN)

    x0, x1 = KX
    y0, y1 = KY
    box("Corner_Floor", (x0 + x1) * 0.5, (y0 + y1) * 0.5, -50.0, x1 - x0, y1 - y0, 50.0, folder=f)
    nav = eas.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector((x0 + x1) * 0.5, (y0 + y1) * 0.5, 300.0))
    nav.set_actor_scale3d(unreal.Vector((x1 - x0) / 200.0, (y1 - y0) / 200.0, 10.0))
    hb["_tag"](nav, tag, "Corner_NavBounds", f)
    _room_walls("Corner", KX, KY, f)
    wall("Corner_Wall", "y", K_WALL_Y, x0, K_TIP_X, 0.0, K_WALL_H, t=30.0, folder=f)

    dx, dy = K_TIP_X - K_DEFENDER[0], K_WALL_Y - K_DEFENDER[1]
    face = math.degrees(math.atan2(dy, dx))
    defender = _point(tag, "Corner_Defender", K_DEFENDER[0], K_DEFENDER[1], cb["S_Z"], face, f + "/SpawnPoints")
    attacker = _point(tag, "Corner_Attacker", K_START[0], K_START[1], cb["S_Z"], 0.0, f + "/SpawnPoints")

    def holder(team=255, mat=None):
        g = _group(shooter, [defender], unreal.SquadTask.HOLD, hold_radius=150.0, centre=defender, weapon=gun)
        g.set_editor_property("team", team)
        if mat is not None:
            g.set_editor_property("body_material", mat)
        return g

    mats = {t: unreal.load_asset(K_MATS + str(t)) for t in (1, 2)}
    walker = _group(shooter, [attacker], unreal.SquadTask.ADVANCE, weapon=gun)
    walker.set_editor_property("team", 2)
    walker.set_editor_property("body_material", mats[2])
    walker.set_editor_property("told_where_enemies_are", True)

    hb["text"]("Corner_Title", x0 + 60.0, K_START[1] - 250.0, 330.0,
               "CORNER\ncome round the wall: he is 2 m behind it", size=40.0, yaw=0.0, folder=f, tag=tag)
    _corner_console(tag, "CORNER 1v1", x0 + 60.0, K_START[1] + 450.0, 130.0, 0.0, f, [holder()])
    _corner_console(tag, "CORNER NPC", x0 + 60.0, K_START[1] + 650.0, 130.0, 0.0, f,
                    [holder(1, mats[1]), walker], watch=True)
    # Камера для записи: сверху вдоль стены с востока, видны обе стороны угла (PC.set_view_target_with_blend).
    # Сбоку с севера защитника прятала стена (проверено 2026-10-14).
    cam_at, look_at = (3500.0, 17750.0, 1050.0), (2000.0, 17800.0, 50.0)
    d = [look_at[i] - cam_at[i] for i in range(3)]
    cam_yaw = math.degrees(math.atan2(d[1], d[0]))
    cam_pitch = math.degrees(math.atan2(d[2], math.hypot(d[0], d[1])))
    cam = eas.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(*cam_at),
                                     unreal.Rotator(roll=0.0, pitch=cam_pitch, yaw=cam_yaw))
    hb["_tag"](cam, tag, "Corner_Cam", f)
    log("ADDED: CORNER 1v1, CORNER NPC; defender faces {:.0f} deg".format(face))
    log("DONE: CORNER")


def step_corner():
    hb["guard"]()
    shooter = unreal.EditorAssetLibrary.load_blueprint_class(cb["SHOOTER"])
    if shooter is None:
        raise RuntimeError("нет класса " + cb["SHOOTER"])
    _corner(shooter)
    hb["_les"]().save_current_level()


def step_field():
    hb["guard"]()
    shooter = unreal.EditorAssetLibrary.load_blueprint_class(cb["SHOOTER"])
    if shooter is None:
        raise RuntimeError("нет класса " + cb["SHOOTER"])
    _patrol(shooter)
    _outpost(shooter)
    _sniper(shooter)
    hb["_les"]().save_current_level()
