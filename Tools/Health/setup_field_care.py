"""Подключение ХП и брони в редакторе (2026-10-11). Код: UFieldCareComponent, AStrippableArmor,
AMedkitPickup. Идемпотентно: каждый шаг сначала смотрит, что уже сделано.

    step_input()    IA_Medkit, клавиша в IMC_Weapons, поле MedkitAction у блюпринтов игрока
    step_enemies()  у врагов из лута убираются хилки и подбор брони, вешается StrippableArmor
    step_hud()      в WBP_Vitals полоска брони (ArmorBar) и число аптечек (MedkitText)

Запуск: g = {"__name__": "care"}; exec(compile(open(p, encoding="utf-8").read(), p, "exec"), g); g["step_input"]()
"""

import json
import unreal

EAL = unreal.EditorAssetLibrary
BT = "editor_toolset.toolsets.blueprint.BlueprintTools"

ACTIONS = "/Game/Variant_Shooter/Input/Actions"
IMC = "/Game/Variant_Shooter/Input/IMC_Weapons"
MEDKIT_KEY = "X"
PLAYER_BPS = (
    "/Game/Variant_Shooter/Blueprints/BP_ShooterCharacter1",
    "/Game/Variant_Shooter/Blueprints/BP_MeleeCharacter",
    "/Game/Variant_Shooter/Blueprints/BP_ShooterCharacterCheat",
    "/Game/Variant_Shooter/Blueprints/BP_SniperCharacter",
    "/Game/Variant_Shooter/Blueprints/BP_TankCharacter",
    "/Game/Variant_Shooter/Blueprints/BP_WizardCharacter",
    "/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterCharacter_AR02_Test",
)
# С кого снимается броня и сколько. Решение автора; пока только пехота.
ARMOR_FROM = {
    "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_ShooterNPC": 50.0,
}
# У кого чистим лут от хилок и старого подбора брони.
LOOT_BPS = (
    "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_ShooterNPC",
    "/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterNPC_AR02_Test",
    "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_KamikazeDrone",
    "/Game/Prototype/HomeBase/BP_KamikazeCarrierDrone",
)
VITALS = "/Game/Variant_Shooter/UI/Widgets/HUD/Registry/WBP_Vitals"
HUD_BAR = "WBP_HudBar_C"       # имя класса виджета, не путь: с путём add_component молча не добавляет


def log(msg):
    print("[CARE_SETUP] " + msg)


def _compile(path):
    ref = "{}.{}".format(path, path.split("/")[-1])
    res = unreal.ToolsetRegistry.execute_tool(BT, "compile_blueprint", json.dumps({"blueprint": {"refPath": ref}}))
    status = str(unreal.load_asset(path).get_editor_property("status"))
    return res.is_complete and not res.error, status


def step_input():
    action_path = ACTIONS + "/IA_Medkit"
    if not EAL.does_asset_exist(action_path):
        unreal.InputService.create_action("IA_Medkit", ACTIONS, "Boolean")
        log("CREATED: " + action_path)
    action = unreal.load_asset(action_path)

    imc = unreal.load_asset(IMC)
    maps = imc.get_editor_property("default_key_mappings").get_editor_property("mappings")
    have = [m for m in maps if m.get_editor_property("action") == action]
    if not have:
        unreal.InputService.add_key_mapping(IMC, action_path, MEDKIT_KEY)
        log("ADDED: {} -> IA_Medkit in IMC_Weapons".format(MEDKIT_KEY))
    EAL.save_asset(action_path, False)
    EAL.save_asset(IMC, False)

    for path in PLAYER_BPS:
        cls = EAL.load_blueprint_class(path)
        cdo = unreal.get_default_object(cls)
        changed = []
        if cdo.get_editor_property("medkit_action") != action:
            cdo.set_editor_property("medkit_action", action)
            changed.append("medkit_action")
        # 100 ХП, 100 брони, без регена [автор, 2026-10-11]. У тестового персонажа стояло 200.
        for prop, want in (("max_hp", 100.0), ("max_armor", 100.0), ("enable_regeneration", False)):
            if cdo.get_editor_property(prop) != want:
                cdo.set_editor_property(prop, want)
                changed.append(prop)
        if changed:
            ok, status = _compile(path)
            saved = EAL.save_asset(path, False)
            log("MODIFIED: {}: {} | compile {} {} | saved {}".format(path.split("/")[-1], ", ".join(changed), ok, status, saved))
        else:
            log("ok: {}".format(path.split("/")[-1]))


def _components(path):
    """(имя, объект-шаблон, handle) для компонентов, добавленных в блюпринте и унаследованных."""
    bp = unreal.load_asset(path)
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    lib = unreal.SubobjectDataBlueprintFunctionLibrary
    out = []
    handles = sds.k2_gather_subobject_data_for_blueprint(bp)
    for h in handles:
        data = sds.k2_find_subobject_data_from_handle(h)
        obj = lib.get_object(data)
        if obj:
            out.append((obj.get_name(), obj, h))
    return bp, sds, handles, out


