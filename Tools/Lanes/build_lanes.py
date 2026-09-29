"""Сборка карты с линиями в редакторе: уровень, туман, свет-сублевел, ландшафт, разметка, линии, осада.

Рельеф и разметку считает terrain_lanes.py вне редактора (heightmap.png + layout.json).
Концепт: Docs/MOBA_Lanes_Concept_2026-09-29.md.

Запуск через execute_python_code или Tools/mcp.sh (длинный код тул обрезает, а подстроку с
расширением файла принимает за путь, поэтому имя собирается по частям):
    import unreal
    p = r"C:/.../Source/Tools/Lanes/build_lanes" + "." + "py"
    g = {"__name__": "lanes"}; exec(compile(open(p, encoding="utf-8").read(), p, "exec"), g)
    g["step_level"]()   затем step_light, step_fog, step_landscape, step_markup, step_gameplay, step_navdata

Идемпотентно: всё сгенерированное помечено тегом и пересоздаётся заново; ландшафт создаётся один
раз, дальше только переимпорт высот. Чужие акторы и чужие уровни не трогаются (гард уровня).
Света скрипт не ставит (П60): свет берётся сублевелом, тем же, что у L_HomeBase.
"""

import json
import math
import os

import unreal

HERE = r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/Lanes"
LEVEL = "/Game/Prototype/Lanes/L_Lanes"
LIGHT_LEVEL = "/Game/Variant_Shooter/Arenas/ArenaDebug/ArenaLightingDebug3"   # как у L_HomeBase
MAT_DIR = "/Game/Prototype/Lanes/Materials"
GAMEMODE = "/Game/Variant_Shooter/Blueprints/BP_ShooterGameMode"
TAG_GEO = "LanesGen"
TAG_ENV = "LanesEnv"
TAG_GAME = "LanesGame"
OLD_LANDSCAPES = ("LanesTerrain",)   # 630 м, до растяжки вдвое 2026-09-29
NAV_TILE_UU = 4000.0                 # карта 1260 м: при 2000 вышло бы 4000 тайлов, строятся минутами (П30)

KIT_BOX = "/Game/LevelPrototyping/PolygonPrototype/Meshes/Buildings/Simple/SM_Bld_Block_1x1_01"
FLAT_MAT = "/Game/LevelPrototyping/Materials/M_FlatCol"
CYL = "/Engine/BasicShapes/Cylinder"

GRUNT_BP = "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_ShooterNPC"
CARRIER_BP = "/Game/Prototype/HomeBase/BP_KamikazeCarrierDrone"
TANK_BP = "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_TrackedTank"

COLORS = {
    "road": (0.42, 0.34, 0.22), "river": (0.12, 0.25, 0.55), "pad": (0.95, 0.85, 0.10),
    "camp": (0.05, 0.75, 0.75), "base": (0.15, 0.55, 0.20), "enemy": (0.65, 0.12, 0.10),
}


def log(msg):
    print("[LANES] {}".format(msg))


def layout():
    with open(os.path.join(HERE, "layout.json"), encoding="utf-8") as fh:
        return json.load(fh)


def _les():
    return unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)


def _eas():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()


def current_level_path():
    lvl = _les().get_current_level()
    return lvl.get_outer().get_path_name() if lvl else ""


def guard():
    # Подключённый сублевел становится текущим (Python_Editor.md): вернуться в основной, если открыт он.
    if current_level_path().startswith(LIGHT_LEVEL + "."):
        _les().set_current_level_by_name("L_Lanes")
    cur = current_level_path()
    if not cur.startswith(LEVEL + "."):
        raise RuntimeError("открыт {}, а не {}: стоп, чужой уровень не трогаю".format(cur, LEVEL))


def _clear(tag):
    n = 0
    for a in _eas().get_all_level_actors():
        if a.actor_has_tag(tag):
            a.destroy_actor()
            n += 1
    if n:
        log("DELETED: {} акторов с тегом {}".format(n, tag))


def _tag(actor, tag, label, folder):
    actor.set_editor_property("tags", [unreal.Name(tag)])
    actor.set_actor_label(label)
    actor.set_folder_path(folder)
    return actor


