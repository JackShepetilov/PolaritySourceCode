"""Стенд фидбека попаданий: все виды врагов стоят в ряд, у каждого вида несколько вариантов живучести.

Зачем: проверять звук, хитмаркер, эффект попадания и полосы над головой по каждому виду врага и по
каждому состоянию (броня держит, броня сломана, тело), не дожидаясь нужного момента в бою.

Раскладка (игрок в нуле, смотрит на +X):
    X = 1000   наёмники на полу
    X = 1700   дроны на тумбах
    X = 2300   стена-задник

Варианты (подпись над каждым):
    NORMAL        всё как в игре, умирает (вернуть: перезапустить PIE)
    ARMOR INF     броня доливается через ~0.3 с после попадания, до здоровья не добраться
    HP INF        брони нет, здоровья 10 млн: попадания по телу без конца
    BREAK+REGEN   броня ломается и возвращается через 2 с, здоровья 10 млн: слом сколько угодно раз

У всех выключен ИИ (AutoPossessAI = Disabled): стоят и не стреляют.

Запуск (через Tools/mcp.sh, длинный код тул режет):
    import unreal
    p = r"C:/.../Source/Tools/DamagePlayground/build_damage_playground" + "." + "py"
    g = {"__name__": "dp"}; exec(compile(open(p, encoding="utf-8").read(), p, "exec"), g)
    g["main"]()
Идемпотентно: чистит и ставит заново только акторы со своими тегами. Свет даёт подуровень
ArenaLightingDebug3, как у L_HomeBase.
"""

import unreal

HB = r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/HomeBase/build_homebase.py"
hb = {"__name__": "hb_lib"}
exec(compile(open(HB, encoding="utf-8").read(), HB, "exec"), hb)

LEVEL = "/Game/Prototype/DamagePlayground/L_DamagePlayground"
LIGHT_LEVEL = "/Game/Variant_Shooter/Arenas/ArenaDebug/ArenaLightingDebug3"
TAG_ROOM = "DamagePlaygroundGen"
TAG_GAME = "DamagePlaygroundGame"
hb["LEVEL"] = LEVEL
hb["TAG_GEO"] = TAG_ROOM

AI = "/Game/Variant_Shooter/Blueprints/AI/BPs/"
INF_HP = 10000000.0

NORMAL, ARMOR_INF, HP_INF, BREAK_REGEN = "NORMAL", "ARMOR INF", "HP INF", "BREAK+REGEN"
ARMORED = (NORMAL, ARMOR_INF, HP_INF, BREAK_REGEN)
PLAIN = (NORMAL, HP_INF)

# (подпись, блюпринт, варианты)
MERCS = (
    ("GRUNT", AI + "BP_ShooterNPC", ARMORED),
)
DRONES = (
    ("KAMIKAZE", AI + "BP_KamikazeDrone", ARMORED),
    ("CARRIER", "/Game/Prototype/DroneTest/BP_KamikazeCarrierDrone_NoPayload", ARMORED),
    ("FLYING", AI + "BP_FlyingDrone", PLAIN),
)

MERC_X, DRONE_X, WALL_X = 1000.0, 1700.0, 2300.0
STEP_Y = 320.0
PEDESTAL_H = 180.0
FLOOR_HX, FLOOR_HY = 2600.0, 2400.0

log = hb["log"]


def _text(label, x, y, z, msg, size=34.0, tag=TAG_GAME, folder="DamagePlayground/Labels"):
    # Латиницей: у шрифта TextRender по умолчанию нет кириллицы.
    return hb["text"](label, x, y, z, msg, size=size, yaw=180.0, folder=folder, tag=tag)


def step_level():
    w = hb["_world"]()
    if w and w.get_path_name().startswith(LEVEL + "."):
        hb["_les"]().set_current_level_by_name(LEVEL.split("/")[-1])
    hb["step_level"]()


def step_light():
    hb["guard"]()
    world = hb["_world"]()
    names = [l.get_outer().get_path_name() for l in unreal.EditorLevelUtils.get_levels(world)]
    if any(n.startswith(LIGHT_LEVEL + ".") or n == LIGHT_LEVEL for n in names):
        log("свет-сублевел уже подключён")
    else:
        sl = unreal.EditorLevelUtils.add_level_to_world(world, LIGHT_LEVEL, unreal.LevelStreamingDynamic)
        log("ADDED: сублевел света {} ({})".format(LIGHT_LEVEL, sl))
    # Подключённый сублевел становится текущим: вернуть основной, иначе всё ляжет в свет.
    hb["_les"]().set_current_level_by_name(LEVEL.split("/")[-1])
    hb["guard"]()
    hb["_les"]().save_current_level()


