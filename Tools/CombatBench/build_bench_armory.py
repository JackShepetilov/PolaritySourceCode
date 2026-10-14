"""Оружейная у старта L_CombatBench (2026-10-11).

Два стола слева и справа от старта игрока. На каждом стволе стойка ABenchWeaponRack: ствол лежит,
его можно подобрать, через полторы секунды на стойке появляется такой же.

Стволы берутся все из папки WEAPONS (с подпапками). Новый ствол в папке: перезапустить шаг.
Свои акторы под тегом CombatBenchArmory, остальной стенд не трогает.
Запуск: g = {"__name__": "arm"}; exec(compile(open(p, encoding="utf-8").read(), p, "exec"), g); g["step_armory"]()
"""

import unreal

CBP = r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/CombatBench/build_combat_bench.py"
cb = {"__name__": "cb_lib"}
exec(compile(open(CBP, encoding="utf-8").read(), CBP, "exec"), cb)
hb = cb["hb"]
log, box = hb["log"], hb["box"]

TAG = "CombatBenchArmory"
TAG_UP = "CombatBenchUpgrades"
REGISTRY = "/Game/Variant_Shooter/Blueprints/Upgrades/DA_UpgradeRegistry"
UP_WALL_Y = 1050.0     # стенка апгрейдов за левым столом, лицом к старту
UP_COLS, UP_STEP = 7, 190.0
UP_ROWS_Z = (500.0, 415.0, 330.0, 245.0, 160.0)   # выше стола с оружием: со старта видно все ряды
UP_WALL_H = 590.0
UP_X = -165.0          # середина стенки. Правый край обязан кончаться до стенки телепортов (x 650): 2026-10-11 они пересеклись
TAG_ATT = "CombatBenchAttachments"
ATTACH_PICKUP = "/Game/Variant_Shooter/Blueprints/Pickups/Attachments/Scopes/Retarded/BP_ScopePickup"
ATT_Y, ATT_X, ATT_STEP = -1020.0, -80.0, 95.0   # стол обвесов за правым столом с оружием
UP_TEXT = 16.0
WEAPONS = "/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation"
TABLE_Y = 600.0        # столы по бокам от старта (старт в нуле, смотрит на +X)
TABLE_H = 90.0
STEP = 85.0            # шаг между стволами
X_FIRST = -400.0


def _weapon_classes():
    out = []
    for path in sorted(unreal.EditorAssetLibrary.list_assets(WEAPONS, recursive=True, include_folder=False)):
        cls = unreal.EditorAssetLibrary.load_blueprint_class(path.split(".")[0])
        if cls and unreal.MathLibrary.class_is_child_of(cls, unreal.ShooterWeapon):
            out.append((path.split("/")[-1].split(".")[0].replace("BP_", ""), cls))
        else:
            log("SKIP (не ствол): {}".format(path))
    return out


def step_upgrades():
    """Стенка апгрейдов: по кнопке ABenchUpgradeButton на каждый апгрейд из реестра. Выстрел в кнопку
    даёт апгрейд (или следующий уровень), выстрел на последнем уровне снимает."""
    hb["guard"]()
    hb["TAG_GEO"] = TAG_UP
    hb["_clear"](TAG_UP)
    f = "CombatBench/Upgrades"
    eas = hb["_eas"]()

    # Все ассеты апгрейдов, а не только реестр: в реестре 27 из 33 (нет Plot Armor, Extra Jump и других).
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    ups = [a.get_asset() for a in ar.get_assets_by_class(unreal.TopLevelAssetPath("/Script/Polarity", "UpgradeDefinition"), True)]
    ups.sort(key=lambda u: (str(u.get_editor_property("category")), str(u.get_editor_property("display_name"))))

    width = UP_COLS * UP_STEP + 100.0
    hb["wall"]("Upgrades_Wall", "y", UP_WALL_Y, UP_X - width * 0.5, UP_X + width * 0.5, 0.0, UP_WALL_H, t=40.0, folder=f)
    hb["text"]("Upgrades_Title", UP_X, UP_WALL_Y - 40.0, UP_WALL_H + 20.0, "UPGRADES: shoot to take, shoot again at the top level to drop",
               size=32.0, yaw=-90.0, folder=f, tag=TAG_UP)

    for n, up in enumerate(ups):
        row, col = divmod(n, UP_COLS)
        if row >= len(UP_ROWS_Z):
            log("SKIP (нет места на стенке): {}".format(up.get_name()))
            continue
        x = UP_X + (col - (UP_COLS - 1) * 0.5) * UP_STEP
        b = eas.spawn_actor_from_class(unreal.BenchUpgradeButton, unreal.Vector(x, UP_WALL_Y - 40.0, UP_ROWS_Z[row]),
                                       unreal.Rotator(roll=0.0, pitch=0.0, yaw=-90.0))
        hb["_tag"](b, TAG_UP, "Upgrade_" + up.get_name(), f + "/Buttons")
        b.set_editor_property("upgrade", up)
        b.get_component_by_class(unreal.TextRenderComponent).set_editor_property("world_size", UP_TEXT)
        log("ADDED: {}".format(up.get_editor_property("display_name")))

    hb["_les"]().save_current_level()
    log("DONE: upgrades, {} buttons".format(len(ups)))


