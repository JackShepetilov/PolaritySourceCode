"""Сборка прототипа базы игрока в редакторе: уровень, свет, ландшафт, постройки, волны.

Рельеф и раскладку считает terrain_homebase.py вне редактора (heightmap.png + layout.json).

Запуск через execute_python_code (длинный код тул обрезает, а подстроку с расширением файла
принимает за путь, поэтому имя собирается по частям):
    import unreal
    p = r"C:/.../Source/Tools/HomeBase/build_homebase" + "." + "py"
    g = {"__name__": "hb"}; exec(compile(open(p, encoding="utf-8").read(), p, "exec"), g)
    g["step_level"]()        затем step_fog, step_landscape, step_geo, step_gameplay, по одному

Идемпотентно: всё сгенерированное помечено тегом и пересоздаётся заново; ландшафт создаётся один
раз, дальше только переимпорт высот. Чужие акторы и чужие уровни не трогаются (гард уровня).
"""

import json
import math
import os
import random

import unreal

HERE = r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/HomeBase"
LEVEL = "/Game/Prototype/HomeBase/L_HomeBase"
MAT_DIR = "/Game/Prototype/HomeBase/Materials"
CARRIER_BP = "/Game/Prototype/HomeBase/BP_KamikazeCarrierDrone"
GAMEMODE = "/Game/Variant_Shooter/Blueprints/BP_ShooterGameMode"
TAG_GEO = "HomeBaseGen"
TAG_ENV = "HomeBaseEnv"
TAG_GAME = "HomeBaseGame"

# Коробки блокаута только из кита с пивотом в углу пола (правило автора): меш лежит в локальных
# x -100..0, y 0..100, z 0..100, поэтому ставится не центром, а углом. Цилиндры и сферы движковые.
KIT_BOX = "/Game/LevelPrototyping/PolygonPrototype/Meshes/Buildings/Simple/SM_Bld_Block_1x1_01"
FLAT_MAT = "/Game/LevelPrototyping/Materials/M_FlatCol"
CUBE = KIT_BOX
CYL = "/Engine/BasicShapes/Cylinder"
SPHERE = "/Engine/BasicShapes/Sphere"

COLORS = {
    "structure": (0.55, 0.55, 0.55), "wood": (0.45, 0.30, 0.18), "rock": (0.33, 0.27, 0.23),
    "pad": (0.05, 0.75, 0.65), "leaf": (0.20, 0.42, 0.16), "trunk": (0.28, 0.19, 0.11),
    "fence": (0.85, 0.85, 0.80), "cover": (0.62, 0.52, 0.30), "console": (0.95, 0.40, 0.08),
    "road": (0.30, 0.29, 0.27),
}


def log(msg):
    print("[HOMEBASE] {}".format(msg))


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
    cur = current_level_path()
    if not cur.startswith(LEVEL + "."):
        raise RuntimeError("открыт {}, а не {}: стоп, чужой уровень не трогаю".format(cur, LEVEL))


# ==================== уровень и окружение ====================

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


def step_fog():
    """Только туман. Света скрипт не ставит: свет автор держит отдельным сублевелом (П60)."""
    guard()
    _clear(TAG_ENV)
    eas = _eas()
    # Туман по дистанции, а не низовой: почти без спада по высоте, иначе с макушки видно поверх.
    fog = eas.spawn_actor_from_class(unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0))
    fc = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
    fc.set_editor_property("fog_density", 0.06)
    fc.set_editor_property("fog_height_falloff", 0.0005)
    fc.set_editor_property("start_distance", 3000.0)
    fc.set_editor_property("fog_max_opacity", 1.0)
    fc.set_editor_property("fog_inscattering_luminance", unreal.LinearColor(0.55, 0.60, 0.66, 1.0))
    _tag(fog, TAG_ENV, "HB_Fog", "HomeBase/Env")
    log("ADDED: туман")
    _les().save_current_level()


def step_landscape():
    guard()
    lay = layout()
    w = lay["world"]
    label = w["landscape_label"]
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


# ==================== примитивы ====================

_mesh_cache = {}
_mat_cache = {}


def _mesh(path):
    if path not in _mesh_cache:
        _mesh_cache[path] = unreal.load_asset(path)
    return _mesh_cache[path]