def step_room():
    hb["guard"]()
    hb["_clear"](TAG_ROOM)
    hb["_count"]["n"] = 0
    del hb["_checks"][:]
    box = hb["box"]
    box("Floor", 800.0, 0.0, -50.0, 2.0 * FLOOR_HX, 2.0 * FLOOR_HY, 50.0, folder="DamagePlayground/Ground")
    box("Backstop", WALL_X, 0.0, 0.0, 60.0, 2.0 * FLOOR_HY, 600.0, mat="structure", folder="DamagePlayground/Ground")
    for dist in (500.0, 1000.0, 1500.0):
        box("Line_{}".format(int(dist)), dist - 300.0, 0.0, 0.0, 10.0, 2.0 * FLOOR_HY, 1.0, mat="road",
            folder="DamagePlayground/Ground", collision=False)
    if not hb["check_kit_centers"]():
        raise RuntimeError("угловой пивот кита посчитан неверно, уровень НЕ сохранён")
    hb["_les"]().save_current_level()
    log("ADDED: пол, задник, линии дистанции; {} мешей".format(hb["_count"]["n"]))


def _apply_variant(actor, variant):
    # Свойства АКТОРА первыми: каждая такая запись перезапускает construction script и пересоздаёт
    # компоненты блюпринта, так что ссылка на компонент, взятая раньше, указывает на труп, и правки
    # в него молча пропадают (2026-10-04: варианты брони не доехали до PIE). Компонент брать после.
    actor.set_editor_property("auto_possess_ai", unreal.AutoPossessAI.DISABLED)
    if isinstance(actor, unreal.KamikazeDroneNPC):
        # Без этого камикадзе сам идёт в атаку из зависания и взрывается о соседей по стенду.
        actor.set_editor_property("self_strike", False)
        actor.set_editor_property("retaliate_on_damage", False)
        # И сам улетает на точку зависания у игрока (HoldDistance / HoldHeight) и дрейфует там.
        # Нулевая скорость подлёта и нулевой дрейф оставляют его на тумбе. CustomTimeDilation для
        # заморозки не годится: AShooterNPC сам пишет его каждый кадр (замедления).
        for name in ("hold_speed", "hold_acceleration", "drift_radius", "drift_vertical"):
            actor.set_editor_property(name, 0.0)
    if isinstance(actor, unreal.KamikazeCarrierDrone):
        actor.set_editor_property("auto_deploy", False)
    if variant in (HP_INF, BREAK_REGEN):
        actor.set_editor_property("current_hp", INF_HP)
    if variant == HP_INF:
        # Профиль класса в BeginPlay возвращает броне размер ShieldCharge и перебивает ноль ниже.
        try:
            actor.set_editor_property("combat_profile", None)
        except Exception as e:
            log("   combat_profile не снят: {}".format(e))

    # И каждая запись в КОМПОНЕНТ тоже пересоздаёт его: брать заново перед каждой записью.
    def put(name, value):
        actor.get_component_by_class(unreal.ShieldFieldComponent).set_editor_property(name, value)

    shield = actor.get_component_by_class(unreal.ShieldFieldComponent)
    if variant == ARMOR_INF:
        put("regen_mode", unreal.ShieldRegenMode.TIMED)
        put("regen_delay", 0.1)
        put("regen_rate", 5000.0)
    elif variant == HP_INF and shield:
        put("max_shield", 0.0)
    elif variant == BREAK_REGEN:
        put("regen_mode", unreal.ShieldRegenMode.TIMED)
        put("regen_delay", 2.0)
        put("regen_rate", 200.0)
    if shield and variant != NORMAL:
        chk = actor.get_component_by_class(unreal.ShieldFieldComponent)
        log("   shield on {}: max {} mode {} delay {}".format(variant, chk.get_editor_property("max_shield"),
            chk.get_editor_property("regen_mode"), chk.get_editor_property("regen_delay")) + " rate {}".format(chk.get_editor_property("regen_rate")))


def _row(rows, x, z, pedestal):
    eas = hb["_eas"]()
    slots = [(name, bp, v) for name, bp, variants in rows for v in variants]
    y0 = -0.5 * STEP_Y * (len(slots) - 1)
    for i, (name, bp, variant) in enumerate(slots):
        y = y0 + i * STEP_Y
        cls = unreal.EditorAssetLibrary.load_blueprint_class(bp)
        if cls is None:
            raise RuntimeError("нет {}".format(bp))
        if pedestal:
            # Тумба получает тег комнаты (его ставит shape): step_room снесёт её при следующем прогоне.
            hb["box"]("Pedestal_{}_{}".format(name, i), x, y, 0.0, 120.0, 120.0, PEDESTAL_H, mat="cover",
                      folder="DamagePlayground/Pedestals")
        act = eas.spawn_actor_from_class(cls, unreal.Vector(x, y, z), unreal.Rotator(roll=0.0, pitch=0.0, yaw=180.0))
        hb["_tag"](act, TAG_GAME, "DP_{}_{}".format(name, variant.replace(" ", "").replace("+", "_")), "DamagePlayground/Targets")
        _apply_variant(act, variant)
        _text("Label_{}_{}".format(name, i), x, y, z + 190.0, "{}\n{}".format(name, variant))
        log("ADDED: {} {} at y={:.0f}".format(name, variant, y))


