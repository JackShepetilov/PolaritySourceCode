import unreal

# Jump-slot upgrades (2026-09-26): extra jump, charged jump, air dash; the inventory's action-slot
# row; the crosshair's cooldown bar colours. Run once after the C++ that adds these classes is built:
#   Tools/mcp.sh py Source/Tools/Upgrades/setup_jump_slot.py
# Idempotent: creates what is missing, fills only what is empty, never recreates an asset.

UP = "/Game/Variant_Shooter/Blueprints/Upgrades/"
FOLDER = UP + "Boring"
POOL = UP + "DA_DispenserUpgradePool"
INV_SCREEN = "/Game/Variant_Shooter/UI/Widgets/HUD/Inventory/WBP_InventoryScreen"
CROSSHAIR = "/Game/Variant_Shooter/UI/Widgets/HUD/WBP_Crosshair"
CHARGE_SOUND = "/Game/SFX/Apex/Energy/Havoc_Turbocharger"
CHARGE_SHAKE = "/Game/Variant_Shooter/Blueprints/CameraShake/GroundCameraShake"
BT = "editor_toolset.toolsets.blueprint.BlueprintTools"


def tag(name):
    t = unreal.GameplayTag()
    t.import_text('(TagName="%s")' % name)
    return t


def q(v):
    return '"' + str(v).replace('"', "'") + '"'


def display(desc, rarity, stats):
    # Struct fields are EditDefaultsOnly: set_editor_property refuses them on an instance, the
    # text import does not.
    d = unreal.UpgradeLevelDisplay()
    rows = ",".join("(Label=%s,Value=%s)" % (q(l), q(v)) for l, v in stats)
    d.import_text("(Description=%s,Stats=(%s),Rarity=%s)" % (q(desc), rows, rarity))
    return d


def struct(cls, text_fields):
    s = cls()
    s.import_text("(" + text_fields + ")")
    return s


def get_or_create(name, cls):
    path = FOLDER + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.EditorAssetLibrary.load_asset(path), False
    fac = unreal.DataAssetFactory()
    fac.set_editor_property("data_asset_class", cls)
    a = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, FOLDER, None, fac)
    print("CREATED:", path)
    return a, True


def compile_bp(path):
    import json
    res = unreal.ToolsetRegistry.execute_tool(BT, "compile_blueprint", json.dumps({"blueprint": {"refPath": path + "." + path.split("/")[-1]}}))
    print("COMPILE", path, res.error if res.error else "ok", unreal.load_asset(path).get_editor_property("status"))