def material(name):
    if name in _mat_cache:
        return _mat_cache[name]
    path = MAT_DIR + "/MI_HB_" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mi = unreal.load_asset(path)
    else:
        fac = unreal.MaterialInstanceConstantFactoryNew()
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "MI_HB_" + name, MAT_DIR, unreal.MaterialInstanceConstant, fac)
        mi.set_editor_property("parent", unreal.load_asset(FLAT_MAT))
        r, g, b = COLORS[name]
        unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
            mi, "Base Color", unreal.LinearColor(r, g, b, 1.0))
        unreal.MaterialEditingLibrary.update_material_instance(mi)
        unreal.EditorAssetLibrary.save_loaded_asset(mi)
        log("CREATED: " + path)
    _mat_cache[name] = mi
    return mi


_count = {"n": 0}


def kit_pivot(center, size, rot):
    """Где поставить угловой пивот кита, чтобы центр коробки оказался в center.
    Центр кита в локальных координатах после масштаба: (-sx/2, sy/2, sz/2). Оси берутся у движка,
    а не своей матрицей, чтобы не гадать со знаками поворота."""
    ml = unreal.MathLibrary
    f, r, u = ml.get_forward_vector(rot), ml.get_right_vector(rot), ml.get_up_vector(rot)
    lx, ly, lz = -size[0] * 0.5, size[1] * 0.5, size[2] * 0.5
    return unreal.Vector(center[0] - (f.x * lx + r.x * ly + u.x * lz),
                         center[1] - (f.y * lx + r.y * ly + u.y * lz),
                         center[2] - (f.z * lx + r.z * ly + u.z * lz))


def shape(mesh_path, label, center, size, yaw=0.0, pitch=0.0, roll=0.0, mat="structure",
          folder="HomeBase", collision=True):
    rot = unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw)
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
    if mesh_path == KIT_BOX:
        _checks.append((act, center))
    return act


_checks = []


def check_kit_centers():
    """Доказательство, что угол кита посчитан верно: центр рамки актора против заданного центра.
    Для повёрнутых коробок это и есть проверка знаков поворота."""
    worst = (0.0, "")
    for act, c in _checks:
        origin, _ = act.get_actor_bounds(False)
        err = math.sqrt((origin.x - c[0]) ** 2 + (origin.y - c[1]) ** 2 + (origin.z - c[2]) ** 2)
        if err > worst[0]:
            worst = (err, act.get_actor_label())
    log("кит: {} коробок, худшее расхождение центра {:.1f} uu ({})".format(len(_checks), worst[0], worst[1]))
    return worst[0] < 2.0


def box(label, x, y, z0, sx, sy, sz, **kw):
    """Коробка по нижней грани: z0 это низ, не центр."""
    return shape(CUBE, label, (x, y, z0 + sz * 0.5), (sx, sy, sz), **kw)


def wall(label, axis, fixed, a0, a1, z0, h, t=30.0, gaps=(), **kw):
    """Стена вдоль оси. axis="x": стоит на x=fixed и тянется по y от a0 до a1; axis="y" наоборот.
    gaps: (от, до, подоконник, верх проёма), в координатах вдоль стены."""
    pieces = []
    cur = a0
    for g0, g1, sill, head in sorted(gaps):
        if g0 > cur:
            pieces.append((cur, g0, 0.0, h))
        if sill > 0:
            pieces.append((g0, g1, 0.0, sill))
        if head < h:
            pieces.append((g0, g1, head, h))
        cur = g1
    if cur < a1:
        pieces.append((cur, a1, 0.0, h))
    for i, (p0, p1, zb, zt) in enumerate(pieces):
        mid, ln = (p0 + p1) * 0.5, p1 - p0
        if axis == "x":
            box("{}_{}".format(label, i), fixed, mid, z0 + zb, t, ln, zt - zb, **kw)
        else:
            box("{}_{}".format(label, i), mid, fixed, z0 + zb, ln, t, zt - zb, **kw)


def text(label, x, y, z, msg, size=60.0, yaw=180.0, folder="HomeBase/Labels", tag=TAG_GEO):
    act = _eas().spawn_actor_from_class(unreal.TextRenderActor, unreal.Vector(x, y, z),
                                        unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
    tc = act.text_render
    tc.set_editor_property("text", msg)
    tc.set_editor_property("world_size", size)
    tc.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)
    tc.set_editor_property("text_render_color", unreal.Color(255, 230, 120, 255))
    return _tag(act, tag, label, folder)