# ==================== уровень, свет, туман, ландшафт ====================

def step_level():
    if current_level_path().startswith(LEVEL + "."):
        log("уровень уже открыт")
    else:
        dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
        if dirty:
            raise RuntimeError("есть несохранённые карты {}: стоп".format([p.get_name() for p in dirty]))
        if unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
            _les().load_level(LEVEL)
            log("открыт существующий " + LEVEL)
        else:
            _les().new_level(LEVEL)
            log("CREATED: " + LEVEL)
    guard()
    gm = unreal.EditorAssetLibrary.load_blueprint_class(GAMEMODE)
    _world().get_world_settings().set_editor_property("default_game_mode", gm)
    log("MODIFIED: game mode {}".format(gm.get_name()))
    _les().save_current_level()


def step_light():
    """Свет сублевелом, как у L_HomeBase: скрипт источников света не ставит (П60)."""
    guard()
    world = _world()
    names = [l.get_outer().get_path_name() for l in unreal.EditorLevelUtils.get_levels(world)]
    if any(n.startswith(LIGHT_LEVEL + ".") or n == LIGHT_LEVEL for n in names):
        log("свет-сублевел уже подключён")
        return
    sl = unreal.EditorLevelUtils.add_level_to_world(world, LIGHT_LEVEL, unreal.LevelStreamingDynamic)
    log("ADDED: сублевел света {} ({})".format(LIGHT_LEVEL, sl))
    guard()
    _les().save_current_level()


def step_fog():
    guard()
    _clear(TAG_ENV)
    fog = _eas().spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0))
    fc = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    # Как на L_HomeBase: туман по дистанции, видимость около 150 м.
    fc.set_editor_property("fog_density", 0.06)
    fc.set_editor_property("fog_height_falloff", 0.0005)
    fc.set_editor_property("start_distance", 3000.0)
    fc.set_editor_property("fog_max_opacity", 1.0)
    fc.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(0.55, 0.60, 0.66, 1.0))
    _tag(fog, TAG_ENV, "LN_Fog", "Lanes/Env")
    log("ADDED: туман")
    _les().save_current_level()


def step_landscape():
    guard()
    lay = layout()
    w = lay["world"]
    label = w["landscape_label"]
    # Ландшафт прошлой, вдвое меньшей версии карты: у него другой размер, переимпорт высот ему не
    # подходит. Сносится только он и только на этом уровне (гард выше).
    for a in _eas().get_all_level_actors():
        if isinstance(a, unreal.Landscape) and a.get_actor_label() in OLD_LANDSCAPES:
            log("DELETED: старый ландшафт " + a.get_actor_label())
            a.destroy_actor()
    existing = [l.actor_label for l in (unreal.LandscapeService.list_landscapes() or [])]
    if label not in existing:
        half = lay["half_extent"]
        res = unreal.LandscapeService.create_landscape(
            unreal.Vector(-half, -half, 0.0), unreal.Rotator(0, 0, 0),
            unreal.Vector(w["vertex_spacing_uu"], w["vertex_spacing_uu"], w["z_scale"]),
            sections_per_component=w["sections_per_component"], quads_per_section=w["quads_per_section"],
            component_count_x=w["component_count"], component_count_y=w["component_count"],
            landscape_label=label)
        log("CREATED: ландшафт {} ({})".format(label, res))
    res = unreal.LandscapeService.import_heightmap(label, os.path.join(HERE, "heightmap.png").replace("\\", "/"))
    log("MODIFIED: высоты импортированы: {}".format(res))
    unreal.LandscapeService.set_landscape_collision(label, True)
    _les().save_current_level()


# ==================== разметка ====================

_mesh_cache = {}
_mat_cache = {}


def _mesh(path):
    if path not in _mesh_cache:
        _mesh_cache[path] = unreal.load_asset(path)
    return _mesh_cache[path]