def step_attachments():
    """Стол обвесов (апгрейды оружия): по одному AAttachmentPickup на каждый UWeaponAttachmentDefinition.
    Подбирается как в игре и уходит в сумку. Взятый обвес до конца сессии не возвращается."""
    hb["guard"]()
    hb["TAG_GEO"] = TAG_ATT
    hb["_clear"](TAG_ATT)
    f = "CombatBench/Attachments"
    eas = hb["_eas"]()
    cls = unreal.EditorAssetLibrary.load_blueprint_class(ATTACH_PICKUP)
    if cls is None:
        raise RuntimeError("нет класса " + ATTACH_PICKUP)

    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    defs = [a.get_asset() for a in ar.get_assets_by_class(unreal.TopLevelAssetPath("/Script/Polarity", "WeaponAttachmentDefinition"), True)]
    defs.sort(key=lambda d: (str(d.get_editor_property("type")), d.get_name()))
    per_row = (len(defs) + 1) // 2
    length = per_row * ATT_STEP + 80.0

    # Две ступени: дальний ряд выше ближнего, чтобы со старта было видно оба.
    box("Attach_TableNear", ATT_X, ATT_Y + 50.0, 0.0, length, 100.0, TABLE_H, folder=f)
    box("Attach_TableFar", ATT_X, ATT_Y - 50.0, 0.0, length, 100.0, TABLE_H + 90.0, folder=f)
    hb["text"]("Attach_Title", ATT_X, ATT_Y - 110.0, 330.0, "ATTACHMENTS: pick up, then mount in the inventory", size=32.0,
               yaw=90.0, folder=f, tag=TAG_ATT)

    for n, d in enumerate(defs):
        row, i = divmod(n, per_row)
        x = ATT_X + (i - (per_row - 1) * 0.5) * ATT_STEP
        y = ATT_Y + 50.0 - row * 100.0
        z = TABLE_H + 25.0 + row * 90.0
        p = eas.spawn_actor_from_class(cls, unreal.Vector(x, y, z), unreal.Rotator(roll=0.0, pitch=0.0, yaw=90.0))
        name = d.get_name().replace("DA_Attach_", "")
        hb["_tag"](p, TAG_ATT, "Attach_" + name, f + "/Pickups")
        p.set_editor_property("attachment", d)
        hb["text"]("Attach_Label_" + name, x, y, z + 28.0, name.replace("_", chr(10), 1), size=9.0, yaw=90.0,
                   folder=f + "/Labels", tag=TAG_ATT)
        log("ADDED: {}".format(name))

    hb["_les"]().save_current_level()
    log("DONE: attachments, {}".format(len(defs)))


def step_medkits():
    """Стойка с аптечками перед стартом: четыре AMedkitPickup по три аптечки. Берутся клавишей «взять»,
    уходят в сумку. Предел у игрока три (у медика шесть), лишнее остаётся лежать."""
    hb["guard"]()
    tag = "CombatBenchMedkits"
    hb["TAG_GEO"] = tag
    hb["_clear"](tag)
    f = "CombatBench/Medkits"
    eas = hb["_eas"]()
    cube = unreal.load_asset("/Engine/BasicShapes/Cube")
    box("Medkit_Stand", 260.0, 0.0, 0.0, 60.0, 340.0, 60.0, folder=f)
    hb["text"]("Medkit_Title", 225.0, 0.0, 95.0, "MEDKITS", size=16.0, yaw=180.0, folder=f, tag=tag)
    for i in range(4):
        p = eas.spawn_actor_from_class(unreal.MedkitPickup, unreal.Vector(260.0, -120.0 + i * 80.0, 78.0))
        hb["_tag"](p, tag, "Medkit_{}".format(i), f)
        p.set_editor_property("medkits", 3)
        mesh = p.get_component_by_class(unreal.StaticMeshComponent)
        if mesh:
            mesh.set_static_mesh(cube)
            mesh.set_relative_scale3d(unreal.Vector(0.22, 0.3, 0.16))
            mesh.set_visibility(True)
    hb["_les"]().save_current_level()
    log("DONE: medkits")


