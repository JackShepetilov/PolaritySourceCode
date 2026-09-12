"""Поставить скелет прод-карты в редактор, чтобы посмотреть на него в масштабе 1:1.

Рельефа тут нет и не должно быть: это скелет, а не карта. Смысл единственный - пролететь и
почувствовать, сколько на самом деле 1372 метра и как далеко друг от друга стоят точки. Цифра
"20-66 секунд бегом" на бумаге ничего не говорит, пока не пройдёшь.

Читает layout.json, который пишет layout.py. Метры там, юниты считаются здесь: смешивать единицы
в одном файле - надёжный способ однажды поставить точку в ста метрах от места.

Идемпотентно по тегу: повторный запуск сносит своё и ставит заново, чужого не трогает.

Запуск через MCP (скрипт длинный, целиком в execute_python_code он не доедет - грабля из
Docs/Gotchas/Python_Editor.md):

    p = r"<этот файл>"
    g = {"__name__": "__main__"}
    exec(compile(open(p, encoding="utf-8").read(), p, "exec"), g)
    g["main"]()
"""

import json
import math
import os

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "layout.json")

LEVEL_PATH = "/Game/MapProd/L_ProdLayout"
TAG = "MapProdLayout"

M = 100.0                      # метр в юнитах

CYL = "/Engine/BasicShapes/Cylinder"
CUBE = "/Engine/BasicShapes/Cube"

# Диаметр и высота маркера в метрах, по РАНГУ места. Ранг это свойство слота и не меняется от
# забега к забегу, в отличие от роли.
TIER_SIZE = {
    "hub":    (30.0, 70.0),
    "major":  (22.0, 45.0),
    "buffer": (13.0, 26.0),
}

# FColor, 0-255. Намеренно не LinearColor: set_text_render_color хочет FColor, а перевод через
# to_rgbe() это НЕ то преобразование (RGBE это формат хранения HDR, цвет получится не тот).
ROLE_COLOUR = {
    "HQ_A":    (86, 148, 255),
    "HQ_B":    (255, 96, 84),
    "MISSION": (255, 168, 62),
    "EXTRACT": (116, 236, 138),
    "NEUTRAL": (184, 191, 204),
}

LOG = []


def log(msg):
    LOG.append(msg)
    unreal.log("[MAPPROD] " + msg)


def level_disk_path(pkg):
    root = unreal.Paths.project_content_dir()
    return os.path.normpath(os.path.join(root, pkg.replace("/Game/", "") + ".umap"))


def open_level(les):
    """Тот же гард, что в SquadPlayground/build_playground.py, и по той же причине.

    new_level поверх открытого несохранённого мира убивает редактор насмерть ("World Memory
    Leaks"), а срабатывает это только со второго запуска: первый упал не сохранив, на диске пусто,
    does_asset_exist врёт False, и скрипт идёт создавать поверх того самого мира, который строит.
    """
    dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    foreign = [p for p in dirty if p.get_name() != LEVEL_PATH]
    if foreign:
        names = ", ".join(p.get_name() for p in foreign)
        raise RuntimeError("НЕСОХРАНЁННЫЕ карты ({}) - сохрани всё в редакторе".format(names))

    try:
        unreal.AssetRegistryHelpers.get_asset_registry().wait_for_completion()
    except Exception:
        pass

    current = les.get_current_level()
    current_name = current.get_outer().get_name() if current else ""
    if current_name.endswith(LEVEL_PATH.rsplit("/", 1)[-1]):
        log("уже стоим в {} - переиспользуем открытый мир".format(current_name))
        return

    if os.path.isfile(level_disk_path(LEVEL_PATH)) \
            or unreal.EditorAssetLibrary.does_asset_exist(LEVEL_PATH):
        if not les.load_level(LEVEL_PATH):
            raise RuntimeError("не удалось открыть " + LEVEL_PATH)
        log("открыт существующий уровень " + LEVEL_PATH)
    else:
        if not les.new_level(LEVEL_PATH):
            raise RuntimeError("не удалось создать " + LEVEL_PATH)
        log("создан уровень " + LEVEL_PATH)


def clear_tagged(eas):
    removed = 0
    for actor in list(eas.get_all_level_actors()):
        try:
            if TAG in [str(t) for t in actor.tags]:
                eas.destroy_actor(actor)
                removed += 1
        except Exception:
            pass
    if removed:
        log("снесено своих акторов: {}".format(removed))


def mark(actor, label):
    actor.tags = [TAG]
    actor.set_actor_label(label)
    return actor