def material(name):
    if name in _mat_cache:
        return _mat_cache[name]
    path = MAT_DIR + "/MI_LN_" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mi = unreal.load_asset(path)
    else:
        fac = unreal.MaterialInstanceConstantFactoryNew()
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "MI_LN_" + name, MAT_DIR, unreal.MaterialInstanceConstant, fac)
        mi.set_editor_property("parent", unreal.load_asset(FLAT_MAT))
        r, g, b = COLORS[name]
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
            mi, "Base Color", unreal.LinearColor(r, g, b, 1.0))
        unreal.MaterialEditingLibrary.update_material_instance(mi)
        unreal.EditorAssetLibrary.save_loaded_asset(mi)
        log("CREATED: " + path)
    _mat_cache[name] = mi
    return mi


def kit_pivot(center, size, rot):
    """Угловой пивот кита (x -100..0, y 0..100, z 0..100), чтобы центр коробки был в center."""
    ml = unreal.MathLibrary
    f, r, u = ml.get_forward_vector(rot), ml.get_right_vector(rot), ml.get_up_vector(rot)
    lx, ly, lz = -size[0] * 0.5, size[1] * 0.5, size[2] * 0.5
    return unreal.Vector(center[0] - (f.x * lx + r.x * ly + u.x * lz),
                         center[1] - (f.y * lx + r.y * ly + u.y * lz),
                         center[2] - (f.z * lx + r.z * ly + u.z * lz))


_count = {"n": 0}


def shape(mesh_path, label, center, size, yaw=0.0, pitch=0.0, mat="road", folder="Lanes", collision=False):
    rot = unreal.Rotator(roll=0.0, pitch=pitch, yaw=yaw)
    loc = kit_pivot(center, size, rot) if mesh_path == KIT_BOX else unreal.Vector(*center)
    act = _eas().spawn_actor_from_class(unreal.StaticMeshActor, loc, rot)
    comp = act.static_mesh_component
    comp.set_static_mesh(_mesh(mesh_path))
    comp.set_material(0, material(mat))
    act.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    if not collision:
        comp.set_collision_profile_name("NoCollision")
    _tag(act, TAG_GEO, label, folder)
    _count["n"] += 1
    return act


def text(label, x, y, z, msg, size=120.0, folder="Lanes/Labels"):
    # Латиница: шрифт TextRenderActor по умолчанию не знает кириллицу (Python_Editor.md).
    act = _eas().spawn_actor_from_class(unreal.TextRenderActor, unreal.Vector(x, y, z),
                                        unreal.Rotator(roll=0.0, pitch=0.0, yaw=180.0))
    tc = act.text_render
    tc.set_editor_property("text", msg)
    tc.set_editor_property("world_size", size)
    tc.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)
    tc.set_editor_property("text_render_color", unreal.Color(255, 230, 120, 255))
    return _tag(act, TAG_GEO, label, folder)


def strip(label, pts, width, mat, folder, lift=8.0, every=1):
    """Полоса поверх ландшафта из отрезков ломаной, без коллизии: только для читаемости."""
    idx = list(range(0, len(pts), every))
    if idx[-1] != len(pts) - 1:
        idx.append(len(pts) - 1)
    for k, (i0, i1) in enumerate(zip(idx[:-1], idx[1:])):
        (x0, y0, z0), (x1, y1, z1) = pts[i0], pts[i1]
        ln = math.hypot(x1 - x0, y1 - y0)
        if ln < 1.0:
            continue
        yaw = math.degrees(math.atan2(y1 - y0, x1 - x0))
        pitch = math.degrees(math.atan2(z1 - z0, ln))
        shape(KIT_BOX, "{}_{:03d}".format(label, k), ((x0 + x1) * 0.5, (y0 + y1) * 0.5, (z0 + z1) * 0.5 + lift),
              (math.hypot(ln, z1 - z0) + 60.0, width, 10.0), yaw=yaw, pitch=pitch, mat=mat, folder=folder)


def densify(pts, step=1500.0):
    out = [pts[0]]
    for (x0, y0, z0), (x1, y1, z1) in zip(pts[:-1], pts[1:]):
        n = max(1, int(math.ceil(math.hypot(x1 - x0, y1 - y0) / step)))
        for k in range(1, n + 1):
            t = k / float(n)
            out.append((x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, z0 + (z1 - z0) * t))
    return out