def tree(label, x, y, z, big=False, folder="HomeBase/Trees"):
    th, cd = (450.0, 520.0) if big else (350.0, 400.0)
    box_c = shape(CYL, label + "_Trunk", (x, y, z + th * 0.5 - 20.0), (40.0, 40.0, th), mat="trunk", folder=folder)
    shape(SPHERE, label + "_Crown", (x, y, z + th + cd * 0.3), (cd, cd, cd * 0.85), mat="leaf", folder=folder)
    return box_c


def polar(a_deg, r):
    a = math.radians(a_deg)
    return r * math.cos(a), r * math.sin(a)


def rim_at(lay, a_deg):
    key = str(int(round(a_deg / 5.0) * 5) % 360)
    return lay["rim_r"][key]


# ==================== постройки ====================

def build_house(H):
    f = "HomeBase/House"
    # Дом 9 x 12 м, стены 3.5 м. Двери на север и на юг (к крыльцу), окна на восток и запад.
    wall("House_N", "x", 435.0, -600.0, 600.0, H, 350.0, gaps=[(-100.0, 100.0, 0.0, 250.0)], folder=f)
    wall("House_S", "x", -435.0, -600.0, 600.0, H, 350.0, gaps=[(-100.0, 100.0, 0.0, 250.0)], folder=f)
    wall("House_E", "y", 585.0, -420.0, 420.0, H, 350.0, gaps=[(-150.0, 150.0, 100.0, 220.0)], folder=f)
    wall("House_W", "y", -585.0, -420.0, 420.0, H, 350.0, gaps=[(-150.0, 150.0, 100.0, 220.0)], folder=f)
    box("House_Roof", 0.0, 0.0, H + 350.0, 960.0, 1260.0, 30.0, folder=f)
    # Парапет на крыше: крыша это огневая точка и площадка под турель.
    wall("House_Parapet_N", "x", 465.0, -630.0, 630.0, H + 380.0, 90.0, t=20.0, folder=f)
    wall("House_Parapet_S", "x", -465.0, -630.0, 630.0, H + 380.0, 90.0, t=20.0, folder=f)
    wall("House_Parapet_E", "y", 620.0, -455.0, 455.0, H + 380.0, 90.0, t=20.0, folder=f)
    wall("House_Parapet_W", "y", -620.0, -455.0, 455.0, H + 380.0, 90.0, t=20.0,
         gaps=[(-280.0, -120.0, 0.0, 90.0)], folder=f)
    # Лестница-пандус на крышу с запада, 30 градусов.
    run, rise = 670.0, 380.0
    ang = math.degrees(math.atan2(rise, run))
    shape(CUBE, "House_RoofRamp", (-200.0, -630.0 - run * 0.5, H + rise * 0.5),
          (math.hypot(run, rise), 150.0, 20.0), yaw=90.0, pitch=ang, folder=f)
    # Крыльцо с навесом: навес это место, где FPV теряет цель.
    box("Porch_Roof", -600.0, 0.0, H + 280.0, 300.0, 1260.0, 20.0, mat="wood", folder=f)
    for i, py in enumerate((-610.0, -200.0, 200.0, 610.0)):
        box("Porch_Post_{}".format(i), -735.0, py, H, 20.0, 20.0, 280.0, mat="wood", folder=f)
    box("House_Console", 300.0, 400.0, H, 80.0, 120.0, 110.0, mat="console", folder=f)


def build_barn(H):
    f = "HomeBase/Barn"
    # Амбар 14 x 10 м, 7 м в высоту: будущий гараж меха. Ворота 6 x 5 м во двор.
    wall("Barn_W", "y", 2015.0, -1000.0, 400.0, H, 700.0, gaps=[(-600.0, 0.0, 0.0, 500.0)], mat="wood", folder=f)
    wall("Barn_E", "y", 2985.0, -1000.0, 400.0, H, 700.0, mat="wood", folder=f)
    wall("Barn_N", "x", 385.0, 2030.0, 2970.0, H, 700.0, gaps=[(2400.0, 2600.0, 0.0, 250.0)], mat="wood", folder=f)
    wall("Barn_S", "x", -985.0, 2030.0, 2970.0, H, 700.0, mat="wood", folder=f)
    box("Barn_Roof", -300.0, 2500.0, H + 700.0, 1460.0, 1060.0, 30.0, mat="wood", folder=f)
    # Навес между домом и амбаром.
    box("Carport_Roof", -900.0, 1250.0, H + 300.0, 600.0, 500.0, 20.0, mat="wood", folder=f)
    for i, (dx, dy) in enumerate(((-280.0, -230.0), (-280.0, 230.0), (280.0, -230.0), (280.0, 230.0))):
        box("Carport_Post_{}".format(i), -900.0 + dx, 1250.0 + dy, H, 20.0, 20.0, 300.0, mat="wood", folder=f)