def step_enemies():
    for path in LOOT_BPS:
        if not EAL.does_asset_exist(path):
            log("нет ассета: " + path)
            continue
        bp, sds, handles, comps = _components(path)
        changed = []
        for name, obj, _h in comps:
            if isinstance(obj, unreal.LootDropComponent):
                loot = list(obj.get_editor_property("loot"))
                keep = []
                for e in loot:
                    pc = e.get_editor_property("pickup_class")
                    is_heal = pc and unreal.MathLibrary.class_is_child_of(pc, unreal.HealthPickup)
                    is_armor = pc and unreal.MathLibrary.class_is_child_of(pc, unreal.ArmorPickup)
                    if is_heal or is_armor:
                        changed.append("-" + pc.get_name())
                    else:
                        keep.append(e)
                if len(keep) != len(loot):
                    obj.set_editor_property("loot", keep)

        amount = ARMOR_FROM.get(path)
        if amount is not None and not any(isinstance(o, unreal.StrippableArmorComponent) for _n, o, _h in comps):
            params = unreal.AddNewSubobjectParams()
            params.set_editor_property("parent_handle", handles[0])
            params.set_editor_property("new_class", unreal.StrippableArmorComponent)
            params.set_editor_property("blueprint_context", bp)
            new_handle, fail = sds.add_new_subobject(params)
            data = sds.k2_find_subobject_data_from_handle(new_handle)
            comp = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(data)
            if comp:
                comp.set_editor_property("armor_amount", amount)
                changed.append("+StrippableArmor {}".format(amount))
            else:
                log("FAILED to add StrippableArmor to {}: {}".format(path, fail))

        if changed:
            ok, status = _compile(path)
            saved = EAL.save_asset(path, False)
            log("MODIFIED: {}: {} | compile {} {} | saved {}".format(path.split("/")[-1], " ".join(changed), ok, status, saved))
        else:
            log("ok: {}".format(path.split("/")[-1]))


def _w(name):
    return unreal.load_object(None, "{}.{}:WidgetTree.{}".format(VITALS, VITALS.split("/")[-1], name))


def step_hud():
    # Цвет брони в палитре HUD. Сеется один раз: правку автора в Project Settings не затирает.
    tag = unreal.GameplayTag()
    tag.import_text('(TagName="Palette.HUD.Armor")')
    if "Armor" not in tag.export_text():
        unreal.GameplayTagService.add_tags(["Palette.HUD.Armor"], "HUD: armour bar")
        tag.import_text('(TagName="Palette.HUD.Armor")')
        log("ADDED: tag Palette.HUD.Armor")
    if not unreal.PolarityPalette.has_color(tag):
        unreal.PolarityPalette.set_color(tag, unreal.LinearColor(0.25, 0.62, 1.0, 1.0))
        log("ADDED: colour Palette.HUD.Armor")

    names = [h.get_editor_property("widget_name") for h in unreal.WidgetService.get_hierarchy(VITALS)]
    made = []
    if "ArmorBar" not in names:
        # Тонкая полоска брони по верхнему краю полоски здоровья, в том же оверлее BarRow.
        unreal.WidgetService.add_component(VITALS, "SizeBox", "ArmorSize", "BarRow", False)
        unreal.WidgetService.add_component(VITALS, HUD_BAR, "ArmorBar", "ArmorSize", True)
        made.append("ArmorBar")
    if "MedkitText" not in names:
        unreal.WidgetService.add_component(VITALS, "TextBlock", "MedkitText", "BarRow", True)
        made.append("MedkitText")

    size = _w("ArmorSize")
    if size:
        # Под полоской здоровья, отдельной полоской: на верхнем краю её было не видно (автор, 2026-10-11).
        size.set_height_override(9.0)
        size.slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
        size.slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_BOTTOM)
        size.set_editor_property("render_transform", unreal.WidgetTransform(translation=unreal.Vector2D(0.0, 12.0)))
    bar = _w("ArmorBar")
    if bar:
        tag = unreal.GameplayTag()
        tag.import_text('(TagName="Palette.HUD.Armor")')
        bar.set_editor_property("fill_color_tag", tag)
        bar.set_editor_property("lean", _w("HealthBar").get_editor_property("lean"))
    text = _w("MedkitText")
    if text:
        text.set_editor_property("text", unreal.Text("3"))
        text.slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_RIGHT)
        text.slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
        font = _w("Caption").get_editor_property("font")
        font.set_editor_property("size", 16)
        text.set_editor_property("font", font)
        text.set_editor_property("render_transform", unreal.WidgetTransform(translation=unreal.Vector2D(34.0, 0.0)))

    ok, status = _compile(VITALS)
    saved = EAL.save_asset(VITALS, False)
    log("HUD: made {} | ArmorBar {} | MedkitText {} | compile {} {} | saved {}".format(
        made, type(bar).__name__ if bar else None, type(text).__name__ if text else None, ok, status, saved))