def step_grenades():
    """Стойка с гранатами рядом с аптечками: на каждый вид гранаты (все UGrenadeDefinition) по два
    AGrenadePickup по три штуки. Берутся клавишей «взять», уходят в сумку, бросок на G, смена вида на H.
    Docs/Grenades_Plan_2026-10-14.md"""
    hb["guard"]()
    tag = "CombatBenchGrenades"
    hb["TAG_GEO"] = tag
    hb["_clear"](tag)
    f = "CombatBench/Grenades"
    eas = hb["_eas"]()
    mesh_asset = unreal.load_asset("/Game/InfimaGames/ArtCore/Weapons/Grenades/Meshes/SM_IG_Grenade_Frag")

    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    types = [a.get_asset() for a in ar.get_assets_by_class(unreal.TopLevelAssetPath("/Script/Polarity", "GrenadeDefinition"), True)]
    types.sort(key=lambda t: t.get_name())
    per_type, count, step = 2, 3, 80.0
    n_slots = len(types) * per_type
    length = n_slots * step + 60.0
    cy = -220.0 - length * 0.5          # справа от стойки аптечек (она y -170..170)
    box("Grenade_Stand", 260.0, cy, 0.0, 60.0, length, 60.0, folder=f)
    hb["text"]("Grenade_Title", 225.0, cy, 95.0, "GRENADES", size=16.0, yaw=180.0, folder=f, tag=tag)

    slot = 0
    for t in types:
        name = t.get_name().replace("DA_Grenade_", "")
        for k in range(per_type):
            y = cy - (n_slots - 1) * step * 0.5 + slot * step
            p = eas.spawn_actor_from_class(unreal.GrenadePickup, unreal.Vector(260.0, y, 78.0))
            hb["_tag"](p, tag, "Grenade_{}_{}".format(name, k), f)
            p.set_editor_property("type", t)
            p.set_editor_property("count", count)
            mesh = p.get_component_by_class(unreal.StaticMeshComponent)
            if mesh and mesh_asset:
                mesh.set_static_mesh(mesh_asset)
                mesh.set_relative_scale3d(unreal.Vector(1.6, 1.6, 1.6))
                mesh.set_visibility(True)
            slot += 1
        # Одна подпись на вид, над его парой.
        y_mid = cy - (n_slots - 1) * step * 0.5 + (slot - per_type * 0.5 - 0.5) * step
        hb["text"]("Grenade_Label_" + name, 228.0, y_mid, 110.0, name.upper(), size=10.0, yaw=180.0,
                   folder=f + "/Labels", tag=tag)
        log("ADDED: {} x{} по {}".format(name, per_type, count))
    hb["_les"]().save_current_level()
    log("DONE: grenades, {} types".format(len(types)))


def step_armory():
    hb["guard"]()
    hb["TAG_GEO"] = TAG
    hb["_clear"](TAG)
    f = "CombatBench/Armory"
    eas = hb["_eas"]()

    guns = _weapon_classes()
    per_side = (len(guns) + 1) // 2
    length = (per_side - 1) * STEP + 160.0
    cx = X_FIRST + (per_side - 1) * STEP * 0.5

    for side in (1, -1):
        key = "L" if side > 0 else "R"
        box("Armory_Table_" + key, cx, side * TABLE_Y, 0.0, length, 90.0, TABLE_H, folder=f)
        hb["text"]("Armory_Title_" + key, cx, side * (TABLE_Y + 60.0), 230.0, "ARMORY", size=45.0,
                   yaw=-90.0 * side, folder=f, tag=TAG)

    for n, (name, cls) in enumerate(guns):
        side = 1 if n < per_side else -1
        i = n if n < per_side else n - per_side
        rack = eas.spawn_actor_from_class(unreal.BenchWeaponRack,
                                          unreal.Vector(X_FIRST + i * STEP, side * TABLE_Y, TABLE_H + 25.0),
                                          unreal.Rotator(roll=0.0, pitch=0.0, yaw=-90.0 * side))
        hb["_tag"](rack, TAG, "Armory_" + name, f + "/Racks")
        rack.set_editor_property("weapon_class", cls)
        rack.set_editor_property("title", name)
        log("ADDED: {}".format(name))

    hb["_les"]().save_current_level()
    log("DONE: armory, {} guns".format(len(guns)))