def build_tower(H):
    f = "HomeBase/WaterTower"
    cx, cy = 2000.0, 1900.0
    for i, (dx, dy) in enumerate(((-200.0, -200.0), (-200.0, 200.0), (200.0, -200.0), (200.0, 200.0))):
        box("Tower_Leg_{}".format(i), cx + dx, cy + dy, H, 40.0, 40.0, 1200.0, folder=f)
    box("Tower_Tank", cx, cy, H + 1200.0, 500.0, 500.0, 400.0, folder=f)


def build_fence(lay, H):
    f = "HomeBase/Fence"
    R = lay["fence_r"]
    gate = lay["gate_at"]
    rav = lay["ravine_at"]

    def gap(a):
        d = lambda b: abs((a - b + 180.0) % 360.0 - 180.0)
        return d(0.0) < 9.0 or d(gate) < 8.0 or d(rav) < 9.0

    step = 6.0
    chord = 2.0 * R * math.sin(math.radians(step * 0.5)) + 6.0
    n = 0
    a = step * 0.5
    while a < 360.0:
        if not gap(a):
            x, y = polar(a, R)
            box("Fence_{:03d}".format(int(a)), x, y, H, chord, 15.0, 150.0, yaw=a + 90.0, mat="fence", folder=f)
            n += 1
        a += step
    for side in (-1.0, 1.0):
        x, y = polar(gate + side * 9.0, R)
        box("Gate_Post_{}".format("L" if side < 0 else "R"), x, y, H, 30.0, 30.0, 250.0, mat="wood", folder=f)
    log("забор: {} секций".format(n))


def build_scarp(lay):
    for i, b in enumerate(lay["scarp_boxes"]):
        shape(CUBE, "Scarp_{:02d}".format(i), (b["x"], b["y"], (b["z0"] + b["z1"]) * 0.5),
              (b["len"], b["wid"], b["z1"] - b["z0"]), yaw=b["yaw"], mat="rock", folder="HomeBase/Scarp")


def build_road(lay):
    """Полотно дороги поверх ландшафта, только для читаемости: без коллизии, ходят по земле."""
    pts, zs = lay["road"], lay["road_z"]
    idx = list(range(1, len(pts) - 1, 3)) + [len(pts) - 2]
    for k, (i0, i1) in enumerate(zip(idx[:-1], idx[1:])):
        (x0, y0), (x1, y1) = pts[i0], pts[i1]
        z0, z1 = zs[i0], zs[i1]
        ln = math.hypot(x1 - x0, y1 - y0)
        yaw = math.degrees(math.atan2(y1 - y0, x1 - x0))
        pitch = math.degrees(math.atan2(z1 - z0, ln))
        shape(CUBE, "Road_{:03d}".format(k), ((x0 + x1) * 0.5, (y0 + y1) * 0.5, (z0 + z1) * 0.5 + 6.0),
              (math.hypot(ln, z1 - z0) + 40.0, 700.0, 10.0), yaw=yaw, pitch=pitch, mat="road",
              folder="HomeBase/Road", collision=False)


def build_greenery(lay, H):
    rng = random.Random(3)
    placed = []
    tries = 0
    while len(placed) < 12 and tries < 500:
        tries += 1
        a = rng.uniform(235.0, 320.0)
        r = rng.uniform(2300.0, 3600.0)
        x, y = polar(a, r)
        if any(math.hypot(x - px, y - py) < 600.0 for px, py in placed):
            continue
        placed.append((x, y))
        tree("Orchard_{:02d}".format(len(placed)), x, y, H, folder="HomeBase/Orchard")
    for i, (x, y, z) in enumerate(lay["trees_slope"]):
        tree("SlopeTree_{:02d}".format(i), x, y, z - 30.0, big=True, folder="HomeBase/Trees")