def spawn_mesh(eas, mesh, loc, rot, scale, label):
    a = eas.spawn_actor_from_class(unreal.StaticMeshActor, loc, rot)
    comp = getattr(a, "static_mesh_component", None)
    if comp is None:
        comp = a.get_component_by_class(unreal.StaticMeshComponent)
    comp.set_static_mesh(mesh)
    a.set_actor_scale3d(scale)
    # set_collision_enabled НЕ переживает сохранение уровня, профиль переживает. Маркеры
    # декоративные: с коллизией они однажды сломают спавн, как уже ломали на стенде.
    comp.set_collision_profile_name("NoCollision")
    return mark(a, label)


def spawn_label(eas, text, loc, rgb, size, label):
    # pitch=90 кладёт текст лицом вверх: карту смотрят сверху, а вертикальная надпись с высоты
    # читается как полоска. Rotator зовётся по именам - позиционные аргументы это (roll,pitch,yaw),
    # и перепутать их молча очень легко (грабля из Docs/Gotchas/Python_Editor.md).
    a = eas.spawn_actor_from_class(unreal.TextRenderActor, loc,
                                   unreal.Rotator(roll=0.0, pitch=90.0, yaw=0.0))
    c = getattr(a, "text_render", None)
    if c is None:
        c = a.get_component_by_class(unreal.TextRenderComponent)
    c.set_text(text)
    c.set_text_render_color(unreal.Color(r=rgb[0], g=rgb[1], b=rgb[2], a=255))
    c.set_world_size(size)
    try:
        c.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
    except Exception:
        pass      # выравнивание это косметика, из-за неё ронять сборку незачем
    return mark(a, label)


def build(data, seed_index=0):
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    cyl = unreal.EditorAssetLibrary.load_asset(CYL)
    cube = unreal.EditorAssetLibrary.load_asset(CUBE)

    run = data["runs"][seed_index]
    roles = run["roles"]
    by_id = {s["id"]: s for s in data["slots"]}

    # --- слоты ---
    for s in data["slots"]:
        dia, hgt = TIER_SIZE[s["tier"]]
        role = roles.get(s["id"], "NEUTRAL")
        loc = unreal.Vector(s["x_m"] * M, s["y_m"] * M, hgt * M * 0.5)
        spawn_mesh(eas, cyl, loc, unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0),
                   unreal.Vector(dia, dia, hgt),
                   "SLOT_{}_{}".format(s["id"], role))

        text = s["id"] if role == "NEUTRAL" else "{}  {}".format(s["id"], role)
        spawn_label(eas, text,
                    unreal.Vector(s["x_m"] * M, s["y_m"] * M, (hgt + 14.0) * M),
                    ROLE_COLOUR[role], 5200.0,
                    "LABEL_" + s["id"])

    # --- коридоры ---
    # Плоскими плитами по земле: это не дороги, а читаемость графа с высоты. Без них в воздухе
    # видно рассыпанные столбы и непонятно, что с чем связано.
    for a_id, b_id in data["lanes"]:
        a, b = by_id[a_id], by_id[b_id]
        dx, dy = b["x_m"] - a["x_m"], b["y_m"] - a["y_m"]
        length = math.hypot(dx, dy)
        yaw = math.degrees(math.atan2(dy, dx))
        mid = unreal.Vector((a["x_m"] + dx * 0.5) * M, (a["y_m"] + dy * 0.5) * M, 30.0)
        spawn_mesh(eas, cube, mid, unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw),
                   unreal.Vector(length, 7.0, 0.4),
                   "LANE_{}_{}".format(a_id, b_id))

    # --- границы ---
    # Внутренняя рамка это игровое пространство, внешняя - ландшафт вместе с пляжами. Разницу
    # между ними иначе никак не увидеть, а именно она отвечает на "сколько тут по краям лишнего".
    w = data["world"]
    for side_m, thick, z, name in ((w["play_m"], 5.0, 60.0, "PLAY"),
                                   (w["land_m"], 9.0, 20.0, "LAND")):
        half = side_m * 0.5
        for sx, sy, sw, sh in ((0, -half, side_m, thick), (0, half, side_m, thick),
                               (-half, 0, thick, side_m), (half, 0, thick, side_m)):
            spawn_mesh(eas, cube, unreal.Vector(sx * M, sy * M, z),
                       unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0),
                       unreal.Vector(sw, sh, 0.6),
                       "EDGE_{}_{:.0f}_{:.0f}".format(name, sx, sy))

    log("поставлено: {} слотов, {} коридоров, 8 отрезков границы"
        .format(len(data["slots"]), len(data["lanes"])))
    log("забег seed {}".format(run["seed"]))
    log("игровое {:.0f} м, ландшафт {:.0f} м, пляжи {:.0f} м"
        .format(w["play_m"], w["land_m"], w["beach_m"]))


def main(seed_index=0):
    del LOG[:]
    with open(DATA, encoding="utf-8") as f:
        data = json.load(f)

    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    open_level(les)

    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    clear_tagged(eas)
    build(data, seed_index)

    les.save_current_level()
    log("уровень сохранён")
    return "\n".join(LOG)
