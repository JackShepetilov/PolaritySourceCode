"""Сборка карты с линиями в редакторе: уровень, туман, свет-сублевел, ландшафт, разметка, линии, осада.

Рельеф и разметку считает terrain_lanes.py вне редактора (heightmap.png + layout.json).
Концепт: Docs/MOBA_Lanes_Concept_2026-09-29.md.

Запуск через execute_python_code или Tools/mcp.sh (длинный код тул обрезает, а подстроку с
расширением файла принимает за путь, поэтому имя собирается по частям):
    import unreal
    p = r"C:/.../Source/Tools/Lanes/build_lanes" + "." + "py"
    g = {"__name__": "lanes"}; exec(compile(open(p, encoding="utf-8").read(), p, "exec"), g)
    g["step_level"]()   затем step_light, step_fog, step_landscape, step_markup, step_base, step_gameplay,
                        step_camps, step_navdata

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
ROAD_MAT = {"Mid": "asphalt", "Top": "dirt", "Bot": "trail"}   # улица, грунтовка, тропинка
TAG_DRESS = "LanesDress"             # посёлок, поля, лес по линиям и между ними (lanes_content.py)
TAG_BASE = "LanesBase"               # ферма на плато: постройки и укрытия (base_layout.py)
OLD_LANDSCAPES = ("LanesTerrain",)   # 630 м, до растяжки вдвое 2026-09-29
NAV_TILE_UU = 4000.0                 # карта 1260 м: при 2000 вышло бы 4000 тайлов, строятся минутами (П30)

KIT_BOX = "/Game/LevelPrototyping/PolygonPrototype/Meshes/Buildings/Simple/SM_Bld_Block_1x1_01"
FLAT_MAT = "/Game/LevelPrototyping/Materials/M_FlatCol"
CYL = "/Engine/BasicShapes/Cylinder"
SPHERE = "/Engine/BasicShapes/Sphere"

GRUNT_BP = "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_ShooterNPC"
CARRIER_BP = "/Game/Prototype/HomeBase/BP_KamikazeCarrierDrone"
TANK_BP = "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_TrackedTank"

COLORS = {
    "road": (0.42, 0.34, 0.22), "river": (0.12, 0.25, 0.55), "pad": (0.95, 0.85, 0.10),
    "camp": (0.05, 0.75, 0.75), "base": (0.15, 0.55, 0.20), "enemy": (0.65, 0.12, 0.10),
    # Ферма (base_layout.py): цвета читаются издалека и отличают вид укрытия.
    "wood": (0.45, 0.30, 0.18), "house": (0.85, 0.83, 0.78), "barn": (0.62, 0.13, 0.11),
    "roof": (0.30, 0.30, 0.33), "metal": (0.58, 0.62, 0.66), "hay": (0.85, 0.72, 0.30),
    "truck": (0.22, 0.34, 0.58), "tractor": (0.22, 0.55, 0.22), "tire": (0.08, 0.08, 0.08),
    "fuel": (0.80, 0.78, 0.22), "concrete": (0.68, 0.68, 0.66), "sacks": (0.74, 0.66, 0.50),
    "fence": (0.92, 0.92, 0.90),
    "rock": (0.33, 0.30, 0.27),
    # Посёлок, поля, лес (lanes_content.py).
    "siding_blue": (0.45, 0.55, 0.65), "siding_yellow": (0.80, 0.72, 0.45), "brick": (0.50, 0.25, 0.18),
    "brick_dark": (0.36, 0.20, 0.16), "awning": (0.20, 0.40, 0.45), "bus": (0.90, 0.70, 0.10),
    "car_red": (0.60, 0.12, 0.10), "car_white": (0.85, 0.85, 0.85), "car_green": (0.20, 0.35, 0.22),
    "trunk": (0.30, 0.20, 0.12), "leaf": (0.25, 0.45, 0.18), "leaf_dark": (0.15, 0.32, 0.13),
    "pine": (0.10, 0.28, 0.18), "soil": (0.30, 0.22, 0.14), "pool": (0.30, 0.60, 0.80),
    "asphalt": (0.16, 0.16, 0.17), "dirt": (0.45, 0.36, 0.24), "trail": (0.55, 0.50, 0.38),
    # Кемпы (step_camps): ландмарк читается цветом и силуэтом издалека.
    "container": (0.55, 0.25, 0.12), "tarp": (0.25, 0.35, 0.22), "lamp_red": (1.0, 0.05, 0.03),
    "beam": (1.0, 0.95, 0.70),
    # Флаг и плёнка корпорации (автор 2026-09-30: белый с чёрным).
    "corp_white": (0.92, 0.92, 0.95), "corp_black": (0.04, 0.04, 0.05),
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


def shape(mesh_path, label, center, size, yaw=0.0, pitch=0.0, mat="road", folder="Lanes", collision=False,
          roll=0.0, tag=None):
    rot = unreal.Rotator(roll=roll, pitch=pitch, yaw=yaw)
    loc = kit_pivot(center, size, rot) if mesh_path == KIT_BOX else unreal.Vector(*center)
    act = _eas().spawn_actor_from_class(unreal.StaticMeshActor, loc, rot)
    comp = act.static_mesh_component
    comp.set_static_mesh(_mesh(mesh_path))
    comp.set_material(0, material(mat))
    act.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    if not collision:
        comp.set_collision_profile_name("NoCollision")
    _tag(act, tag or TAG_GEO, label, folder)
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


def _load_hill():
    import importlib.util
    spec = importlib.util.spec_from_file_location("base_layout", os.path.join(HERE, "base_layout" + "." + "py"))
    bl = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bl)
    return bl


def _path_at(pts, dist):
    """Точка, направление и путь до конца вдоль ломаной [(x, y), ...]."""
    acc = 0.0
    for (x0, y0), (x1, y1) in zip(pts[:-1], pts[1:]):
        L = math.hypot(x1 - x0, y1 - y0)
        if dist <= acc + L or (x1, y1) == tuple(pts[-1]):
            t = min(max((dist - acc) / max(L, 1.0), 0.0), 1.0)
            return (x0 + (x1 - x0) * t, y0 + (y1 - y0) * t), ((x1 - x0) / L, (y1 - y0) / L)
        acc += L
    return tuple(pts[-1]), (1.0, 0.0)


def _path_len(pts):
    return sum(math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(pts[:-1], pts[1:]))


def _dist_to_path(x, y, pts):
    best = 1e18
    for (x0, y0), (x1, y1) in zip(pts[:-1], pts[1:]):
        vx, vy = x1 - x0, y1 - y0
        t = max(0.0, min(1.0, ((x - x0) * vx + (y - y0) * vy) / max(vx * vx + vy * vy, 1.0)))
        best = min(best, math.hypot(x - x0 - t * vx, y - y0 - t * vy))
    return best


def hill_dressing(lay):
    """Холм под фермой: камни по склону обрыва и у подножия (обрыв неровный, не стена), и у каждого
    подъёма своё обустройство по лору захода (автор 2026-09-30):
      Mid  подъездная дорожка с улицы: забор из жердей по бокам, ворота и почтовый ящик внизу;
      Top  полевой въезд с грунтовки: решётка от скота и открытые полевые ворота, отбойные камни;
      Bot  тропа: ступени из брёвен поперёк, столбики с верёвкой по краю обрыва."""
    import random
    bl = _load_hill()
    rng = random.Random(11)
    c = lay["corner"]
    ramps = lay["ramps"]
    n = 0
    for base, bx, by, turn in (("Base", -c, -c, 0.0), ("Enemy", c, c, 180.0)):
        paths = [[tuple(p) for p in r["points"]] for r in ramps if r["base"] == base]
        a = 0.0
        while a < 360.0:
            own = (a - turn) % 360.0
            rE = bl.hg_edge_r(own)
            fw = bl.hg_face_w(own)
            ex, ey = bx + rE * math.cos(math.radians(a)), by + rE * math.sin(math.radians(a))
            if all(_dist_to_path(ex, ey, pts) > 2500.0 for pts in paths):
                # Два-три камня по склону и валун у подножия: ломают ровную линию обрыва.
                for k in range(rng.choice((2, 3))):
                    r = rE + fw * rng.uniform(0.2, 1.1)
                    x = bx + r * math.cos(math.radians(a + rng.uniform(-1.5, 1.5)))
                    y = by + r * math.sin(math.radians(a + rng.uniform(-1.5, 1.5)))
                    sz = rng.uniform(250.0, 650.0)
                    zg = _ground_z(x, y, 0.0)
                    shape(KIT_BOX, "Rock_{}_{:04d}_{}".format(base, int(a * 10), k), (x, y, zg + sz * 0.15),
                          (sz * rng.uniform(0.8, 1.6), sz * rng.uniform(0.8, 1.4), sz), yaw=rng.uniform(0, 360),
                          pitch=rng.uniform(-15, 15), mat="rock", folder="Lanes/Hill/Rocks", collision=True)
                    n += 1
            a += rng.uniform(2.5, 4.5)

        for r in ramps:
            if r["base"] != base:
                continue
            pts = [tuple(p) for p in r["points"]]
            half = r["half"]
            total = _path_len(pts)
            kind = r["kind"]
            folder = "Lanes/Hill/{}_{}".format(base, kind)
            if kind == "Mid":
                # Забор из жердей по обеим сторонам дорожки, от кромки до подножия.
                d = 600.0
                while d < total - 200.0:
                    (x, y), (ux, uy) = _path_at(pts, d)
                    for side in (-1.0, 1.0):
                        px, py = x - uy * side * (half + 80.0), y + ux * side * (half + 80.0)
                        zg = _ground_z(px, py, 0.0)
                        shape(KIT_BOX, "Drive_Post_{}_{:04d}_{}".format(base, int(d), int(side)), (px, py, zg + 60.0),
                              (18.0, 18.0, 130.0), mat="wood", folder=folder, collision=True)
                        (x2, y2), _ = _path_at(pts, d + 300.0)
                        qx, qy = x2 - uy * side * (half + 80.0), y2 + ux * side * (half + 80.0)
                        zq = _ground_z(qx, qy, 0.0)
                        L = math.hypot(qx - px, qy - py)
                        shape(KIT_BOX, "Drive_Rail_{}_{:04d}_{}".format(base, int(d), int(side)),
                              ((px + qx) * 0.5, (py + qy) * 0.5, (zg + zq) * 0.5 + 95.0), (L + 10.0, 10.0, 12.0),
                              yaw=math.degrees(math.atan2(qy - py, qx - px)),
                              pitch=math.degrees(math.atan2(zq - zg, max(L, 1.0))), mat="wood", folder=folder)
                        n += 2
                    d += 300.0
                (x, y), (ux, uy) = _path_at(pts, total - 150.0)
                for side in (-1.0, 1.0):
                    px, py = x - uy * side * (half + 120.0), y + ux * side * (half + 120.0)
                    zg = _ground_z(px, py, 0.0)
                    shape(KIT_BOX, "Drive_GatePost_{}_{}".format(base, int(side)), (px, py, zg + 140.0),
                          (40.0, 40.0, 280.0), mat="wood", folder=folder, collision=True)
                mx, my = x - uy * (half + 300.0), y + ux * (half + 300.0)
                zg = _ground_z(mx, my, 0.0)
                shape(KIT_BOX, "Drive_Mailbox_{}".format(base), (mx, my, zg + 60.0), (15.0, 15.0, 120.0), mat="wood", folder=folder)
                shape(KIT_BOX, "Drive_MailboxBox_{}".format(base), (mx, my, zg + 135.0), (50.0, 25.0, 30.0), mat="metal", folder=folder)
                n += 4
            elif kind == "Top":
                # Решётка от скота у подножия и распахнутые полевые ворота, отбойные камни по краю.
                (x, y), (ux, uy) = _path_at(pts, total - 300.0)
                zg = _ground_z(x, y, 0.0)
                shape(KIT_BOX, "Field_CattleGuard_{}".format(base), (x, y, zg + 4.0), (250.0, half * 2.0, 8.0),
                      yaw=math.degrees(math.atan2(uy, ux)), mat="tire", folder=folder)
                for side in (-1.0, 1.0):
                    px, py = x - uy * side * (half + 60.0), y + ux * side * (half + 60.0)
                    zp = _ground_z(px, py, 0.0)
                    shape(KIT_BOX, "Field_GatePost_{}_{}".format(base, int(side)), (px, py, zp + 75.0),
                          (25.0, 25.0, 150.0), mat="wood", folder=folder, collision=True)
                gx, gy = x - uy * (half + 60.0) + ux * 200.0, y + ux * (half + 60.0) + uy * 200.0
                zq = _ground_z(gx, gy, 0.0)
                shape(KIT_BOX, "Field_Gate_{}".format(base), (gx, gy, zq + 70.0), (400.0, 8.0, 110.0),
                      yaw=math.degrees(math.atan2(uy, ux)), mat="metal", folder=folder, collision=True)
                n += 4
                d = 900.0
                while d < total - 500.0:
                    (x, y), (ux, uy) = _path_at(pts, d)
                    for side in (-1.0, 1.0):
                        if rng.random() < 0.5:
                            px, py = x - uy * side * (half + 100.0), y + ux * side * (half + 100.0)
                            zp = _ground_z(px, py, 0.0)
                            sz = rng.uniform(90.0, 150.0)
                            shape(KIT_BOX, "Field_Stone_{}_{:04d}_{}".format(base, int(d), int(side)), (px, py, zp + sz * 0.3),
                                  (sz * 1.3, sz, sz), yaw=rng.uniform(0, 360), mat="rock", folder=folder, collision=True)
                            n += 1
                    d += 450.0
            else:
                # Ступени из брёвен поперёк тропы и столбики с верёвкой по краю.
                d = 600.0
                k = 0
                while d < total - 150.0:
                    (x, y), (ux, uy) = _path_at(pts, d)
                    zg = _ground_z(x, y, 0.0)
                    shape(KIT_BOX, "Trail_Step_{}_{:03d}".format(base, k), (x, y, zg + 6.0), (25.0, half * 1.8, 18.0),
                          yaw=math.degrees(math.atan2(uy, ux)), mat="wood", folder=folder)
                    if k % 2 == 0:
                        for side in (-1.0, 1.0):
                            px, py = x - uy * side * (half + 50.0), y + ux * side * (half + 50.0)
                            zp = _ground_z(px, py, 0.0)
                            shape(KIT_BOX, "Trail_Post_{}_{:03d}_{}".format(base, k, int(side)), (px, py, zp + 50.0),
                                  (14.0, 14.0, 100.0), mat="wood", folder=folder, collision=True)
                            n += 1
                    n += 1
                    k += 1
                    d += 150.0
    log("ADDED: холм, {} камней и обустройства подъёмов".format(n))


def step_lanes():
    """Посёлок по Mid, поля по Top, лес по Bot, задворки и природа между ними (lanes_content.py).
    Отдельные постройки и укрытия ставятся акторами, массовое (деревья, столбики, шпалы) одним
    актором на вид с экземплярами: тысячи акторов тормозят редактор."""
    import importlib.util
    import sys
    guard()
    lay = layout()
    _clear(TAG_DRESS)
    if HERE not in sys.path:
        sys.path.insert(0, HERE)
    spec = importlib.util.spec_from_file_location("lanes_content", os.path.join(HERE, "lanes_content" + "." + "py"))
    lc = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(lc)
    items = lc.build(lay)

    zcache = {}

    def ground(it):
        key = (round(it.get("ax", it["x"])), round(it.get("ay", it["y"])))
        if key not in zcache:
            zcache[key] = _ground_z(key[0], key[1], 0.0)
        return zcache[key]

    def xform(it):
        """Мировая трансформация и путь к мешу, как у shape()."""
        z0 = ground(it) + it["z0"]
        if it["kind"] == "box":
            rot = unreal.Rotator(roll=0.0, pitch=it.get("pitch", 0.0), yaw=it["yaw"])
            size = (it["sx"], it["sy"], it["sz"])
            loc = kit_pivot((it["x"], it["y"], z0 + it["sz"] * 0.5), size, rot)
            return KIT_BOX, loc, rot, size
        if it["kind"] == "cyl":
            if it["lying_yaw"] is None:
                return CYL, unreal.Vector(it["x"], it["y"], z0 + it["h"] * 0.5), unreal.Rotator(), (it["d"], it["d"], it["h"])
            return (CYL, unreal.Vector(it["x"], it["y"], z0 + it["d"] * 0.5),
                    unreal.Rotator(roll=90.0, pitch=0.0, yaw=it["lying_yaw"] - 90.0), (it["d"], it["d"], it["h"]))
        return SPHERE, unreal.Vector(it["x"], it["y"], z0 + it["h"] * 0.5), unreal.Rotator(), (it["d"], it["d"], it["h"])

    solo = 0
    groups = {}
    for it in items:
        if it.get("ism"):
            groups.setdefault((it["kind"], it["mat"], it.get("collision", True)), []).append(it)
            continue
        mesh, loc, rot, size = xform(it)
        act = _eas().spawn_actor_from_class(unreal.StaticMeshActor, loc, rot)
        comp = act.static_mesh_component
        comp.set_static_mesh(_mesh(mesh))
        comp.set_material(0, material(it["mat"]))
        act.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
        if not it.get("collision", it["kind"] != "sphere"):
            comp.set_collision_profile_name("NoCollision")
        _tag(act, TAG_DRESS, it["label"], "Lanes/Dress/" + it["label"].split("_")[0])
        solo += 1

    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    inst = 0
    for (kind, mat, collide), group in sorted(groups.items()):
        host = _eas().spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator())
        _tag(host, TAG_DRESS, "LN_Instances_{}_{}".format(kind, mat), "Lanes/Dress/Instances")
        # Экземпляры: компонент через SubobjectDataSubsystem (приём из ArenaBlockout/apply_biome1_art_pass).
        roots = sds.k2_gather_subobject_data_for_instance(host)
        handle, _ = sds.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=roots[0],
                                                                        new_class=unreal.InstancedStaticMeshComponent))
        comp = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(sds.k2_find_subobject_data_from_handle(handle))
        mesh = {"box": KIT_BOX, "cyl": CYL, "sphere": SPHERE}[kind]
        comp.set_static_mesh(_mesh(mesh))
        comp.set_material(0, material(mat))
        if not collide or kind == "sphere":
            comp.set_collision_profile_name("NoCollision")
        for it in group:
            m, loc, rot, size = xform(it)
            comp.add_instance(unreal.Transform(location=loc, rotation=rot,
                                               scale=unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0)), True)
            inst += 1
    log("ADDED: посёлок, поля и лес: {} акторов, {} экземпляров в {} группах".format(solo, inst, len(groups)))
    _les().save_current_level()


def step_base():
    """Ферма на нашем плато: дом, сарай под мех, вышка, силосы, стога, техника, баррикады.
    Раскладка и проверки проходов в base_layout.py (его же можно нарисовать вне редактора)."""
    import importlib.util
    guard()
    lay = layout()
    _clear(TAG_BASE)
    spec = importlib.util.spec_from_file_location("base_layout", os.path.join(HERE, "base_layout" + "." + "py"))
    bl = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bl)
    items = bl.build()
    bad = bl.check(items)
    if bad:
        raise RuntimeError("раскладка базы не прошла проверку: {}".format(bad[:5]))
    c = lay["corner"]
    top = _ground_z(-c, -c, lay["base_z"])
    n = 0
    for it in items:
        x, y = -c + it["x"], -c + it["y"]
        z0 = top + it["z0"]
        folder = "Lanes/Base/" + it["label"].split("_")[0]
        if it["kind"] == "box":
            shape(KIT_BOX, it["label"], (x, y, z0 + it["sz"] * 0.5), (it["sx"], it["sy"], it["sz"]),
                  yaw=it["yaw"], pitch=it.get("pitch", 0.0), mat=it["mat"], folder=folder, collision=True, tag=TAG_BASE)
        elif it["lying_yaw"] is None:
            shape(CYL, it["label"], (x, y, z0 + it["h"] * 0.5), (it["d"], it["d"], it["h"]),
                  mat=it["mat"], folder=folder, collision=True, tag=TAG_BASE)
        else:
            # Лёжа: roll 90 кладёт ось цилиндра вдоль локального Y, yaw поворачивает её на lying_yaw.
            shape(CYL, it["label"], (x, y, z0 + it["d"] * 0.5), (it["d"], it["d"], it["h"]),
                  yaw=it["lying_yaw"] - 90.0, roll=90.0, mat=it["mat"], folder=folder, collision=True, tag=TAG_BASE)
        n += 1
    log("ADDED: ферма на плато, {} примитивов ({} укрытий), верх плато {:.0f}".format(
        n, sum(1 for i in items if i["cover"]), top))
    _les().save_current_level()


LAND_LABEL = "LanesTerrain2x"
ROAD_STRIP = "/Game/Prototype/Lanes/Meshes/SM_LN_RoadStrip"
ROAD_PIECE = 10.0          # длина одного куска меша вдоль дороги, м (полоса 1 м, растягивается)
ROAD_MESH_LIFT = 5.0       # Mesh Vertical Offset точки сплайна: штатный зазор меша над подогнанной землёй
RAMP_MAT = {"Mid": "dirt", "Top": "dirt", "Bot": "trail"}   # подъёмы на холм: подъездная, полевой въезд, тропа


def road_strip_mesh():
    """Плоская полоса 1 x 1 м, разбитая вдоль на 10 рядов: сплайн-меш гнётся только по вершинам, а
    плоскость движка это один квадрат на весь кусок. Геометрия пересобирается каждый прогон."""
    if unreal.EditorAssetLibrary.does_asset_exist(ROAD_STRIP):
        mesh = unreal.load_asset(ROAD_STRIP)
    else:
        mesh = unreal.EditorAssetLibrary.duplicate_asset("/Engine/BasicShapes/Plane", ROAD_STRIP)
        log("CREATED: " + ROAD_STRIP)
    md = mesh.create_static_mesh_description()
    grp = md.create_polygon_group()
    md.set_polygon_group_material_slot_name(grp, mesh.static_materials[0].material_slot_name)
    nx, ny = 10, 2
    grid = []
    for i in range(nx + 1):
        row = []
        for j in range(ny + 1):
            v = md.create_vertex()
            md.set_vertex_position(v, unreal.Vector(-50.0 + 100.0 * i / nx, -50.0 + 100.0 * j / ny, 0.0))
            row.append((v, (i / float(nx), j / float(ny))))
        grid.append(row)

    def inst(vuv):
        vi = md.create_vertex_instance(vuv[0])
        md.set_vertex_instance_uv(vi, unreal.Vector2D(vuv[1][0], vuv[1][1]), 0)
        return vi
    for i in range(nx):
        for j in range(ny):
            a, b, c, d = grid[i][j], grid[i + 1][j], grid[i + 1][j + 1], grid[i][j + 1]
            md.create_triangle(grp, [inst(a), inst(d), inst(c)])
            md.create_triangle(grp, [inst(a), inst(c), inst(b)])
    mesh.build_from_static_mesh_descriptions([md], False, False)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh)
    return ROAD_STRIP


def _land_z(x, y, default):
    """Высота самого ландшафта, без луча: луч цеплял бы камни, деревья и постройки."""
    s = unreal.LandscapeService.get_height_at_location(LAND_LABEL, x, y)
    return s.height if s.valid else default


_gen = {}


def _gen_z(x, y):
    """Высота рельефа из heightmap.png генератора, билинейно. Не из ландшафта: сплайны его двигают, и
    точка, снятая с уже подогнанной земли, гоняла бы дорогу по кругу. PNG пишет MapEventBench/terrain.py:
    16 бит, строки без фильтра, raw = 32768 + z * 128 / z_scale."""
    import struct
    import zlib
    if not _gen:
        lay = layout()
        data = open(os.path.join(HERE, "heightmap.png"), "rb").read()
        w, h = struct.unpack(">II", data[16:24])
        pos, idat = 8, b""
        while pos < len(data):
            ln, tag = struct.unpack(">I4s", data[pos:pos + 8])
            if tag == b"IDAT":
                idat += data[pos + 8:pos + 8 + ln]
            pos += 12 + ln
        raw = zlib.decompress(idat)
        _gen.update(w=w, h=h, raw=raw, half=lay["half_extent"], step=lay["world"]["vertex_spacing_uu"],
                    zs=lay["world"]["z_scale"])
    g = _gen

    def px(ix, iy):
        ix = min(max(ix, 0), g["w"] - 1)
        iy = min(max(iy, 0), g["h"] - 1)
        o = iy * (g["w"] * 2 + 1) + 1 + ix * 2
        return (struct.unpack(">H", g["raw"][o:o + 2])[0] - 32768) * g["zs"] / 128.0
    fx, fy = (x + g["half"]) / g["step"], (y + g["half"]) / g["step"]
    ix, iy = int(math.floor(fx)), int(math.floor(fy))
    tx, ty = fx - ix, fy - iy
    z0 = px(ix, iy) * (1 - tx) + px(ix + 1, iy) * tx
    z1 = px(ix, iy + 1) * (1 - tx) + px(ix + 1, iy + 1) * tx
    return z0 * (1 - ty) + z1 * ty


def _road_paths(lay, name):
    """Путь дороги линии кусками (точки, полуширина, материал, шаг точек): наш подъём сверху вниз, полотно
    между подножиями, вражеский подъём снизу вверх. Плато (участок) без дороги."""
    pts = lay["lanes_full"][name]
    our = [r for r in lay["ramps"] if r["base"] == "Base" and r["lane"] == name][0]
    enemy = [r for r in lay["ramps"] if r["base"] == "Enemy" and r["lane"] == name][0]
    nb, ne = len(our["points"]), len(enemy["points"])
    half = lay.get("lane_width", {}).get(name, lay["lane_w"]) * 0.5
    return [
        (pts[1:nb + 1], our["half"], RAMP_MAT[name], 1500.0),
        (pts[nb:len(pts) - ne], half, ROAD_MAT.get(name, "road"), 3000.0),
        (pts[len(pts) - ne - 1:len(pts) - 1], enemy["half"], RAMP_MAT[enemy["kind"]], 1500.0),
    ]


def _clear_roads():
    """Сплайны ландшафта и их меши. Удаление точек в LandscapeService не сносит меши сегментов, поэтому
    сплайн-меши на ландшафте убираются отдельно, иначе остаются висеть."""
    unreal.LandscapeService.delete_all_splines(LAND_LABEL)
    n = 0
    for a in _eas().get_all_level_actors():
        if isinstance(a, (unreal.Landscape, unreal.LandscapeProxy)):
            for c in a.get_components_by_class(unreal.SplineMeshComponent):
                c.destroy_component(c)
                n += 1
    old = 0
    for a in _eas().get_all_level_actors():
        if a.actor_has_tag(TAG_GEO) and str(a.get_folder_path()) == "Lanes/Roads":
            a.destroy_actor()
            old += 1
    log("DELETED: сплайны дорог, {} сплайн-мешей, {} старых полос-коробок".format(n, old))


def _land():
    for a in _eas().get_all_level_actors():
        if isinstance(a, unreal.Landscape) and a.get_actor_label() == LAND_LABEL:
            return a
    return None


def step_roads(lanes=None):
    """Дороги сплайнами ландшафта, штатным инструментом движка для дорог. Землю под дорогой двигает
    отдельный слой редактирования типа Splines (LandscapeEditLayerSplines): основной рельеф не
    трогается, сплайн можно тянуть руками, и земля пересчитается сама. Без этого слоя шаг не идёт:
    кнопка Apply Splines писала бы в основной слой и портила рельеф при каждом прогоне."""
    guard()
    lay = layout()
    LS = unreal.LandscapeService
    land = _land()
    if not land or not any(isinstance(l, unreal.LandscapeEditLayerSplines) for l in land.get_edit_layers_bp()):
        raise RuntimeError("у {} нет слоя Splines: Landscape mode, Edit Layers, добавить слой типа Splines".format(LAND_LABEL))
    _clear_roads()
    for name in (lanes or sorted(lay["lanes_full"])):
        prev = None
        for k, (pts, half, mat, step) in enumerate(_road_paths(lay, name)):
            dense = densify(pts, step)
            if k > 0:
                dense = dense[1:]   # подножие подъёма уже стоит точкой: сплайн линии один, без стыка
            for x, y, z in dense:
                r = LS.create_spline_point(LAND_LABEL, unreal.Vector(x, y, _gen_z(x, y)),
                                           half, 300.0, 300.0, "", True, True)
                if not r.success:
                    log("FAIL: точка дороги {} {}".format(name, r.error_message))
                    continue
                if prev is not None:
                    LS.connect_spline_points(LAND_LABEL, prev, r.point_index, 0.0, 0.0, "", True, True)
                prev = r.point_index
            log("ADDED: дорога {} {} ({} точек, полуширина {:.0f})".format(name, mat, len(dense), half))
    info = LS.get_spline_info(LAND_LABEL)
    by_mat = {}
    for name in (lanes or sorted(lay["lanes_full"])):
        for pts, half, mat, step in _road_paths(lay, name):
            by_mat.setdefault(mat, []).append(pts)
    # Меш на сегмент: полоса по ширине сплайна, материал по ближайшему куску пути.
    strip_mesh = road_strip_mesh()
    for seg in info.segments:
        a = info.control_points[seg.start_point_index].location
        b = info.control_points[seg.end_point_index].location
        mx, my = (a.x + b.x) * 0.5, (a.y + b.y) * 0.5
        mat = min(((_dist_to_path(mx, my, [(p[0], p[1]) for p in pts]), m)
                   for m, group in by_mat.items() for pts in group))[1]
        entry = unreal.LandscapeSplineMeshEntryInfo()
        entry.mesh_path = strip_mesh
        entry.scale = unreal.Vector(ROAD_PIECE, 1.0, 1.0)
        entry.scale_to_width = True
        entry.material_override_paths = [material(mat).get_path_name()]
        entry.forward_axis = 0
        entry.up_axis = 2
        LS.set_spline_segment_meshes(LAND_LABEL, seg.segment_index, [entry])
    for pt in info.control_points:
        LS.set_spline_point_mesh(LAND_LABEL, pt.point_index, "", unreal.Vector(1.0, 1.0, 1.0), ROAD_MESH_LIFT)
    # Слой Splines пересчитывается по запросу обновления слоёв; красок на ландшафте нет, стирать нечего.
    land.force_layers_full_update()
    log("ADDED: {} точек, {} сегментов с мешами".format(info.num_control_points, info.num_segments))
    _les().save_current_level()


def _ism(label, folder, mesh, mat, transforms, collide=True):
    """Один актор с экземплярами вместо сотен акторов (как step_lanes). transforms: [(loc, rot, size)]."""
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    host = _eas().spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0.0, 0.0, 0.0), unreal.Rotator())
    _tag(host, TAG_GEO, label, folder)
    roots = sds.k2_gather_subobject_data_for_instance(host)
    handle, _ = sds.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=roots[0],
                                                                    new_class=unreal.InstancedStaticMeshComponent))
    comp = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(sds.k2_find_subobject_data_from_handle(handle))
    comp.set_static_mesh(_mesh(mesh))
    comp.set_material(0, material(mat))
    if not collide:
        comp.set_collision_profile_name("NoCollision")
    for loc, rot, size in transforms:
        comp.add_instance(unreal.Transform(location=loc, rotation=rot,
                                           scale=unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0)), True)
    return len(transforms)


def edge_dressing(lay):
    """Камни на скальном склоне гряды по краю карты и на склонах холмов тяжёлых кемпов: скала читается
    скалой, а не гладким склоном ландшафта. Подъём на холм свободен."""
    import random
    rng = random.Random(23)
    rocks = []

    def rock(x, y, sz, zlift=0.15):
        zg = _land_z(x, y, 0.0)
        rot = unreal.Rotator(roll=rng.uniform(-12, 12), pitch=rng.uniform(-15, 15), yaw=rng.uniform(0, 360))
        size = (sz * rng.uniform(0.9, 1.7), sz * rng.uniform(0.8, 1.4), sz * rng.uniform(0.7, 1.2))
        rocks.append((kit_pivot((x, y, zg + size[2] * (0.5 - zlift)), size, rot), rot, size))

    for x, y, ix, iy, fw in lay.get("ridge", {}).get("face", []):
        for k in range(rng.choice((2, 3))):
            t = rng.uniform(-0.5, 0.6) * fw
            j = rng.uniform(-500.0, 500.0)
            rock(x + ix * t + iy * j, y + iy * t + ix * j, rng.uniform(500.0, 1300.0))
    ridge_n = len(rocks)
    for s in lay["poi_slots"]:
        kn = s.get("knoll")
        if not kn:
            continue
        (x0, y0), (x1, y1) = kn["ramp"]
        ramp_ang = math.atan2(y1 - y0, x1 - x0)
        for i, rE in enumerate(kn["edge_r"]):
            b = -math.pi + 2.0 * math.pi * i / len(kn["edge_r"])
            gap = abs((b - ramp_ang + math.pi) % (2.0 * math.pi) - math.pi)
            if gap < math.radians(14.0):
                continue            # подъём
            r = rE + rng.uniform(100.0, 350.0)
            rock(s["x"] + r * math.cos(b), s["y"] + r * math.sin(b), rng.uniform(250.0, 500.0), zlift=0.3)
    n = _ism("LN_Rocks_Edge", "Lanes/Edge", KIT_BOX, "rock", rocks)
    log("ADDED: камни гряды {} и холмов кемпов {}".format(ridge_n, n - ridge_n))


def step_markup():
    """Полотна линий, река, площадки турелей и кемпов, подписи. Всё без коллизии."""
    guard()
    lay = layout()
    _clear(TAG_GEO)
    _count["n"] = 0
    # Полотна линий теперь сплайны ландшафта (step_roads), полос-коробок больше нет (автор 2026-09-30).
    # Реки полосой больше нет: русло с дном это сам ландшафт (автор 2026-09-29).
    hill_dressing(lay)
    edge_dressing(lay)
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
        text("Camp_{}_Label".format(s["name"]), s["x"], s["y"], z + 400.0,
             "{} {} {}".format(s["name"], s.get("tier", ""), s.get("terrain", "")))
    c = lay["corner"]
    for key, x, y, msg in (("base", -c, -c, "BASE"), ("enemy", c, c, "ENEMY BASE")):
        # Кольца по краю плато больше нет: край неровный, его читают забор фермы и камни обрыва.
        z = _ground_z(x, y, 0.0)
        text("Label_" + key, x, y, z + 800.0, msg, size=300.0)
    log("ADDED: {} мешей разметки".format(_count["n"]))
    _les().save_current_level()


# ==================== игровая часть ====================

WAVE_INTERVAL = 30.0          # только для режима «все линии разом» (bStaggerLanes выкл)
PACK_INTERVAL = 30.0          # по линии за раз, по кругу: соло пачка раз в 30 с, на каждой линии раз в 90 с;
                              # делится на число игроков (автор 2026-09-29), у троих выходит темп доты
LANE_ORDER = ("Mid", "Top", "Bot")   # открывающая на Mid идёт первой
PREWARM_FIRST_ARRIVAL = 10.0         # осада «шла до игрока»: первый крип у базы через 10 с после раздатчика
FIRST_WAVE_DELAY = 0.0        # поставил раздатчик, первая пачка сразу (автор 2026-09-23)
CARRIER_CAP = 6               # по две матки на линию; лишние усиливают живых


def _curve(keys, step=True):
    """FRuntimeFloatCurve из [(x, y), ...]; step=True даёт ступеньки («+1 на 15-й минуте»)."""
    rc = unreal.RuntimeFloatCurve()
    txt = ",".join("(InterpMode={},Time={},Value={})".format("RCIM_Constant" if step else "RCIM_Linear", x, y)
                   for x, y in keys)
    rc.import_text("(EditorCurveData=(Keys=({})))".format(txt))
    return rc


def _kind(cls, base=1, count_keys=None, per_player=0.0, first=1, every=1, last=0, lanes=(),
          plus_one_every=0.0, lane_cycle=0.0, lane_cycle_first=3.0):
    k = unreal.SiegeCreepKind()
    k.set_editor_property("npc_class", cls)
    k.set_editor_property("base_count", base)
    if count_keys:
        k.set_editor_property("count_by_minute", _curve(count_keys))
    k.set_editor_property("count_per_player", per_player)
    k.set_editor_property("add_one_every_minutes", plus_one_every)
    k.set_editor_property("lane_cycle_minutes", lane_cycle)
    k.set_editor_property("lane_cycle_first_minute", lane_cycle_first)
    k.set_editor_property("first_wave", first)
    k.set_editor_property("every_nth_wave", every)
    k.set_editor_property("last_wave", last)
    k.set_editor_property("only_lanes", [unreal.Name(n) for n in lanes])
    return k


GRUNT_PLUS_ONE_MIN = 10.0     # +1 грунт в пачке каждые 10 мин, линейно и бесконечно (автор 2026-09-30)
CARRIER_PLUS_ONE_MIN = 15.0   # +1 матка каждые 15 мин
TANK_LANE_CYCLE_MIN = 9.0     # танкетка на каждой линии раз в 9 мин, линии со сдвигом: игроку раз в 3 мин
# Часы пачек идут от выхода первой пачки, это ~1.4 мин до раздатчика (прогрев): первая танкетка у базы
# примерно через 3 мин после раздатчика.
TANK_FIRST_MIN = 4.5
CREEP_HP_INTERVAL = 300.0     # HP крипов +10% от базы каждые 5 мин, плавно: x1.9 к 45-й минуте
CREEP_HP_PER_INTERVAL = 1.10


def lane_pack():
    """Пачка по решению автора 2026-09-30 (числа в FSiegeCreepKind, правятся в Details директора).

    Волна 1: только открывающая, по грунту на игрока, на средней линии (ближний бой за стволы).
    Дальше каждая пачка: 3 грунта и 1 матка, число тех и других растёт линейно. Танкетка по своим
    часам, по линиям по очереди. Без рывков и без таймера конца."""
    grunt = unreal.EditorAssetLibrary.load_blueprint_class(GRUNT_BP)
    carrier = unreal.EditorAssetLibrary.load_blueprint_class(CARRIER_BP)
    tank = unreal.EditorAssetLibrary.load_blueprint_class(TANK_BP)
    return [
        _kind(grunt, base=0, per_player=1.0, first=1, last=1, lanes=("Mid",)),
        _kind(grunt, base=3, first=2, plus_one_every=GRUNT_PLUS_ONE_MIN),
        _kind(carrier, base=1, first=2, plus_one_every=CARRIER_PLUS_ONE_MIN),
        _kind(tank, base=1, lane_cycle=TANK_LANE_CYCLE_MIN, lane_cycle_first=TANK_FIRST_MIN),
    ]


def apply_waves():
    """Только пачка и рост HP на живом директоре L_Lanes: остальное автор мог подкрутить руками."""
    guard()
    sd = [a for a in _eas().get_all_level_actors() if isinstance(a, unreal.SiegeDirector)]
    if len(sd) != 1:
        raise RuntimeError("директоров осады на уровне: {}".format(len(sd)))
    sd = sd[0]
    log("OLD: пачка {} строк, HP интервал {} x{}".format(len(sd.get_editor_property("lane_pack")),
        sd.get_editor_property("creep_upgrade_interval"), sd.get_editor_property("creep_health_per_upgrade")))
    sd.set_editor_property("lane_pack", lane_pack())
    sd.set_editor_property("creep_upgrade_interval", CREEP_HP_INTERVAL)
    sd.set_editor_property("creep_health_per_upgrade", CREEP_HP_PER_INTERVAL)
    log("MODIFIED: пачка и рост HP на " + sd.get_actor_label())
    _les().save_current_level()


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
    # Начало (автор 2026-09-30): часы осады отмотаны назад, настоящие волны уже в пути по линиям.
    # До раздатчика их нет, появляются в момент старта там, куда успели бы дойти.
    sd.set_editor_property("prewarm", True)
    sd.set_editor_property("prewarm_first_arrival", PREWARM_FIRST_ARRIVAL)
    # 750, а не 420: крипы на линии бегут (замер 2026-09-30, 7.4-7.6 м/с на Top и Bot). Прежние 420
    # сняты с грунта, который стоял и стрелял, и расписание приходов сжималось почти вдвое.
    sd.set_editor_property("creep_lane_speed", 750.0)
    sd.set_editor_property("max_carriers_alive", CARRIER_CAP)
    sd.set_editor_property("creep_upgrade_interval", CREEP_HP_INTERVAL)
    sd.set_editor_property("creep_health_per_upgrade", CREEP_HP_PER_INTERVAL)
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


# ==================== лесные кемпы и сборка меха ====================
# Дизайн: Docs/Lane_Camps_Design_2026-09-30.md. Кемп это ASiegeCampSite в месте из layout.json
# (poi_slots) плюс ландмарк по тиру: костёр с дымом (easy), склад с мачтой и красной лампой (medium),
# вышка с прожектором (hard). Детали меха: 5 слотов x 3 редкости, data asset'ы в PART_DIR.
# Сарай фермы: ASiegeMechBay, триггер во всю коробку сарая.

TAG_CAMP = "LanesCamp"
CONE = "/Engine/BasicShapes/Cone"
CUBE = "/Engine/BasicShapes/Cube"
PART_DIR = "/Game/Prototype/Lanes/MechParts"
NS_FIRE = "/Game/NiagaraExamples/FX_Misc/NS_Fire"
NS_SMOKE = "/Game/NiagaraExamples/FX_Smoke/NS_Smoke_Plume"

# Слот: (меш на ящике, масштаб). Форма говорит слот издалека.
PART_LOOK = {
    "CHASSIS": (CUBE, (1.2, 1.2, 0.6)),
    "MAIN_WEAPON": (CYL, (0.25, 0.25, 1.4)),
    "TACTICAL": (SPHERE, (0.8, 0.8, 0.8)),
    "HEAVY": (CONE, (0.8, 0.8, 1.0)),
    "CORE": (CYL, (0.9, 0.9, 0.25)),
}
RARITIES = ("COMMON", "RARE", "EPIC")        # easy, medium, hard
TIER_ENUM = {"easy": "EASY", "medium": "MEDIUM", "hard": "HARD"}


def _guard(cls, base, every=0.0, first=0.0, cap=0, per_extra=0.0):
    k = unreal.SiegeCampGuardKind()
    k.set_editor_property("npc_class", cls)
    k.set_editor_property("base_count", base)
    k.set_editor_property("add_one_every_minutes", every)
    k.set_editor_property("first_minute", first)
    k.set_editor_property("max_count", cap)
    k.set_editor_property("count_per_extra_player", per_extra)
    return k


def camp_guards(tier):
    """Охрана по тиру (дизайн, §5). Числа правятся в Details кемпа."""
    grunt = unreal.EditorAssetLibrary.load_blueprint_class(GRUNT_BP)
    carrier = unreal.EditorAssetLibrary.load_blueprint_class(CARRIER_BP)
    tank = unreal.EditorAssetLibrary.load_blueprint_class(TANK_BP)
    if tier == "easy":
        return [_guard(grunt, 2, every=10.0, cap=5, per_extra=1.0)]
    if tier == "medium":
        return [_guard(grunt, 3, every=10.0, per_extra=1.0), _guard(carrier, 1, every=20.0, cap=2)]
    # Грунты первыми: первые точки охраны у кусков бывают наверху, туда встают они, не танкетки.
    return [_guard(grunt, 3, every=8.0, per_extra=1.0), _guard(tank, 1), _guard(tank, 1, first=25.0)]


def mech_parts():
    """15 деталей: 5 слотов x 3 редкости. Есть ассет: обновить на месте, не пересоздавать."""
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    out = []
    for slot, (mesh, scale) in PART_LOOK.items():
        for rarity in RARITIES:
            name = "DA_MechPart_{}_{}".format(slot.title().replace("_", ""), rarity.title())
            path = PART_DIR + "/" + name
            if unreal.EditorAssetLibrary.does_asset_exist(path):
                da = unreal.load_asset(path)
            else:
                fac = unreal.DataAssetFactory()
                fac.set_editor_property("data_asset_class", unreal.MechPartDefinition)
                da = tools.create_asset(name, PART_DIR, unreal.MechPartDefinition, fac)
                log("CREATED: " + path)
            da.set_editor_property("slot", getattr(unreal.MechPartSlot, slot))
            da.set_editor_property("rarity", getattr(unreal.UpgradeRarity, rarity))
            da.set_editor_property("display_name", unreal.Text("{} ({})".format(slot.title().replace("_", " "), rarity.title())))
            da.set_editor_property("display_mesh", _mesh(mesh))
            da.set_editor_property("display_scale", unreal.Vector(*scale))
            unreal.EditorAssetLibrary.save_loaded_asset(da)
            out.append(da)
    log("MODIFIED: {} деталей меха в {}".format(len(out), PART_DIR))
    return out


def _fx(label, ns_path, loc, scale, folder):
    act = _eas().spawn_actor_from_class(unreal.NiagaraActor, unreal.Vector(*loc))
    act.get_editor_property("niagara_component").set_asset(unreal.load_asset(ns_path))
    act.set_actor_scale3d(unreal.Vector(scale, scale, scale))
    act.set_editor_property("tags", [unreal.Name(TAG_CAMP), unreal.Name("CampLit")])
    act.set_actor_label(label)
    act.set_folder_path(folder)
    return act


# ---- Кемпы по местам (дизайн §9, референсы Docs/Lane_Camps_References_2026-09-30.md) ----
# Каждый кусок: (акторы ландмарка, где лежит деталь (u, v, z), точки охраны [(u, v)]). u вперёд, к нашей
# базе, v вправо. Размеры из референсов, укрытия по правилам карты: низкое 115, высокое 230.

def _piece_apiary(s, box, at, flag, fire, mast, lit, z, a, folder):
    """JungleBot_1, easy: пасека. Медовый сарай открыт к нашей базе, деталь в нём; ульи рядами по бокам,
    костёр у сарая."""
    box("ShedBack", -250.0, 0.0, 0.0, (20.0, 520.0, 280.0))
    box("ShedL", 0.0, -250.0, 0.0, (500.0, 20.0, 280.0))
    box("ShedR", 0.0, 250.0, 0.0, (500.0, 20.0, 280.0))
    box("ShedRoof", 0.0, 0.0, 280.0, (580.0, 580.0, 20.0), mat="roof")
    box("Crate", -60.0, 0.0, 0.0, (140.0, 140.0, 100.0))
    box("Extractor", -150.0, 160.0, 0.0, (70.0, 70.0, 110.0), mat="metal", mesh=CYL)
    box("HoneyDrums", -150.0, -160.0, 0.0, (60.0, 60.0, 90.0), mat="fuel", mesh=CYL)
    # Ульи: улей 50 x 41, три корпуса = 73 см, на подставке 20: сидя за ним видно, это не укрытие.
    for side, sname in ((-1.0, "L"), (1.0, "R")):
        for row, off in enumerate((1250.0, 1550.0)):
            for k in range(8):
                u = -875.0 + k * 250.0
                box("Hive{}{}_{}Stand".format(sname, row, k), u, side * off, 0.0, (60.0, 50.0, 20.0))
                box("Hive{}{}_{}".format(sname, row, k), u, side * off, 20.0, (50.0, 41.0, 73.0), mat="house")
    fire(600.0, -450.0)
    box("Bench", 600.0, -150.0, 0.0, (200.0, 40.0, 45.0))
    flag(-330.0, 330.0, 0.0)
    posts = [(600.0 + 300.0 * math.cos(math.radians(t)), -450.0 + 300.0 * math.sin(math.radians(t)))
             for t in (0.0, 72.0, 144.0, 216.0, 288.0)] + [(400.0, 150.0), (900.0, 0.0)]
    return lit, (-60.0, 0.0, 170.0), posts


def _piece_silage(s, box, at, flag, fire, mast, lit, z, a, folder):
    """JungleTop_3, medium, яма: силосная траншея. Бетонные стенки 3 м, между ними 9 м, куча под чёрной
    плёнкой с шинами. Въезд к нашей базе, деталь у въезда под краем плёнки, охрана там же."""
    L, W, H = 2400.0, 900.0, 300.0
    for side, sname in ((-1.0, "L"), (1.0, "R")):
        box("Wall" + sname, 0.0, side * (W * 0.5 + 20.0), 0.0, (L, 40.0, H), mat="concrete")
    box("Silage", -200.0, 0.0, 0.0, (L - 400.0, W - 20.0, 200.0), mat="soil")
    box("Tarp", -200.0, 0.0, 200.0, (L - 380.0, W - 10.0, 8.0), mat="corp_black", collision=False)
    for i in range(5):
        for j in range(3):
            box("Tire{}{}".format(i, j), -1100.0 + i * 450.0, -300.0 + j * 300.0, 208.0, (80.0, 80.0, 22.0),
                mat="tire", collision=False, mesh=CYL)
    # Край плёнки свисает у въезда: низкий навес над ящиком с деталью.
    box("TarpFlap", 1050.0, 0.0, 180.0, (300.0, W - 10.0, 8.0), mat="corp_black", collision=False)
    box("Crate", 1300.0, 0.0, 0.0, (140.0, 140.0, 100.0))
    box("TireStack", 1500.0, -600.0, 0.0, (90.0, 90.0, 110.0), mat="tire", mesh=CYL)
    box("TireStack2", 1500.0, 650.0, 0.0, (90.0, 90.0, 110.0), mat="tire", mesh=CYL)
    mast(-1500.0, 800.0)
    flag(-1180.0, -470.0, H)
    posts = [(1600.0, -350.0), (1600.0, 350.0), (1900.0, 0.0), (1350.0, -700.0), (1350.0, 700.0),
             (2100.0, -500.0), (2100.0, 500.0), (-1500.0, 0.0)]
    return lit, (1300.0, 0.0, 170.0), posts


def _piece_lookout(s, box, at, flag, fire, mast, lit, z, a, folder):
    """JungleBot_4, hard, холм: две вышки и мост (автор 2026-09-30). Низкая бетонная 8 x 8 м, верх на 6 м,
    к ней пандус; с неё наклонный мост на галерею высокой деревянной вышки, кабина на 12 м. Всё ходибельно
    по навмешу (уклон 29 градусов при пределе агента 44): наверх ходят и игрок, и охрана. Деталь в кабине
    высокой, двое охранников на низкой, остальные внизу. Низкая к нашей базе пандусом (u+), обе по оси v."""
    def slab(label, u0, v0, z0, u1, v1, z1, width, thick=30.0, mat="concrete"):
        """Наклонная плита от точки к точке (z абсолютные, по верхней грани)."""
        x0, y0 = at(u0, v0)
        x1, y1 = at(u1, v1)
        run = math.hypot(x1 - x0, y1 - y0)
        shape(KIT_BOX, "Camp_{}_{}".format(s["name"], label),
              ((x0 + x1) * 0.5, (y0 + y1) * 0.5, (z0 + z1) * 0.5 - thick * 0.5),
              (math.hypot(run, z1 - z0) + 20.0, width, thick),
              yaw=math.degrees(math.atan2(y1 - y0, x1 - x0)), pitch=math.degrees(math.atan2(z1 - z0, run)),
              mat=mat, folder=folder, collision=True, tag=TAG_CAMP)

    # ---- Низкая: бетонный блок, верх 600, парапет по пояс с проходами к пандусу и к мосту ----
    lv, lh, half = -600.0, 600.0, 400.0
    box("LowBlock", 0.0, lv, 0.0, (2.0 * half, 2.0 * half, lh), mat="concrete")
    gap = 150.0
    for k, (u0, v0, su, sv) in enumerate((
            (half - 15.0, lv - (half + gap) * 0.5, 30.0, half - gap),     # перед, слева от пандуса
            (half - 15.0, lv + (half + gap) * 0.5, 30.0, half - gap),     # перед, справа
            (-half + 15.0, lv, 30.0, 2.0 * half),                         # зад
            (0.0, lv - half + 15.0, 2.0 * half, 30.0),                    # левый бок
            ((half + 125.0) * -0.5, lv + half - 15.0, half - 125.0, 30.0),    # правый бок до проёма моста
            ((half + 125.0) * 0.5, lv + half - 15.0, half - 125.0, 30.0))):   # и после
        box("LowParapet{}".format(k), u0, v0, lh, (su, sv, 100.0), mat="concrete")
    # Пандус к нашей базе: 6 м на 11 м.
    ru = half + 1100.0
    px, py = at(ru, lv)
    foot = _ground_z(px, py, z)
    slab("LowRamp", ru, lv, foot, half, lv, z + lh, 300.0)
    for side in (-1.0, 1.0):
        slab("LowRampRail{:+.0f}".format(side), ru, lv + side * 160.0, foot + 100.0, half, lv + side * 160.0, z + lh + 100.0,
             10.0, thick=100.0, mat="metal")

    # ---- Высокая: деревянная, галерея 700 x 700 на 1200, кабина 430 с проёмом к мосту ----
    hv, hh, leg, cab = 1250.0, 1200.0, 300.0, 430.0
    for k, (du, dv) in enumerate(((-leg, -leg), (leg, -leg), (-leg, leg), (leg, leg))):
        box("HighLeg{}".format(k), du, hv + dv, 0.0, (50.0, 50.0, hh), mat="wood", mesh=CYL)
    for lvl in (400.0, 800.0):
        for side in (-1.0, 1.0):
            box("HighBeamU{:.0f}{:+.0f}".format(lvl, side), side * leg, hv, lvl, (20.0, 2.0 * leg + 50.0, 20.0))
            box("HighBeamV{:.0f}{:+.0f}".format(lvl, side), 0.0, hv + side * leg, lvl, (2.0 * leg + 50.0, 20.0, 20.0))
    box("HighDeck", 0.0, hv, hh, (700.0, 700.0, 30.0))
    top = hh + 30.0
    door = 220.0
    seg = (cab - door) * 0.5
    box("CabWallF", cab * 0.5, hv, top, (20.0, cab, 100.0))
    box("CabWallB", -cab * 0.5, hv, top, (20.0, cab, 100.0))
    box("CabWallR", 0.0, hv + cab * 0.5, top, (cab, 20.0, 100.0))
    for side in (-1.0, 1.0):                                    # к мосту: проём 2.2 м в полный рост
        box("CabWallL{:+.0f}".format(side), side * (door + seg) * 0.5, hv - cab * 0.5, top, (seg, 20.0, 100.0))
    for k, (du, dv) in enumerate(((-1.0, -1.0), (1.0, -1.0), (-1.0, 1.0), (1.0, 1.0))):
        box("CabPost{}".format(k), du * (cab * 0.5 - 10.0), hv + dv * (cab * 0.5 - 10.0), top, (20.0, 20.0, 260.0))
    box("CabRoof", 0.0, hv, top + 260.0, (560.0, 560.0, 20.0), mat="roof")
    box("CabCap", 0.0, hv, top + 280.0, (560.0, 560.0, 180.0), mat="roof", mesh=CONE)
    box("Crate", 60.0, hv + 60.0, top, (100.0, 100.0, 80.0))
    box("Searchlight", 300.0, hv + 250.0, top, (60.0, 60.0, 100.0), mat="metal", mesh=CYL)   # свет потом, не мешем
    flag(0.0, hv, top + 460.0, pole=600.0)

    # ---- Мост: с правого края низкой (v = lv + half) на галерею высокой (v = hv - 350), подъём 6 м ----
    b0, b1 = lv + half, hv - 350.0
    slab("Bridge", 0.0, b0, z + lh, 0.0, b1, z + top, 250.0, mat="wood")
    for side in (-1.0, 1.0):
        slab("BridgeRail{:+.0f}".format(side), side * 135.0, b0, z + lh + 100.0, side * 135.0, b1, z + top + 100.0,
             10.0, thick=100.0, mat="wood")

    # У подножия: мешки, укрытие охраны.
    for k, (su, sv, dy) in enumerate(((1700.0, 400.0, -20.0), (900.0, 1300.0, 60.0), (-900.0, 300.0, 90.0))):
        box("Sacks{}".format(k), su, sv, 0.0, (250.0, 70.0, 90.0), dyaw=dy, mat="sacks")
    # Точки: первые две на низкой вышке (трасса сверху попадает на её верх), дальше земля с запасом под
    # танкетку. Грунты в списке охраны идут первыми (camp_guards), так на вышку встают они.
    posts = [(-150.0, lv - 150.0), (-150.0, lv + 150.0), (1800.0, 900.0), (1900.0, -300.0), (1200.0, 1900.0),
             (-1300.0, 0.0), (-1300.0, 1300.0), (600.0, -1600.0)]
    return lit, (60.0, hv + 60.0, top + 80.0 + 60.0), posts


class _Kit(object):
    """Помощники кусков поверх box/at: плита, лежачий цилиндр, сарай, кольцо точек."""

    def __init__(self, s, box, at, z, a, folder):
        self.s, self.box, self.at, self.z, self.a, self.folder = s, box, at, z, a, folder

    def slab(self, label, u0, v0, z0, u1, v1, z1, width, thick=30.0, mat="concrete", collision=True):
        """Наклонная плита от точки к точке, z абсолютные по верхней грани. Пандус, мост, транспортёр."""
        x0, y0 = self.at(u0, v0)
        x1, y1 = self.at(u1, v1)
        run = math.hypot(x1 - x0, y1 - y0)
        return shape(KIT_BOX, "Camp_{}_{}".format(self.s["name"], label),
                     ((x0 + x1) * 0.5, (y0 + y1) * 0.5, (z0 + z1) * 0.5 - thick * 0.5),
                     (math.hypot(run, z1 - z0) + 20.0, width, thick),
                     yaw=math.degrees(math.atan2(y1 - y0, x1 - x0)), pitch=math.degrees(math.atan2(z1 - z0, run)),
                     mat=mat, folder=self.folder, collision=collision, tag=TAG_CAMP)

    def lying(self, label, u, v, z0, d, length, dyaw=0.0, mat="trunk", collision=True):
        """Лежачий цилиндр осью вдоль u (плюс dyaw): бревно, бочка, барабан. Как в step_base."""
        x, y = self.at(u, v)
        return shape(CYL, "Camp_{}_{}".format(self.s["name"], label), (x, y, self.z + z0 + d * 0.5), (d, d, length),
                     yaw=math.degrees(self.a) + dyaw - 90.0, roll=90.0, mat=mat, folder=self.folder,
                     collision=collision, tag=TAG_CAMP)

    def shed(self, name, cu, cv, du, dv, h, mat="wood", roof="roof", open_front=True):
        """Сарай du x dv высотой h с центром (cu, cv): задняя и боковые стены, крыша; фасад (+u) открыт."""
        b = self.box
        b(name + "Back", cu - du * 0.5 + 10.0, cv, 0.0, (20.0, dv, h), mat=mat)
        b(name + "L", cu, cv - dv * 0.5 + 10.0, 0.0, (du, 20.0, h), mat=mat)
        b(name + "R", cu, cv + dv * 0.5 - 10.0, 0.0, (du, 20.0, h), mat=mat)
        if not open_front:
            b(name + "Front", cu + du * 0.5 - 10.0, cv, 0.0, (20.0, dv, h), mat=mat)
        b(name + "Roof", cu, cv, h, (du + 60.0, dv + 60.0, 20.0), mat=roof)

    @staticmethod
    def ring(cu, cv, r, n, phase=0.0):
        return [(cu + r * math.cos(math.radians(phase + 360.0 * k / n)),
                 cv + r * math.sin(math.radians(phase + 360.0 * k / n))) for k in range(n)]


def _piece_hay(s, box, at, flag, fire, mast, lit, z, a, folder):
    """BehindTop_1, easy: сенокос. Крепость из квадратных тюков 245 x 120 x 90 (задняя стена в три яруса,
    боковые в два), открыта к нашей базе; внутри прицеп с сеном, деталь на прицепе. Пресс-подборщик рядом."""
    for layer in range(3):
        for k in range(6):
            box("BaleB{}{}".format(layer, k), -700.0, -612.5 + k * 245.0, layer * 90.0, (120.0, 245.0, 90.0), mat="hay")
    for side, sname in ((-1.0, "L"), (1.0, "R")):
        for layer in range(2):
            for k in range(4):
                box("Bale{}{}{}".format(sname, layer, k), -517.5 + k * 245.0, side * 795.0, layer * 90.0,
                    (245.0, 120.0, 90.0), mat="hay")
    box("TrailerDeck", 0.0, 0.0, 80.0, (600.0, 240.0, 30.0), mat="wood")
    for k, (du, dv) in enumerate(((-200.0, -130.0), (-200.0, 130.0), (200.0, -130.0), (200.0, 130.0))):
        box("TrailerWheel{}".format(k), du, dv, 0.0, (80.0, 30.0, 80.0), mat="tire")
    box("TrailerBale", -150.0, 0.0, 110.0, (245.0, 120.0, 90.0), dyaw=90.0, mat="hay")
    box("Crate", 170.0, 0.0, 110.0, (140.0, 140.0, 100.0))
    box("Baler", 500.0, 1150.0, 0.0, (400.0, 250.0, 220.0), mat="car_red")
    fire(450.0, -550.0)
    flag(-700.0, 0.0, 270.0)
    return lit, (170.0, 0.0, 280.0), _Kit.ring(450.0, -550.0, 300.0, 6) + [(250.0, 400.0)]


def _piece_machine_yard(s, box, at, flag, fire, mast, lit, z, a, folder):
    """BehindTop_2, easy: машинный двор. Открытый навес 12 x 18 м, свес 4.3 м, задняя стена глухая; внутри
    комбайн, деталь на верстаке. Бак с топливом, костёр у навеса."""
    du, dv, h = 1220.0, 1830.0, 430.0
    box("ShedBack", -du * 0.5, 0.0, 0.0, (20.0, dv, h), mat="metal")
    for k in range(4):
        v = -dv * 0.5 + k * dv / 3.0
        box("ShedPostF{}".format(k), du * 0.5, v, 0.0, (30.0, 30.0, h), mat="wood", mesh=CYL)
    box("ShedRoof", 0.0, 0.0, h, (du + 100.0, dv + 60.0, 20.0), mat="roof")
    box("CombineBody", 50.0, -350.0, 80.0, (650.0, 330.0, 280.0), mat="tractor")
    box("CombineCab", 250.0, -350.0, 360.0, (180.0, 180.0, 60.0), mat="metal")
    box("CombineHeader", 450.0, -350.0, 30.0, (180.0, 600.0, 90.0), mat="metal")
    for k, dv2 in enumerate((-190.0, 190.0)):
        box("CombineWheel{}".format(k), 150.0, -350.0 + dv2, 0.0, (160.0, 40.0, 160.0), mat="tire")
    box("Workbench", -450.0, 500.0, 0.0, (200.0, 80.0, 90.0))
    box("FuelTank", 800.0, 1200.0, 100.0, (120.0, 120.0, 200.0), mat="fuel", mesh=CYL)
    fire(1000.0, 0.0)
    flag(-500.0, -800.0, h + 20.0)
    return lit, (-450.0, 500.0, 160.0), _Kit.ring(1000.0, 0.0, 300.0, 6) + [(-300.0, 300.0)]


def _piece_pump(s, box, at, flag, fire, mast, lit, z, a, folder):
    """BehindTop_3, medium: насосная орошения. Будка с насосом у пруда, деталь в будке; пролёт поливалки
    35 м боком наружу за край площадки, на треугольных опорах: это силуэт."""
    K = _Kit(s, box, at, z, a, folder)
    box("Pond", -700.0, -500.0, 0.0, (1200.0, 900.0, 5.0), mat="pool", collision=False)
    K.shed("Pump", 0.0, 500.0, 400.0, 300.0, 280.0, mat="concrete")
    box("PumpMotor", -80.0, 500.0, 0.0, (120.0, 90.0, 110.0), mat="metal")
    box("Crate", 80.0, 500.0, 0.0, (100.0, 100.0, 80.0))
    box("PumpPipe", -350.0, 150.0, 30.0, (20.0, 700.0, 20.0), mat="metal", dyaw=0.0)
    # Поливалка: центр у будки, труба на 3.5 м вбок, опоры A-формы до своей земли.
    pv0, pv1, ph = 700.0, 4200.0, 350.0
    box("PivotBase", -400.0, pv0, 0.0, (80.0, 80.0, ph + 60.0), mat="metal", mesh=CYL)
    K.slab("PivotPipe", -400.0, pv0, z + ph + 20.0, -400.0, pv1, z + ph + 20.0, 25.0, thick=25.0, mat="metal",
           collision=False)
    for k, tv in enumerate((pv0 + (pv1 - pv0) * 0.5, pv1)):
        gx, gy = at(-400.0, tv)
        g = _ground_z(gx, gy, z)
        for side in (-1.0, 1.0):
            K.slab("PivotLeg{}{:+.0f}".format(k, side), -400.0 + side * 180.0, tv, g, -400.0, tv, z + ph, 15.0,
                   thick=15.0, mat="metal", collision=False)
    mast(-1300.0, -300.0)
    flag(0.0, 500.0, 300.0)
    return lit, (80.0, 500.0, 150.0), [(350.0, 450.0), (350.0, 700.0), (500.0, 250.0), (-200.0, 0.0),
                                       (600.0, -300.0), (-600.0, 900.0)]


def _piece_speeder(s, box, at, flag, fire, mast, lit, z, a, folder):
    """BehindBot_1, easy: будка обходчика. Тупик пути заходит в сарай для дрезин 7.3 x 3.7 м, дрезина на
    пути перед сараем, деталь на дрезине. Костёр у будки."""
    K = _Kit(s, box, at, z, a, folder)
    tv = -300.0
    for side in (-1.0, 1.0):
        box("Rail{:+.0f}".format(side), 0.0, tv + side * 72.0, 15.0, (2400.0, 10.0, 15.0), mat="metal")
    for k in range(10):
        box("Sleeper{}".format(k), -1100.0 + k * 240.0, tv, 0.0, (25.0, 250.0, 15.0), mat="trunk")
    box("BufferStop", -1220.0, tv, 0.0, (40.0, 250.0, 120.0), mat="car_red")
    K.shed("Speeder", -800.0, tv, 730.0, 370.0, 300.0, mat="wood")
    box("SpeederDeck", 100.0, tv, 40.0, (180.0, 150.0, 25.0), mat="car_red")
    box("SpeederSeat", 50.0, tv, 65.0, (60.0, 150.0, 40.0), mat="metal")
    box("ToolRack", -500.0, 300.0, 0.0, (40.0, 200.0, 180.0))
    fire(300.0, 550.0)
    flag(-800.0, tv, 320.0)
    return lit, (130.0, tv, 65.0 + 70.0), _Kit.ring(300.0, 550.0, 300.0, 6) + [(300.0, -700.0)]


def _piece_loading(s, box, at, flag, fire, mast, lit, z, a, folder):
    """BehindBot_2, easy: разгрузочная площадка. Рампа 1.2 м вдоль пути, пандус к ней, ящики на ней,
    платформа-вагон рядом; деталь на рампе. Костёр у рампы."""
    K = _Kit(s, box, at, z, a, folder)
    dv, dh = -400.0, 120.0
    box("Dock", -200.0, dv, 0.0, (1600.0, 400.0, dh), mat="concrete")
    K.slab("DockRamp", 1000.0, dv, z, 600.0, dv, z + dh, 300.0)
    for k, (cu, cz) in enumerate(((-700.0, 0.0), (-700.0, 150.0), (-500.0, 0.0), (-300.0, 0.0))):
        box("DockCrate{}".format(k), cu, dv - 60.0, dh + cz, (150.0, 150.0, 150.0), mat="wood")
    box("Crate", 250.0, dv, dh, (140.0, 140.0, 100.0))
    for side in (-1.0, 1.0):
        box("Rail{:+.0f}".format(side), 0.0, -850.0 + side * 72.0, 0.0, (2400.0, 10.0, 15.0), mat="metal")
    box("Flatcar", -300.0, -850.0, 60.0, (1500.0, 280.0, 60.0), mat="container")
    for k in range(4):
        box("FlatcarWheel{}".format(k), -900.0 + k * 400.0, -850.0, 0.0, (80.0, 260.0, 60.0), mat="tire")
    fire(350.0, 500.0)
    flag(-950.0, dv + 150.0, dh)
    return lit, (250.0, dv, dh + 100.0 + 70.0), _Kit.ring(350.0, 500.0, 300.0, 6) + [(700.0, -100.0)]


def _piece_sawmill(s, box, at, flag, fire, mast, lit, z, a, folder):
    """BehindBot_3, medium: лесопилка. Навес 14 x 5 м над пилой, бревно на станке, деталь на станке;
    штабель брёвен за навесом, стопки досок, мачта."""
    K = _Kit(s, box, at, z, a, folder)
    du, dv, h = 1400.0, 500.0, 370.0
    for k in range(3):
        u = -du * 0.5 + k * du * 0.5
        for side in (-1.0, 1.0):
            box("ShedPost{}{:+.0f}".format(k, side), u, side * dv * 0.5, 0.0, (25.0, 25.0, h), mesh=CYL)
    box("ShedRoof", 0.0, 0.0, h, (du + 80.0, dv + 120.0, 20.0), mat="roof")
    box("SawBed", 0.0, 0.0, 0.0, (900.0, 80.0, 60.0), mat="metal")
    box("SawHead", 200.0, 0.0, 60.0, (60.0, 160.0, 150.0), mat="car_red")
    K.lying("SawLog", -150.0, 0.0, 60.0, 55.0, 500.0)
    for k in range(3):
        for j in range(2 - (k // 2)):
            K.lying("DeckLog{}{}".format(k, j), -100.0, 900.0 + (k - 1) * 65.0 + j * 30.0, j * 55.0, 60.0, 900.0)
    for k, cu in enumerate((-500.0, 100.0)):
        box("Lumber{}".format(k), cu, -950.0, 0.0, (500.0, 120.0, 110.0))
    mast(-1000.0, 1000.0)
    flag(700.0, 250.0, h + 20.0)
    return lit, (320.0, 0.0, 130.0), [(300.0, 350.0), (-300.0, -350.0), (900.0, 0.0), (-900.0, 0.0),
                                      (500.0, -700.0), (-400.0, 700.0)]


def _piece_autoyard(s, box, at, flag, fire, mast, lit, z, a, folder):
    """JungleTop_1, easy: двор автомастерской. Остовы машин рядами, часть в два яруса, шины стопками,
    эвакуатор, деталь на его платформе. Огонь в бочке."""
    K = _Kit(s, box, at, z, a, folder)
    cols = ("car_red", "car_white", "car_green", "truck")
    for side in (-1.0, 1.0):
        for k in range(4):
            u = -900.0 + k * 520.0
            box("Wreck{:+.0f}{}".format(side, k), u, side * 950.0, 0.0, (450.0, 180.0, 140.0), dyaw=(k % 2) * 6.0,
                mat=cols[k % 4])
            if k % 2 == 0:
                box("WreckTop{:+.0f}{}".format(side, k), u + 30.0, side * 950.0, 140.0, (430.0, 175.0, 120.0),
                    dyaw=-8.0, mat=cols[(k + 1) % 4])
    for k, (tu, tv, n) in enumerate(((-1100.0, 0.0, 4), (-800.0, -350.0, 3), (900.0, 700.0, 5))):
        box("Tires{}".format(k), tu, tv, 0.0, (80.0, 80.0, 22.0 * n), mat="tire", mesh=CYL)
    box("TowBed", 50.0, 0.0, 80.0, (500.0, 230.0, 30.0), mat="metal")
    box("TowCab", 400.0, 0.0, 60.0, (200.0, 230.0, 180.0), mat="truck")
    box("TowChassis", 150.0, 0.0, 30.0, (700.0, 200.0, 50.0), mat="tire")
    K.slab("TowBoom", -150.0, 0.0, z + 110.0, -350.0, 0.0, z + 280.0, 30.0, thick=30.0, mat="metal", collision=False)
    box("FireBarrel", 700.0, -450.0, 0.0, (70.0, 70.0, 90.0), mat="metal", mesh=CYL)
    px, py = at(700.0, -450.0)
    lit.append(_fx("Camp_{}_Fire".format(s["name"]), NS_FIRE, (px, py, z + 95.0), 0.8, folder))
    lit.append(_fx("Camp_{}_Smoke".format(s["name"]), NS_SMOKE, (px, py, z + 150.0), 3.0, folder))
    flag(-1100.0, 400.0, 0.0)
    return lit, (100.0, 0.0, 110.0 + 70.0), _Kit.ring(700.0, -450.0, 280.0, 6) + [(400.0, 400.0)]


def _piece_foundation(s, box, at, flag, fire, mast, lit, z, a, folder):
    """JungleTop_2, medium, яма: котлован недостроя. Недолитый фундамент по пояс с арматурой, поддоны
    блоков, бетономешалка; деталь на поддоне. Мачта из ямы."""
    K = _Kit(s, box, at, z, a, folder)
    fu, fv, fh = 1400.0, 1000.0, 120.0
    box("FoundBack", -fu * 0.5, 0.0, 0.0, (30.0, fv, fh), mat="concrete")
    box("FoundL", 0.0, -fv * 0.5, 0.0, (fu, 30.0, fh), mat="concrete")
    box("FoundR", -200.0, fv * 0.5, 0.0, (fu - 400.0, 30.0, fh), mat="concrete")
    for k in range(8):
        u = -fu * 0.5 + 100.0 + k * 170.0
        box("Rebar{}".format(k), u, -fv * 0.5, fh, (8.0, 8.0, 140.0), mat="metal", mesh=CYL, collision=False)
    for k, (bu, bv, n) in enumerate(((900.0, 600.0, 1), (900.0, -700.0, 2), (-1000.0, 900.0, 2))):
        box("Pallet{}".format(k), bu, bv, 0.0, (130.0, 130.0, 15.0), mat="wood")
        for j in range(n):
            box("Blocks{}{}".format(k, j), bu, bv, 15.0 + j * 100.0, (120.0, 120.0, 100.0), mat="concrete")
    box("MixerFrame", -500.0, -1000.0, 0.0, (250.0, 150.0, 80.0), mat="metal")
    K.lying("MixerDrum", -500.0, -1000.0, 80.0, 140.0, 200.0, dyaw=0.0, mat="car_red")
    mast(-900.0, 800.0)
    flag(-fu * 0.5, -fv * 0.5, fh)
    return lit, (900.0, 600.0, 115.0 + 70.0), [(600.0, 300.0), (600.0, -300.0), (-300.0, 0.0), (1100.0, 0.0),
                                               (-500.0, -700.0), (300.0, 900.0)]


def _piece_celltower(s, box, at, flag, fire, mast, lit, z, a, folder):
    """JungleTop_4, hard, холм: вышка связи корпорации и серверный модуль. Решётчатая вышка 30 м (ландмарк,
    не лазить); модуль: платформа из двух контейнеров, на ней короткий контейнер с открытым торцом, к крыше
    пандус 26 градусов. Деталь в верхнем контейнере, по навмешу (урок вышки лесничества)."""
    K = _Kit(s, box, at, z, a, folder)
    # Вышка связи: три ноги треугольником, пояса через 5 м, антенны наверху.
    tu, tv, th = -300.0, 900.0, 3000.0
    legs = [(tu + 160.0 * math.cos(math.radians(t)), tv + 160.0 * math.sin(math.radians(t))) for t in (0.0, 120.0, 240.0)]
    for k, (lu, lv) in enumerate(legs):
        box("TowerLeg{}".format(k), lu, lv, 0.0, (25.0, 25.0, th), mat="metal", mesh=CYL)
    for lvl in range(500, int(th), 500):
        for k in range(3):
            (u0, v0), (u1, v1) = legs[k], legs[(k + 1) % 3]
            K.slab("TowerBrace{}_{}".format(lvl, k), u0, v0, z + lvl, u1, v1, z + lvl, 10.0, thick=10.0, mat="metal",
                   collision=False)
    for k in range(3):
        box("Antenna{}".format(k), tu + 100.0 * math.cos(math.radians(k * 120.0 + 60.0)),
            tv + 100.0 * math.sin(math.radians(k * 120.0 + 60.0)), th - 400.0, (30.0, 60.0, 250.0), mat="corp_white")
    # Серверный модуль.
    mu, mv, ph = 0.0, -800.0, 260.0
    box("PlatformA", mu, mv - 122.0, 0.0, (610.0, 245.0, ph), mat="container")
    box("PlatformB", mu, mv + 122.0, 0.0, (610.0, 245.0, ph), mat="corp_white")
    cu0, cu1, ch = -305.0, -5.0, 250.0     # верхний контейнер 3 м, торец к +u открыт
    box("UpperBack", cu0 + 10.0, mv, ph, (20.0, 245.0, ch), mat="corp_black")
    for side in (-1.0, 1.0):
        box("UpperSide{:+.0f}".format(side), (cu0 + cu1) * 0.5, mv + side * 112.0, ph, (cu1 - cu0, 20.0, ch), mat="corp_black")
    box("UpperRoof", (cu0 + cu1) * 0.5, mv, ph + ch, (cu1 - cu0 + 20.0, 265.0, 20.0), mat="corp_black")
    box("ServerRack", cu0 + 60.0, mv, ph, (60.0, 200.0, 200.0), mat="metal")
    box("Crate", -150.0, mv, ph, (100.0, 100.0, 80.0))
    K.slab("ModuleRamp", 850.0, mv, z, 305.0, mv, z + ph, 200.0, mat="metal")
    for side in (-1.0, 1.0):
        K.slab("ModuleRampRail{:+.0f}".format(side), 850.0, mv + side * 110.0, z + 100.0, 305.0, mv + side * 110.0,
               z + ph + 100.0, 8.0, thick=100.0, mat="metal")
    box("Searchlight", 250.0, mv + 180.0, ph, (60.0, 60.0, 100.0), mat="metal", mesh=CYL)   # свет потом, не мешем
    box("Generator", 400.0, 200.0, 0.0, (300.0, 150.0, 160.0), mat="fuel")
    for k, (su, sv, dy) in enumerate(((1600.0, 300.0, -20.0), (900.0, -1600.0, 60.0), (-1200.0, -300.0, 90.0))):
        box("Sacks{}".format(k), su, sv, 0.0, (250.0, 70.0, 90.0), dyaw=dy, mat="sacks")
    flag(-150.0, mv, ph + ch + 20.0)
    # Первые две точки на крыше платформы перед верхним контейнером (грунты идут первыми).
    posts = [(150.0, mv - 70.0), (150.0, mv + 70.0), (1800.0, -600.0), (1600.0, 900.0), (-1300.0, -1200.0),
             (-1300.0, 400.0), (700.0, 1600.0), (1900.0, 300.0)]
    return lit, (-150.0, mv, ph + 80.0 + 60.0), posts


def _piece_gravel(s, box, at, flag, fire, mast, lit, z, a, folder):
    """JungleBot_2, medium, яма: гравийный карьер. Кучи щебня, наклонный транспортёр от грохота (по нему можно
    забраться, 20 градусов), экскаватор; деталь у ковша. Мачта."""
    K = _Kit(s, box, at, z, a, folder)
    for k, (gu, gv, d, hh) in enumerate(((-600.0, -700.0, 900.0, 400.0), (-1100.0, 300.0, 700.0, 300.0),
                                        (-300.0, 800.0, 500.0, 220.0))):
        box("Pile{}".format(k), gu, gv, 0.0, (d, d, hh), mat="rock", mesh=CONE)
    box("Screener", 900.0, -700.0, 0.0, (400.0, 250.0, 250.0), mat="bus")
    K.slab("Conveyor", 700.0, -700.0, z + 250.0, -300.0, -700.0, z + 620.0, 100.0, thick=20.0, mat="tire")
    for k, cu in enumerate((400.0, 50.0)):
        box("ConveyorLeg{}".format(k), cu, -700.0, 0.0, (20.0, 80.0, 250.0 + (700.0 - cu) * 0.37 - 20.0), mat="metal")
    box("ExcTracks", 300.0, 700.0, 0.0, (450.0, 300.0, 100.0), mat="tire")
    box("ExcBody", 300.0, 700.0, 100.0, (350.0, 280.0, 200.0), mat="fuel")
    K.slab("ExcBoom", 450.0, 700.0, z + 280.0, 850.0, 700.0, z + 420.0, 40.0, thick=40.0, mat="fuel", collision=False)
    K.slab("ExcArm", 850.0, 700.0, z + 420.0, 1000.0, 700.0, z + 100.0, 30.0, thick=30.0, mat="fuel", collision=False)
    box("ExcBucket", 1050.0, 700.0, 0.0, (150.0, 150.0, 70.0), mat="metal")
    mast(-1200.0, -1100.0)
    flag(900.0, -700.0, 250.0)
    return lit, (1050.0, 700.0, 70.0 + 70.0), [(700.0, 300.0), (700.0, 1100.0), (1300.0, 700.0), (0.0, 0.0),
                                               (1300.0, -300.0), (-800.0, 1100.0)]


def _piece_drypond(s, box, at, flag, fire, mast, lit, z, a, folder):
    """JungleBot_3, medium, яма: высохший пруд. Мостки на сваях над сухим дном, лодка у их конца, деталь в
    лодке; лодочный сарай на дне. Мачта."""
    K = _Kit(s, box, at, z, a, folder)
    box("Mud", 0.0, 0.0, 0.0, (1800.0, 1400.0, 4.0), mat="soil", collision=False)
    dv, dz = 600.0, 150.0
    box("Dock", -600.0, dv, dz, (1300.0, 250.0, 20.0))
    for k in range(5):
        for side in (-1.0, 1.0):
            box("Pile{}{:+.0f}".format(k, side), -1200.0 + k * 300.0, dv + side * 110.0, 0.0, (25.0, 25.0, dz), mesh=CYL,
                mat="trunk")
    K.slab("DockSteps", 350.0, dv, z, 50.0, dv, z + dz + 20.0, 200.0, mat="wood")
    box("BoatHull", 350.0, 100.0, 0.0, (500.0, 170.0, 60.0), dyaw=15.0, mat="car_white")
    box("BoatSeat", 330.0, 100.0, 30.0, (40.0, 150.0, 40.0), dyaw=15.0)
    K.shed("Boathouse", -900.0, -600.0, 600.0, 450.0, 350.0, mat="wood")
    K.lying("OldCanoe", -300.0, -900.0, 0.0, 70.0, 450.0, dyaw=40.0, mat="car_green")
    mast(-500.0, -1300.0)
    flag(-900.0, -600.0, 370.0)
    return lit, (400.0, 100.0, 60.0 + 70.0), [(700.0, 400.0), (700.0, -200.0), (100.0, -300.0), (-300.0, 300.0),
                                              (1000.0, 100.0), (-500.0, 1000.0)]


CAMP_PIECES = {
    "BehindTop_1": _piece_hay,
    "BehindTop_2": _piece_machine_yard,
    "BehindTop_3": _piece_pump,
    "BehindBot_1": _piece_speeder,
    "BehindBot_2": _piece_loading,
    "BehindBot_3": _piece_sawmill,
    "JungleTop_1": _piece_autoyard,
    "JungleTop_2": _piece_foundation,
    "JungleTop_3": _piece_silage,
    "JungleTop_4": _piece_celltower,
    "JungleBot_1": _piece_apiary,
    "JungleBot_2": _piece_gravel,
    "JungleBot_3": _piece_drypond,
    "JungleBot_4": _piece_lookout,
}


def _camp_piece(s, x, y, z, yaw, folder):
    """Ландмарк и укрытия одного кемпа. u вперёд (к нашей базе), v вправо. Возвращает акторы ландмарка."""
    a = math.radians(yaw)

    def at(u, v):
        return x + u * math.cos(a) - v * math.sin(a), y + u * math.sin(a) + v * math.cos(a)

    def box(tag_label, u, v, z0, size, dyaw=0.0, mat="wood", collision=True, mesh=KIT_BOX, lit=False):
        px, py = at(u, v)
        act = shape(mesh, "Camp_{}_{}".format(s["name"], tag_label), (px, py, z + z0 + size[2] * 0.5), size,
                    yaw=yaw + dyaw, mat=mat, folder=folder, collision=collision, tag=TAG_CAMP)
        if lit:
            act.set_editor_property("tags", [unreal.Name(TAG_CAMP), unreal.Name("CampLit")])
        return act

    lit = []

    def flag(u, v, z0, pole=1000.0):
        """Флаг корпорации (автор 2026-09-30: белый с чёрным) над постом. Полотно гаснет с кемпом (CampLit)."""
        box("FlagPole", u, v, z0, (14.0, 14.0, pole), mat="metal", mesh=CYL)
        lit.append(box("Flag", u, v + 100.0, z0 + pole - 140.0, (8.0, 200.0, 130.0), mat="corp_white",
                       collision=False, lit=True))
        lit.append(box("FlagBand", u, v + 100.0, z0 + pole - 90.0, (10.0, 202.0, 30.0), mat="corp_black",
                       collision=False, lit=True))

    def fire(fu, fv):
        """Костёр с дымом: знак easy-кемпа."""
        box("FireRing", fu, fv, 0.0, (220.0, 220.0, 30.0), mat="rock", collision=False, mesh=CYL)
        box("Log1", fu, fv, 30.0, (180.0, 30.0, 30.0), dyaw=30.0, collision=False)
        box("Log2", fu, fv, 30.0, (180.0, 30.0, 30.0), dyaw=-30.0, collision=False)
        px, py = at(fu, fv)
        lit.append(_fx("Camp_{}_Fire".format(s["name"]), NS_FIRE, (px, py, z + 40.0), 1.5, folder))
        lit.append(_fx("Camp_{}_Smoke".format(s["name"]), NS_SMOKE, (px, py, z + 120.0), 3.0, folder))

    def mast(mu, mv, z0=0.0):
        """Мачта ретранслятора 18 м с красной лампой: знак medium-кемпа."""
        box("Mast", mu, mv, z0, (35.0, 35.0, 1800.0), mat="metal", mesh=CYL)
        lit.append(box("MastLamp", mu, mv, z0 + 1800.0, (90.0, 90.0, 90.0), mat="lamp_red", collision=False,
                       mesh=SPHERE, lit=True))

    special = CAMP_PIECES.get(s["name"])
    if special:
        return special(s, box, at, flag, fire, mast, lit, z, a, folder)

    # Общее: ящик с деталью в центре (деталь рисует сам кемп) и низкие мешки по кругу, 6 м.
    box("Crate", 0.0, 0.0, 0.0, (140.0, 140.0, 100.0))
    for k, ang in enumerate((45.0, 165.0, 285.0)):
        r = math.radians(ang)
        box("Sacks{}".format(k), 600.0 * math.cos(r), 600.0 * math.sin(r), 0.0, (250.0, 70.0, 90.0),
            dyaw=ang + 90.0, mat="sacks")

    tier = s["tier"]
    if tier == "easy":
        # Костёр у ящика, над ним столб дыма; рядом машина охраны.
        fire(-300.0, 250.0)
        box("TruckBody", 200.0, -650.0, 50.0, (500.0, 220.0, 110.0), dyaw=15.0, mat="truck")
        box("TruckCab", 330.0, -615.0, 160.0, (180.0, 210.0, 110.0), dyaw=15.0, mat="truck")
    elif tier == "medium":
        # Склад: два контейнера, тент, мачта 18 м с красной лампой (видна из ямы над лесом).
        box("ContainerA", -500.0, 550.0, 0.0, (610.0, 245.0, 260.0), dyaw=20.0, mat="container")
        box("ContainerB", 400.0, -600.0, 0.0, (610.0, 245.0, 260.0), dyaw=-10.0, mat="container")
        for k, (pu, pv) in enumerate(((-300.0, -450.0), (100.0, -450.0), (-300.0, -150.0), (100.0, -150.0))):
            box("TentPost{}".format(k), pu, pv, 0.0, (15.0, 15.0, 250.0), mesh=CYL)
        box("TentRoof", -100.0, -300.0, 250.0, (460.0, 360.0, 15.0), mat="tarp")
        mast(-250.0, -750.0)
    else:
        # Вышка 14 м с прожектором; контейнер и мешки у подножия.
        tu, tv, h = -450.0, 0.0, 1400.0
        for k, (du, dv) in enumerate(((-250.0, -250.0), (250.0, -250.0), (-250.0, 250.0), (250.0, 250.0))):
            box("TowerLeg{}".format(k), tu + du, tv + dv, 0.0, (40.0, 40.0, h), mat="metal", mesh=CYL)
            box("TowerRoofPost{}".format(k), tu + du, tv + dv, h + 30.0, (20.0, 20.0, 220.0), mat="metal", mesh=CYL)
        box("TowerDeck", tu, tv, h, (600.0, 600.0, 30.0), mat="wood")
        box("TowerRoof", tu, tv, h + 250.0, (650.0, 650.0, 20.0), mat="roof")
        for k, (du, dv, rot) in enumerate(((300.0, 0.0, 90.0), (-300.0, 0.0, 90.0), (0.0, 300.0, 0.0), (0.0, -300.0, 0.0))):
            box("TowerRail{}".format(k), tu + du, tv + dv, h + 30.0, (600.0, 20.0, 100.0), dyaw=rot, mat="wood")
        box("Searchlight", tu + 250.0, tv, h + 30.0, (80.0, 80.0, 120.0), mat="metal", mesh=CYL)
        box("ContainerA", 300.0, 550.0, 0.0, (610.0, 245.0, 260.0), dyaw=70.0, mat="container")
    flag(-700.0, 700.0, 0.0)
    return lit, (0.0, 0.0, 170.0), []


def _forest_side(zone):
    return "Top" if zone.endswith("Top") else "Bot"


def step_camps():
    """14 кемпов по layout.json, детали меха, сарай-сборщик. Идемпотентно по тегу LanesCamp."""
    import importlib.util
    guard()
    lay = layout()
    eas = _eas()
    _clear(TAG_CAMP)
    # Кемпы и сарай ставит только этот шаг: остатки без тега (упавший прогон) тоже его.
    for act in eas.get_all_level_actors():
        if isinstance(act, (unreal.SiegeCampSite, unreal.SiegeMechBay)):
            log("DELETED: без тега " + act.get_actor_label())
            act.destroy_actor()
    parts = mech_parts()
    c = lay["corner"]
    n0 = _count["n"]

    for s in lay["poi_slots"]:
        z = _ground_z(s["x"], s["y"], s["z"])
        yaw = math.degrees(math.atan2(-c - s["y"], -c - s["x"]))    # фасадом к нашей базе
        folder = "Lanes/Camps/" + s["name"]
        lit, part_at, posts = _camp_piece(s, s["x"], s["y"], z, yaw, folder)
        camp = eas.spawn_actor_from_class(unreal.SiegeCampSite, unreal.Vector(s["x"], s["y"], z),
                                          unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
        _tag(camp, TAG_CAMP, "LN_Camp_" + s["name"], folder)   # сразу: упавший прогон иначе оставит актор без тега
        # Деталь лежит там, где её положил кусок (в сарае, у въезда траншеи, в кабине вышки).
        camp.get_editor_property("part_display").set_relative_location(unreal.Vector(*part_at), False, False)
        camp.set_editor_property("guard_posts", [unreal.Vector(pu, pv, 0.0) for pu, pv in posts])
        camp.set_editor_property("slot_name", unreal.Name(s["name"]))
        camp.set_editor_property("forest_side", unreal.Name(_forest_side(s["zone"])))
        camp.set_editor_property("tier", getattr(unreal.SiegeCampTier, TIER_ENUM[s["tier"]]))
        camp.set_editor_property("landmark_actors", lit)
        camp.set_editor_property("guards", camp_guards(s["tier"]))
        camp.set_editor_property("part_pool", parts)

    # Разметку мест (кольца и подписи из step_markup) в игре не видно: у кемпов нет значков (автор).
    hidden = 0
    for act in eas.get_all_level_actors():
        if act.actor_has_tag(TAG_GEO) and act.get_actor_label().startswith("Camp_"):
            act.set_actor_hidden_in_game(True)
            hidden += 1

    # Сарай: триггер во всю коробку, мех собирается, когда вошёл игрок с полным набором у команды.
    spec = importlib.util.spec_from_file_location("base_layout", os.path.join(HERE, "base_layout" + "." + "py"))
    bl = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bl)
    bx, by = bl.polar(275.0, 3900.0)
    byaw = bl.facing_center(bx, by)
    top = _ground_z(-c, -c, lay["base_z"])
    bay = eas.spawn_actor_from_class(unreal.SiegeMechBay, unreal.Vector(-c + bx, -c + by, top + 300.0),
                                     unreal.Rotator(roll=0.0, pitch=0.0, yaw=byaw))
    bay.get_editor_property("trigger").set_box_extent(unreal.Vector(600.0, 1000.0, 300.0), False)
    _tag(bay, TAG_CAMP, "LN_MechBay", "Lanes/Camps")

    log("ADDED: {} кемпов ({} мешей), сарай-сборщик; скрыто в игре {} меток разметки".format(
        len(lay["poi_slots"]), _count["n"] - n0, hidden))
    _les().save_current_level()