def main():
    # ---- tags ----
    try:
        unreal.GameplayTagService.add_tags(["Upgrade.ExtraJump", "Upgrade.ChargedJump"], "Jump-slot upgrades")
        print("TAGS added")
    except Exception as e:
        print("TAGS ERR", e)

    # ---- extra jump: Lv1 rare double, Lv2 legendary triple [author] ----
    ej, created = get_or_create("DA_Upgrade_ExtraJump", unreal.UpgradeDefinition_ExtraJump)
    if len(ej.get_editor_property("level_data")) == 0:
        ej.set_editor_property("upgrade_tag", tag("Upgrade.ExtraJump"))
        ej.set_editor_property("category", unreal.SkillCategory.BORING)
        ej.set_editor_property("display_name", "Extra Jump")
        ej.set_editor_property("description", "Jump again in the air.")
        ej.set_editor_property("component_class", unreal.Upgrade_ExtraJump)
        ej.set_editor_property("level_data", [
            struct(unreal.ExtraJumpLevelData, "ExtraJumps=1,Cooldown=3.0"),
            struct(unreal.ExtraJumpLevelData, "ExtraJumps=2,Cooldown=3.0")])
        ej.set_editor_property("level_displays", [
            display("Double jump.", "Rare", [("Air jumps", "1"), ("Cooldown", "3s")]),
            display("Triple jump.", "Legendary", [("Air jumps", "2"), ("Cooldown", "3s")])])
        ej.set_editor_property("max_level", 2)
        unreal.EditorAssetLibrary.save_loaded_asset(ej)
        print("MODIFIED: DA_Upgrade_ExtraJump")

    # ---- charged jump: numbers from the Melee passive's smoke jump (MaxZ 2000, 0.55 s, +400) ----
    cj, created = get_or_create("DA_Upgrade_ChargedJump", unreal.UpgradeDefinition_ChargedJump)
    if len(cj.get_editor_property("level_data")) == 0:
        cj.set_editor_property("upgrade_tag", tag("Upgrade.ChargedJump"))
        cj.set_editor_property("category", unreal.SkillCategory.BORING)
        cj.set_editor_property("display_name", "Charged Jump")
        cj.set_editor_property("description", "Hold jump on the ground, release to jump higher.")
        cj.set_editor_property("component_class", unreal.Upgrade_ChargedJump)
        cj.set_editor_property("level_data", [
            struct(unreal.ChargedJumpLevelData, "MaxZVelocity=1600,ChargeTime=0.55,ForwardBoost=300,ChargeMoveScale=1.0,Cooldown=4.0"),
            struct(unreal.ChargedJumpLevelData, "MaxZVelocity=2000,ChargeTime=0.55,ForwardBoost=400,ChargeMoveScale=1.0,Cooldown=3.0")])
        cj.set_editor_property("level_displays", [
            display("Hold jump, release to launch.", "Common", [("Launch speed", "1600"), ("Cooldown", "4s")]),
            display("Hold jump, release to launch higher.", "Epic", [("Launch speed", "2000"), ("Cooldown", "3s")])])
        cj.set_editor_property("max_level", 2)
        snd = unreal.EditorAssetLibrary.load_asset(CHARGE_SOUND)
        if snd:
            cj.set_editor_property("charge_sound", snd)
        shake = unreal.EditorAssetLibrary.load_blueprint_class(CHARGE_SHAKE)
        if shake:
            cj.set_editor_property("charge_camera_shake", shake)
        unreal.EditorAssetLibrary.save_loaded_asset(cj)
        print("MODIFIED: DA_Upgrade_ChargedJump sound", snd, "shake", shake)

    # ---- air dash: had no level data and the bare parent tag "Upgrade" ----
    ad = unreal.EditorAssetLibrary.load_asset(FOLDER + "/DA_Upgrade_AirDash")
    if ad and len(ad.get_editor_property("level_data")) == 0:
        ad.set_editor_property("upgrade_tag", tag("Upgrade.AirDash"))
        ad.set_editor_property("level_data", [
            struct(unreal.AirDashLevelData, "MaxCharges=1,CooldownSeconds=1.5,ImpulseMultiplier=1.0"),
            struct(unreal.AirDashLevelData, "MaxCharges=2,CooldownSeconds=1.0,ImpulseMultiplier=1.0")])
        ad.set_editor_property("level_displays", [
            display("Press jump in the air to dash.", "Common", [("Dashes per flight", "1"), ("Cooldown", "1.5s")]),
            display("Press jump in the air to dash.", "Epic", [("Dashes per flight", "2"), ("Cooldown", "1s")])])
        ad.set_editor_property("max_level", 2)
        unreal.EditorAssetLibrary.save_loaded_asset(ad)
        print("MODIFIED: DA_Upgrade_AirDash")

    # ---- pool: the Jump slot holds all three ----
    pool = unreal.EditorAssetLibrary.load_asset(POOL)
    slots = pool.get_editor_property("slots")
    changed = False
    # Elements come out as copies: edit the copy and put it back by index.
    slots = list(slots)
    for i, s in enumerate(slots):
        if str(s.get_editor_property("display_name")) != "Jump":
            continue
        entries = list(s.get_editor_property("upgrades"))
        have = [e.get_editor_property("upgrade") for e in entries]
        for a in (ej, cj, ad):
            if a and a not in have:
                e = unreal.DispenserUpgradeEntry()
                e.set_editor_property("upgrade", a)
                e.set_editor_property("weight", 1.0)
                entries.append(e)
                changed = True
                print("ADDED to Jump slot:", a.get_name())
        s.set_editor_property("upgrades", entries)
        slots[i] = s
    if changed:
        pool.set_editor_property("slots", slots)
        unreal.EditorAssetLibrary.save_loaded_asset(pool)
        print("MODIFIED:", POOL)

    # ---- crosshair bar colours (palette tags) ----
    cls = unreal.EditorAssetLibrary.load_blueprint_class(CROSSHAIR)
    cdo = unreal.get_default_object(cls)
    cur = cdo.get_editor_property("jump_cooldown_fill_color_tag").export_text()
    if "Palette." not in cur:
        cdo.set_editor_property("jump_cooldown_fill_color_tag", tag("Palette.HUD.Text"))
        cdo.set_editor_property("jump_cooldown_track_color_tag", tag("Palette.HUD.Track"))
        compile_bp(CROSSHAIR)
        unreal.EditorAssetLibrary.save_asset(CROSSHAIR)
        print("MODIFIED:", CROSSHAIR)

    # ---- inventory screen: a row for the action slots under the grid ----
    if not unreal.find_object(None, INV_SCREEN + ".WBP_InventoryScreen:WidgetTree.ActionSlots"):
        unreal.WidgetService.add_component(INV_SCREEN, "HorizontalBox", "ActionSlots", "CenterStack", True)
        w = unreal.load_object(None, INV_SCREEN + ".WBP_InventoryScreen:WidgetTree.ActionSlots")
        if w and w.slot:
            w.slot.set_padding(unreal.Margin(0.0, 16.0, 0.0, 0.0))
            w.slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_CENTER)
        compile_bp(INV_SCREEN)
        unreal.EditorAssetLibrary.save_asset(INV_SCREEN)
        print("MODIFIED:", INV_SCREEN)


main()