def _ground_z(x, y, default=0.0):
    hit = unreal.SystemLibrary.line_trace_single(
        _world(), unreal.Vector(x, y, 20000.0), unreal.Vector(x, y, -5000.0),
        unreal.TraceTypeQuery.ECC_VISIBILITY, False, [], unreal.DrawDebugTrace.NONE, True)
    if hit and hit.to_tuple()[0]:
        return hit.to_tuple()[5].z
    return default


def ramp_walls(lay):
    """Стенки вдоль пандусов хайграунда, с коллизией: бок пандуса это обрыв, который у подножия
    сходит на нет, и эти несколько треугольников в полосе 30-50 (П19) закрывает стенка."""
    half_w = lay["hg"]["ramp_half"]
    seg = 300.0
    n = 0
    for r in lay["ramps"]:
        ux, uy = r["ux"], r["uy"]
        nx, ny = -uy, ux
        L = math.hypot(r["x1"] - r["x0"], r["y1"] - r["y0"])
        k = 0
        s_ = -200.0
        while s_ < L:
            s1 = min(s_ + seg, L)
            sm = (s_ + s1) * 0.5
            for side in (-1.0, 1.0):
                cx = r["x0"] + ux * sm + nx * side * (half_w + 30.0)
                cy = r["y0"] + uy * sm + ny * side * (half_w + 30.0)
                top = _ground_z(r["x0"] + ux * sm + nx * side * (half_w - 60.0),
                                r["y0"] + uy * sm + ny * side * (half_w - 60.0)) + 150.0
                low = _ground_z(r["x0"] + ux * sm + nx * side * (half_w + 250.0),
                                r["y0"] + uy * sm + ny * side * (half_w + 250.0)) - 20.0
                h = max(top - low, 60.0)
                shape(KIT_BOX, "RampWall_{}_{}_{:02d}_{}".format(r["base"], r["lane"], k, "L" if side < 0 else "R"),
                      (cx, cy, low + h * 0.5), (s1 - s_ + 20.0, 60.0, h),
                      yaw=math.degrees(math.atan2(uy, ux)), mat="base" if r["base"] == "Base" else "enemy",
                      folder="Lanes/RampWalls", collision=True)
                n += 1
            s_ = s1
            k += 1
    log("стенок вдоль пандусов: {}".format(n))


def step_markup():
    """Полотна линий, река, площадки турелей и кемпов, подписи. Всё без коллизии."""
    guard()
    lay = layout()
    _clear(TAG_GEO)
    _count["n"] = 0
    for name, pts in lay["lanes_full"].items():
        # Шаг 5 м: на бродах и пандусах отрезок в 15 м уходил под землю, полотно рвалось.
        dense = [(x, y, _ground_z(x, y, z)) for x, y, z in densify(pts, 500.0)]
        strip("Road_" + name, dense, lay["lane_w"], "road", "Lanes/Roads")
    # Реки полосой больше нет: русло с дном это сам ландшафт (автор 2026-09-29).
    ramp_walls(lay)
    for p in lay["turret_pads"]:
        z = _ground_z(p["x"], p["y"], p["z"])
        a = shape(CYL, "TurretPad_" + p["name"], (p["x"], p["y"], z + 10.0), (300.0, 300.0, 20.0), mat="pad",
                  folder="Lanes/TurretPads")
        a.set_editor_property("tags", [unreal.Name(TAG_GEO), unreal.Name("TurretPad")])
        text("TurretPad_{}_Label".format(p["name"]), p["x"], p["y"], z + 350.0, p["name"].replace("_", " "))
    for s in lay["poi_slots"]:
        z = _ground_z(s["x"], s["y"], s["z"])
        # Кольцо из восьми коробок по краю площадки: видно, где кусок кемпа, и не мешает ходить.
        for k in range(12):
            a = math.radians(k * 30.0)
            shape(KIT_BOX, "Camp_{}_{:02d}".format(s["name"], k),
                  (s["x"] + s["r"] * math.cos(a), s["y"] + s["r"] * math.sin(a), z + 15.0),
                  (600.0, 60.0, 30.0), yaw=k * 30.0 + 90.0, mat="camp", folder="Lanes/CampSlots")
        text("Camp_{}_Label".format(s["name"]), s["x"], s["y"], z + 400.0, s["name"])
    c = lay["corner"]
    for key, x, y, msg in (("base", -c, -c, "BASE"), ("enemy", c, c, "ENEMY BASE")):
        z = _ground_z(x, y, 0.0)
        for k in range(24):
            a = math.radians(k * 15.0)
            shape(KIT_BOX, "Ring_{}_{:02d}".format(key, k),
                  (x + lay["base_r"] * math.cos(a), y + lay["base_r"] * math.sin(a), z + 15.0),
                  (1500.0, 80.0, 30.0), yaw=k * 15.0 + 90.0, mat=key, folder="Lanes/Bases")
        text("Label_" + key, x, y, z + 800.0, msg, size=300.0)
    log("ADDED: {} мешей разметки".format(_count["n"]))
    _les().save_current_level()