def step_game():
    hb["guard"]()
    hb["_clear"](TAG_GAME)
    eas = hb["_eas"]()
    ps = eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0.0, 0.0, 100.0),
                                    unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    hb["_tag"](ps, TAG_GAME, "DP_PlayerStart", "DamagePlayground/Gameplay")
    _row(MERCS, MERC_X, 100.0, pedestal=False)
    _row(DRONES, DRONE_X, PEDESTAL_H + 120.0, pedestal=True)
    hb["_les"]().save_current_level()


TAG_TANK = "DamagePlaygroundTank"
TANK_BP = AI + "BP_TrackedTank"
TANK_X = 1300.0
TANKS = ((-1700.0, NORMAL), (-2150.0, HP_INF))


def step_tank():
    """Танкетка сбоку от рядов, носом к игроку: корпус, гусеницы, оптика (зоны попаданий). Свой тег,
    чтобы не пересобирать остальной стенд. polarity.debug.hitzones 1 показывает коробки зон."""
    hb["guard"]()
    hb["_clear"](TAG_TANK)
    eas = hb["_eas"]()
    cls = unreal.EditorAssetLibrary.load_blueprint_class(TANK_BP)
    if cls is None:
        raise RuntimeError("нет {}".format(TANK_BP))
    for y, variant in TANKS:
        act = eas.spawn_actor_from_class(cls, unreal.Vector(TANK_X, y, 120.0), unreal.Rotator(roll=0.0, pitch=0.0, yaw=180.0))
        hb["_tag"](act, TAG_TANK, "DP_TANK_{}".format(variant.replace(" ", "")), "DamagePlayground/Targets")
        _apply_variant(act, variant)
        _text("Label_TANK_{}".format(variant.replace(" ", "")), TANK_X, y, 330.0, "TANK\n{}".format(variant), tag=TAG_TANK)
        log("ADDED: TANK {} at y={:.0f}".format(variant, y))
    hb["_les"]().save_current_level()


TAG_UPGRADES = "DamagePlaygroundUpgrades"
POOL = "/Game/Variant_Shooter/Blueprints/Upgrades/DA_DispenserUpgradePool"
UPGRADE_X, UPGRADE_ROW_X, UPGRADE_STEP_Y = -600.0, 300.0, 220.0


def step_upgrades():
    """Все улучшения из пула раздатчика лежат пикапами за спиной игрока: ряд на слот пула. Пикап тот
    же, что раздатчик показывает в ячейке (UpgradePickupClass пула), берётся клавишей притягивания.
    Взятый пикап пропадает до перезапуска PIE; снять улучшение: polarity.upgrade.remove <тег>."""
    hb["guard"]()
    hb["_clear"](TAG_UPGRADES)
    eas = hb["_eas"]()
    pool = unreal.load_asset(POOL)
    if pool is None:
        raise RuntimeError("нет {}".format(POOL))
    cls = pool.get_editor_property("upgrade_pickup_class") or unreal.UpgradePickup
    row = -1
    for slot in pool.get_editor_property("slots"):
        defs = [e.get_editor_property("upgrade") for e in slot.get_editor_property("upgrades")]
        defs = [d for d in defs if d]
        if not defs:
            continue
        row += 1
        x = UPGRADE_X - row * UPGRADE_ROW_X
        y0 = -0.5 * UPGRADE_STEP_Y * (len(defs) - 1)
        for i, d in enumerate(defs):
            y = y0 + i * UPGRADE_STEP_Y
            short = d.get_name().replace("DA_UpgradeDefinition_", "").replace("DA_Upgrade_", "").replace("DA_UD_", "").replace("DA_", "")
            act = eas.spawn_actor_from_class(cls, unreal.Vector(x, y, 110.0), unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
            hb["_tag"](act, TAG_UPGRADES, "DP_Upgrade_{}".format(short), "DamagePlayground/Upgrades")
            act.set_editor_property("upgrade_definition", d)
            hb["text"]("Label_Upgrade_{}".format(short), x, y, 230.0, short, size=22.0, yaw=0.0,
                       folder="DamagePlayground/Labels", tag=TAG_UPGRADES)
            log("ADDED: upgrade {} row {} at y={:.0f}".format(short, row, y))
    hb["_les"]().save_current_level()


def main():
    step_level()
    step_light()
    step_room()
    step_game()
    step_tank()
    step_upgrades()
    log("DONE: {}".format(LEVEL))