def build_cover(H):
    f = "HomeBase/Cover"
    box("Truck", -1300.0, -900.0, H, 520.0, 220.0, 180.0, yaw=20.0, mat="cover", folder=f)
    box("Tractor", 1500.0, -2200.0, H, 380.0, 190.0, 220.0, yaw=-30.0, mat="console", folder=f)
    box("WoodPile", -700.0, -1800.0, H, 300.0, 100.0, 120.0, yaw=10.0, mat="wood", folder=f)
    for i, (x, y) in enumerate(((2500.0, -800.0), (2900.0, -300.0), (2650.0, 650.0), (3300.0, 150.0), (1900.0, -1500.0))):
        shape(CYL, "HayBale_{}".format(i), (x, y, H + 75.0), (150.0, 150.0, 120.0), roll=90.0, yaw=i * 37.0,
              mat="cover", folder=f)
    # Бетонные блоки на северной бровке: из-за них держат поле.
    for i, (x, y) in enumerate(((4650.0, -1300.0), (4800.0, 800.0), (4450.0, 2300.0))):
        box("RimBlock_{}".format(i), x, y, H, 300.0, 80.0, 110.0, yaw=math.degrees(math.atan2(y, x)) + 90.0, folder=f)


def build_pads(lay, H):
    f = "HomeBase/TurretPads"
    pads = [
        ("Roof", 200.0, 350.0, H + 380.0),
        ("WaterTower", 2000.0, 1900.0, H + 1600.0),
        ("BarnRoof", -300.0, 2500.0, H + 730.0),
    ]
    for name, a in (("SouthRim", 190.0), ("NorthRim", 0.0), ("WestRim", 272.0)):
        x, y = polar(a, rim_at(lay, a) - 300.0)
        pads.append((name, x, y, H))
    x, y = polar(lay["gate_at"], 3600.0)
    pads.append(("Gate", x, y, H))
    for i, (name, x, y, z) in enumerate(pads, 1):
        a = shape(CYL, "TurretPad_{}_{}".format(i, name), (x, y, z + 10.0), (160.0, 160.0, 20.0), mat="pad", folder=f)
        a.set_editor_property("tags", [unreal.Name(TAG_GEO), unreal.Name("TurretPad")])
        text("TurretPad_{}_Label".format(i), x, y, z + 160.0, "ТУРЕЛЬ {}".format(i), size=50.0)
    log("площадок под турели: {}".format(len(pads)))


def step_geo():
    guard()
    lay = layout()
    H = lay["H"]
    _clear(TAG_GEO)
    _count["n"] = 0
    del _checks[:]
    build_house(H)
    build_barn(H)
    build_tower(H)
    build_fence(lay, H)
    build_scarp(lay)
    build_road(lay)
    build_greenery(lay, H)
    build_cover(H)
    build_pads(lay, H)
    text("Console_Label", 300.0, 400.0, H + 230.0, "КОНСОЛЬ: НАЧАТЬ ОСАДУ", size=40.0)
    log("ADDED: {} мешей".format(_count["n"]))
    if not check_kit_centers():
        raise RuntimeError("угловой пивот кита посчитан неверно, уровень НЕ сохранён")
    _les().save_current_level()


# ==================== игровая часть ====================

WAVES = [[1], [2]]            # авторские волны: маток на волну
WAVE_INTERVAL = 90.0          # волны по часам, живая предыдущая их не задерживает
FIRST_WAVE_DELAY = 0.0           # зашёл в триггер, первая волна сразу (автор 2026-09-14)
ENDLESS_GROWTH = 1.12         # бюджет бесконечной волны к предыдущей
AIR_SPAWN_HEIGHT = 3500.0     # над землёй у кромки тумана: примерно 10 м выше макушки
CORE_AT = (900.0, 1300.0)     # ядро базы во дворе, под открытым небом: под крышей дрон бьёт крышу
CORE_SIZE = 300.0
CORE_DEFEND_RADIUS = 4000.0   # нет игрока ближе 40 м: матки бьют ядро