# ==================== игровая часть ====================

WAVE_INTERVAL = 30.0          # только для режима «все линии разом» (bStaggerLanes выкл)
PACK_INTERVAL = 30.0          # по линии за раз, по кругу: соло пачка раз в 30 с, на каждой линии раз в 90 с;
                              # делится на число игроков (автор 2026-09-29), у троих выходит темп доты
LANE_ORDER = ("Mid", "Top", "Bot")   # открывающая на Mid идёт первой, пустые Top и Bot дают паузу 60 с
FIRST_WAVE_DELAY = 0.0        # поставил раздатчик, первая пачка сразу (автор 2026-09-23)
CARRIER_CAP = 6               # по две матки на линию; лишние усиливают живых


def _curve(keys, step=True):
    """FRuntimeFloatCurve из [(x, y), ...]; step=True даёт ступеньки («+1 на 15-й минуте»)."""
    rc = unreal.RuntimeFloatCurve()
    txt = ",".join("(InterpMode={},Time={},Value={})".format("RCIM_Constant" if step else "RCIM_Linear", x, y)
                   for x, y in keys)
    rc.import_text("(EditorCurveData=(Keys=({})))".format(txt))
    return rc


def _kind(cls, base=1, count_keys=None, per_player=0.0, first=1, every=1, last=0, lanes=()):
    k = unreal.SiegeCreepKind()
    k.set_editor_property("npc_class", cls)
    k.set_editor_property("base_count", base)
    if count_keys:
        k.set_editor_property("count_by_minute", _curve(count_keys))
    k.set_editor_property("count_per_player", per_player)
    k.set_editor_property("first_wave", first)
    k.set_editor_property("every_nth_wave", every)
    k.set_editor_property("last_wave", last)
    k.set_editor_property("only_lanes", [unreal.Name(n) for n in lanes])
    return k


def lane_pack():
    """Пачка по доте (числа в FSiegeCreepKind), со сдвигом на открывающую волну автора.

    Волна 1: только открывающая, по грунту на игрока, на средней линии (ближний бой за стволы).
    Дальше: 3 грунта (+1 на 15, 30, 45 мин), матка каждую вторую волну (+1 на 40 мин), танкетка
    с 11-й волны каждую 10-ю (5 мин), вторая с 35 мин. Всё это правится в Details директора."""
    grunt = unreal.EditorAssetLibrary.load_blueprint_class(GRUNT_BP)
    carrier = unreal.EditorAssetLibrary.load_blueprint_class(CARRIER_BP)
    tank = unreal.EditorAssetLibrary.load_blueprint_class(TANK_BP)
    return [
        _kind(grunt, base=0, per_player=1.0, first=1, last=1, lanes=("Mid",)),
        _kind(grunt, count_keys=[(0, 3), (15, 4), (30, 5), (45, 6)], first=2),
        _kind(carrier, count_keys=[(0, 1), (40, 2)], first=3, every=2),
        _kind(tank, count_keys=[(0, 1), (35, 2)], first=11, every=10),
    ]


