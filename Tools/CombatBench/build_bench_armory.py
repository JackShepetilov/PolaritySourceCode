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
UP_X = -520.0          # правый край остаётся позади лучей к кнопкам комнат
TAG_ATT = "CombatBenchAttachments"
# Keep these off the test bench, including when the attachment table is regenerated.
EXCLUDED_BENCH_ATTACHMENTS = {"DA_Attach_Muzzle_Ricochet", "DA_Attach_Muzzle_Stagger"}
ATTACH_PICKUP = "/Game/Variant_Shooter/Blueprints/Pickups/Attachments/Scopes/Retarded/BP_ScopePickup"
ATT_Y, ATT_X, ATT_STEP = -1170.0, -240.0, 95.0   # стол обвесов за правым столом с оружием
UP_TEXT = 16.0
WEAPONS = "/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation"
TABLE_Y = 650.0        # левый стол, справа оставлен проход к гранатам
TABLE_RIGHT_Y = 850.0
TABLE_H = 90.0
STEP = 85.0            # шаг между стволами
X_FIRST = -525.0


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
    defs = [a.get_asset() for a in ar.get_assets_by_class(unreal.TopLevelAssetPath("/Script/Polarity", "WeaponAttachmentDefinition"), True)
            if str(a.asset_name) not in EXCLUDED_BENCH_ATTACHMENTS]
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
    """Стойка с гранатами рядом с аптечками: на каждый вид гранаты (все UGrenadeDefinition) одна
    ABenchGrenadeRack по три штуки. Взял, и через полторы секунды на стойке лежат новые, как стволы
    в оружейной [автор, 2026-10-15]. Берутся клавишей «взять», бросок на G, смена вида на H.
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
    count, step = 3, 90.0
    length = len(types) * step + 60.0
    gx, cy = -180.0, -320.0
    box("Grenade_Stand", gx, cy, 0.0, 60.0, length, 60.0, folder=f)
    hb["text"]("Grenade_Title", gx - 35.0, cy, 95.0, "GRENADES", size=16.0, yaw=180.0, folder=f, tag=tag)

    for i, t in enumerate(types):
        name = t.get_name().replace("DA_Grenade_", "")
        y = cy - (len(types) - 1) * step * 0.5 + i * step
        r = eas.spawn_actor_from_class(unreal.BenchGrenadeRack, unreal.Vector(gx, y, 78.0))
        hb["_tag"](r, tag, "GrenadeRack_" + name, f)
        r.set_editor_property("grenade_type", t)
        r.set_editor_property("count", count)
        if mesh_asset:
            r.set_editor_property("pickup_mesh", mesh_asset)
        log("ADDED: rack {} x{}".format(name, count))
    hb["_les"]().save_current_level()
    log("DONE: grenades, {} racks".format(len(types)))


# Стенка прогрессии продолжает стенку апгрейдов (Y = 1050, кончается на X = 195) до стены телепортов
# хаба (Hub_Wall, X = 630..670): в той же комнате, видно и стреляется со старта, бегать не надо.
# Первая версия 2026-10-16 стояла на X 190..1190 и прошла сквозь стену хаба: автор не смог ей пользоваться.
PROG_Y = 1050.0
PROG_X0, PROG_X1 = 205.0, 620.0
PROG_COLS_X = (310.0, 515.0)
PROG_ROWS_Z = (500.0, 390.0, 280.0, 170.0)
PROG_ACTIONS = (           # (столбец, действие, надпись в редакторе): слева владение стволом, справа уровень и гаджеты
    (0, "MASTERY_UP", "GUN MASTERY +1"), (0, "MASTERY_DOWN", "GUN MASTERY -1"),
    (0, "MASTERY_RESET", "GUN MASTERY 0"), (0, "MASTERY_MAX", "GUN MASTERY MAX"),
    (1, "RUN_LEVEL_UP", "RUN LEVEL +1"), (1, "RUN_LEVEL_RESET", "RUN LEVEL 0"),
    (1, "GADGET_KINDS_TWO", "GADGET KINDS 2"), (1, "GADGET_KINDS_THREE", "GADGET KINDS 3 (perk)"),
)
PROG_BUTTON_SCALE = 0.45   # кубик 45 см, крупнее кнопок апгрейдов: стрелять со старта, 11 м
PROG_TEXT = 18.0
# Мишень за спиной у старта, на открытом месте перед горками (Hill_Up начинается с X = -1495). Линия
# взгляда со старта на неё проходит между кнопками пресетов (Y 45..198 на X = -450).
FATIGUE_X, FATIGUE_Y = -1450.0, 450.0


def step_progress():
    """Стенка прогрессии и мишень усталости [автор, 2026-10-16].
    Кнопки ABenchProgressButton: выстрел меняет владение ствола в руках, общий уровень, виды гаджетов.
    Мишень: светлый щит с тёмной точкой; держать прицел на точке и смотреть, как он начинает гулять.
    Числа на экране: консоль polarity.debug.progress 1."""
    hb["guard"]()
    tag = "CombatBenchProgress"
    hb["TAG_GEO"] = tag
    hb["_clear"](tag)
    f = "CombatBench/Progress"
    eas = hb["_eas"]()

    hb["wall"]("Progress_Wall", "y", PROG_Y, PROG_X0, PROG_X1, 0.0, UP_WALL_H, t=40.0, folder=f)
    hb["text"]("Progress_Title", (PROG_X0 + PROG_X1) * 0.5, PROG_Y - 40.0, UP_WALL_H + 20.0,
               "PROGRESS" + chr(10) + "numbers: polarity.debug.progress 1", size=22.0, yaw=-90.0, folder=f, tag=tag)

    row_of_col = {}
    for col, action, title in PROG_ACTIONS:
        row = row_of_col.get(col, 0)
        row_of_col[col] = row + 1
        b = eas.spawn_actor_from_class(unreal.BenchProgressButton,
                                       unreal.Vector(PROG_COLS_X[col], PROG_Y - 40.0, PROG_ROWS_Z[row]),
                                       unreal.Rotator(roll=0.0, pitch=0.0, yaw=-90.0))
        hb["_tag"](b, tag, "Progress_" + action, f + "/Buttons")
        b.set_editor_property("action", getattr(unreal.BenchProgressAction, action))
        # Крупнее кнопки и надпись, видная и в редакторе (в игре её перепишет сама кнопка).
        for m in b.get_components_by_class(unreal.StaticMeshComponent):
            m.set_relative_scale3d(unreal.Vector(0.12, PROG_BUTTON_SCALE, PROG_BUTTON_SCALE))
        label = b.get_component_by_class(unreal.TextRenderComponent)
        label.set_editor_property("text", title)
        label.set_editor_property("world_size", PROG_TEXT)
        label.set_relative_location(unreal.Vector(8.0, 0.0, PROG_BUTTON_SCALE * 50.0 + 6.0), False, False)
        log("ADDED: progress button {}".format(action))

    # Мишень: щит 1.2 x 1.2 м лицом к старту (на +X), точка 8 см по центру, на высоте глаз.
    box("Fatigue_Board", FATIGUE_X, FATIGUE_Y, 100.0, 10.0, 120.0, 120.0, mat="fence", folder=f)
    box("Fatigue_Dot", FATIGUE_X + 8.0, FATIGUE_Y, 156.0, 4.0, 8.0, 8.0, mat="road", folder=f)
    hb["text"]("Fatigue_Title", FATIGUE_X + 10.0, FATIGUE_Y, 240.0, "ADS FATIGUE" + chr(10) + "hold the sight on the dot",
               size=18.0, yaw=0.0, folder=f, tag=tag)

    hb["_les"]().save_current_level()
    log("DONE: progress wall, {} buttons, fatigue target".format(len(PROG_ACTIONS)))


POSER_X, POSER_Y = -1250.0, -500.0     # за стартом, слева от оружейной; стреляет в -Y, мимо столов
POSER_NPC = "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_ShooterNPC"
POSER_FIRST = ("AKX203",)                # эти стволы идут первыми
# Только стволы с дублёром третьего лица [автор, 2026-10-17]: пилот AKX203 и всё из jobs.json.
POSER_JOBS = r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/TPDouble/jobs.json"
POSER_EXTRA = ("ARs/BP_AKX203",)


def _poser_guns():
    import json
    jobs = json.load(open(POSER_JOBS, encoding="utf-8"))
    paths = list(POSER_EXTRA) + [j["bp"] for j in jobs["guns"].values()]
    out = []
    for rel in paths:
        cls = unreal.EditorAssetLibrary.load_blueprint_class(WEAPONS + "/" + rel)
        if cls is None:
            raise RuntimeError("нет ствола " + rel)
        out.append((rel.split("/")[-1].replace("BP_", ""), cls))
    return out


def step_poser():
    """Солдат-манекен: по кругу показывает все анимации ствола от третьего лица (стоит, одиночные,
    очередь, перезарядка с патронами, пустая, убрать, достать), потом следующий ствол
    [автор, 2026-10-17]. Стволы только с дублёром (_poser_guns). Выстрел игрока в солдата листает
    на следующий ствол. Стоит боком к старту, стреляет в сторону, урона не получает."""
    hb["guard"]()
    tag = "CombatBenchPoser"
    hb["TAG_GEO"] = tag
    hb["_clear"](tag)
    f = "CombatBench/Poser"
    eas = hb["_eas"]()
    npc = unreal.EditorAssetLibrary.load_blueprint_class(POSER_NPC)
    if npc is None:
        raise RuntimeError("нет класса " + POSER_NPC)

    guns = _poser_guns()
    guns.sort(key=lambda g: (0 if g[0] in POSER_FIRST else 1, g[0]))
    p = eas.spawn_actor_from_class(unreal.BenchWeaponPoser, unreal.Vector(POSER_X, POSER_Y, 100.0),
                                   unreal.Rotator(roll=0.0, pitch=0.0, yaw=-90.0))
    hb["_tag"](p, tag, "WeaponPoser", f)
    p.set_editor_property("npc_class", npc)
    p.set_editor_property("weapons", [c for _, c in guns])
    label = p.get_component_by_class(unreal.TextRenderComponent)
    label.set_world_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0), False, False)   # читается со старта
    label.set_editor_property("world_size", 22.0)
    box("Poser_Pad", POSER_X, POSER_Y, 0.0, 160.0, 160.0, 4.0, mat="pad", folder=f)
    hb["_les"]().save_current_level()
    log("DONE: poser, {} guns, first {}".format(len(guns), guns[0][0] if guns else None))


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
        table_y = TABLE_Y if side > 0 else -TABLE_RIGHT_Y
        box("Armory_Table_" + key, cx, table_y, 0.0, length, 90.0, TABLE_H, folder=f)
        hb["text"]("Armory_Title_" + key, cx, table_y + side * 60.0, 230.0, "ARMORY", size=45.0,
                   yaw=-90.0 * side, folder=f, tag=TAG)

    for n, (name, cls) in enumerate(guns):
        side = 1 if n < per_side else -1
        i = n if n < per_side else n - per_side
        rack = eas.spawn_actor_from_class(unreal.BenchWeaponRack,
                                          unreal.Vector(X_FIRST + i * STEP, TABLE_Y if side > 0 else -TABLE_RIGHT_Y, TABLE_H + 25.0),
                                          unreal.Rotator(roll=0.0, pitch=0.0, yaw=-90.0 * side))
        hb["_tag"](rack, TAG, "Armory_" + name, f + "/Racks")
        rack.set_editor_property("weapon_class", cls)
        rack.set_editor_property("title", name)
        log("ADDED: {}".format(name))

    hb["_les"]().save_current_level()
    log("DONE: armory, {} guns".format(len(guns)))