def step_gameplay():
    guard()
    lay = layout()
    H = lay["H"]
    eas = _eas()
    _clear(TAG_GAME)
    f = "HomeBase/Gameplay"

    ps = eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(1300.0, -600.0, H + 100.0),
                                    unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    _tag(ps, TAG_GAME, "HB_PlayerStart", f)

    # Осада стартует, когда игрок подходит к консоли в доме, а не на спавне: даёт осмотреться.
    trig = eas.spawn_actor_from_class(unreal.TriggerBox, unreal.Vector(300.0, 400.0, H + 120.0))
    trig.collision_component.set_box_extent(unreal.Vector(150.0, 150.0, 120.0))
    _tag(trig, TAG_GAME, "HB_SiegeStartTrigger", f)

    points = []
    for name, (x, y), gz in (("North", (16000.0, 0.0), lay["spawn_ground_z"]["north"]),
                             ("West", (0.0, -16000.0), lay["spawn_ground_z"]["west"])):
        yaw = math.degrees(math.atan2(-y, -x))
        sp = eas.spawn_actor_from_class(unreal.ArenaSpawnPoint, unreal.Vector(x, y, gz),
                                        unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
        sp.set_editor_property("air_spawn", True)
        sp.set_editor_property("air_spawn_height", AIR_SPAWN_HEIGHT)
        _tag(sp, TAG_GAME, "HB_CarrierSpawn_" + name, f)
        points.append(sp)

    # Ядро базы: постройка с большим HP. Пока рядом нет игрока, матки роняют дроны в него, а не
    # в пешек (правило автора 2026-09-14). Ставится готовым, без чертежа: ключ его не чинит.
    core = eas.spawn_actor_from_class(unreal.SiegeCoreBuildable,
                                      kit_pivot((CORE_AT[0], CORE_AT[1], H + CORE_SIZE * 0.5),
                                                (CORE_SIZE, CORE_SIZE, CORE_SIZE), unreal.Rotator()),
                                      unreal.Rotator())
    core.mesh.set_static_mesh(_mesh(KIT_BOX))
    core.mesh.set_material(0, material("console"))
    core.set_actor_scale3d(unreal.Vector(CORE_SIZE / 100.0, CORE_SIZE / 100.0, CORE_SIZE / 100.0))
    core.set_editor_property("defend_radius", CORE_DEFEND_RADIUS)
    _tag(core, TAG_GAME, "HB_SiegeCore", f)
    text("SiegeCore_Label", CORE_AT[0], CORE_AT[1], H + CORE_SIZE + 120.0, "ЯДРО БАЗЫ", size=50.0,
         folder=f, tag=TAG_GAME)

    carrier = unreal.EditorAssetLibrary.load_blueprint_class(CARRIER_BP)
    waves = []
    for counts in WAVES:
        w = unreal.ArenaWave()
        e = unreal.ArenaSpawnEntry()
        e.set_editor_property("npc_class", carrier)
        e.set_editor_property("count", counts[0])
        w.set_editor_property("entries", [e])
        waves.append(w)
    kind = unreal.SiegeEnemyType()
    kind.set_editor_property("npc_class", carrier)
    kind.set_editor_property("cost", 1.0)
    kind.set_editor_property("first_wave", 1)
    kind.set_editor_property("weight", 1.0)
    sd = eas.spawn_actor_from_class(unreal.SiegeDirector, unreal.Vector(0.0, 0.0, H + 400.0))
    sd.set_editor_property("authored_waves", waves)
    sd.set_editor_property("endless_pool", [kind])
    sd.set_editor_property("endless_budget_growth", ENDLESS_GROWTH)
    sd.set_editor_property("wave_interval", WAVE_INTERVAL)
    sd.set_editor_property("first_wave_delay", FIRST_WAVE_DELAY)
    sd.set_editor_property("start_triggers", [trig])
    sd.set_editor_property("spawn_points", points)
    sd.set_editor_property("cores", [core])
    _tag(sd, TAG_GAME, "HB_SiegeDirector", f)

    nav = eas.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector(0.0, 0.0, 1500.0))
    nav.set_actor_scale3d(unreal.Vector(36000.0 / 200.0, 36000.0 / 200.0, 4500.0 / 200.0))
    _tag(nav, TAG_GAME, "HB_NavBounds", f)
    log("ADDED: старт игрока, триггер осады, ядро, 2 точки маток, директор осады ({} авторских волн, "
        "дальше бесконечно x{}), границы навмеша".format(len(waves), ENDLESS_GROWTH))
    _les().save_current_level()


def step_navdata():
    """Навданные движок заводит сам и со STATIC + тайлом 1000. Правим найденные, не спавним свои."""
    guard()
    recast = [a for a in _eas().get_all_level_actors() if isinstance(a, unreal.RecastNavMesh)]
    log("RecastNavMesh на уровне: {}".format(len(recast)))
    for r in recast:
        r.set_editor_property("tile_size_uu", 2000.0)
        r.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)
        log("MODIFIED: {} tile 2000, DYNAMIC".format(r.get_actor_label()))
    _les().save_current_level()