def step_gameplay():
    guard()
    lay = layout()
    eas = _eas()
    _clear(TAG_GAME)
    f = "Lanes/Gameplay"
    c = lay["corner"]
    bz = _ground_z(-c, -c, lay["base_z"])

    ps = eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-c + 800.0, -c + 800.0, bz + 100.0),
                                    unreal.Rotator(roll=0.0, pitch=0.0, yaw=45.0))
    _tag(ps, TAG_GAME, "LN_PlayerStart", f)

    for name, ln in lay["lanes"].items():
        pts = ln["points"]
        lane = eas.spawn_actor_from_class(unreal.SiegeLane, unreal.Vector(pts[0][0], pts[0][1], pts[0][2]))
        lane.set_editor_property("lane_name", unreal.Name(name))
        # Шаг в колонне больше MinSpawnSeparation директора (500): иначе каждый второй в пачке
        # ждёт ретрая, пока сосед отойдёт.
        lane.set_editor_property("pack_spacing", 600.0)
        spline = lane.get_path()
        spline.clear_spline_points(False)
        for x, y, z in pts:
            spline.add_spline_point(unreal.Vector(x, y, _ground_z(x, y, z) + 50.0), unreal.SplineCoordinateSpace.WORLD, False)
        # Ломаная, а не кривая: дуги углов уже в точках, а авто-касательные на длинной прямой рядом
        # с дугой раздували поворот (567 м сплайна против 514 м полотна).
        for i in range(spline.get_number_of_spline_points()):
            spline.set_spline_point_type(i, unreal.SplinePointType.LINEAR, False)
        spline.update_spline()
        _tag(lane, TAG_GAME, "LN_Lane_" + name, f)
        log("ADDED: линия {} ({} точек, {:.0f} м)".format(name, len(pts), spline.get_spline_length() / 100.0))

    sd = eas.spawn_actor_from_class(unreal.SiegeDirector, unreal.Vector(-c, -c, bz + 400.0))
    sd.set_editor_property("use_lanes", True)
    sd.set_editor_property("lane_pack", lane_pack())
    sd.set_editor_property("wave_interval", WAVE_INTERVAL)
    sd.set_editor_property("first_wave_delay", FIRST_WAVE_DELAY)
    sd.set_editor_property("early_wave_on_lull", False)   # линии идут по часам, как в доте
    sd.set_editor_property("stagger_lanes", True)
    sd.set_editor_property("lane_pack_interval", PACK_INTERVAL)
    sd.set_editor_property("pack_rate_per_player", True)
    sd.set_editor_property("lane_order", [unreal.Name(n) for n in LANE_ORDER])
    sd.set_editor_property("max_carriers_alive", CARRIER_CAP)
    sd.set_editor_property("start_when_dispenser_built", True)
    sd.set_editor_property("auto_collect_spawn_points", False)
    sd.set_editor_property("cores", [])
    _tag(sd, TAG_GAME, "LN_SiegeDirector", f)

    half = lay["half_extent"]
    nav = eas.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector(0.0, 0.0, 1000.0))
    nav.set_actor_scale3d(unreal.Vector(2.0 * half / 200.0, 2.0 * half / 200.0, 6000.0 / 200.0))
    _tag(nav, TAG_GAME, "LN_NavBounds", f)
    log("ADDED: старт игрока, 3 линии, директор осады (пачка {} строк), границы навмеша".format(len(lane_pack())))
    _les().save_current_level()


def step_navdata():
    """Навданные движок заводит сам и со STATIC + тайлом 1000. Правим найденные, не спавним свои (П30)."""
    guard()
    recast = [a for a in _eas().get_all_level_actors() if isinstance(a, unreal.RecastNavMesh)]
    log("RecastNavMesh на уровне: {}".format(len(recast)))
    for r in recast:
        r.set_editor_property("tile_size_uu", NAV_TILE_UU)
        r.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)
        log("MODIFIED: {} tile {:.0f}, DYNAMIC".format(r.get_actor_label(), NAV_TILE_UU))
    _les().save_current_level()
