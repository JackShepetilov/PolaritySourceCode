import unreal

# Stock attachments: Docs/Stock_Attachment_Plan_2026-10-01.md.
# Run through run_step.py. Idempotent: numbers are written only when an asset is created, so the
# author's tuning in the editor is never overwritten. The old placeholder
# Stocks/DA_Attach_StockGeneralCommon is left alone.
#
# No mesh: the pack guns have their stocks built in and there is no stock part in the project.
# Without a mesh nothing is mounted on the gun, and the pickup keeps its Blueprint look.

ROOT = "/Game/Variant_Shooter/Blueprints/Pickups/Attachments/Stocks"
WEAPON_DIRS = ["/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation"]
SKIP = ("BP_RPG7",)
EAL = unreal.EditorAssetLibrary
BT = "editor_toolset.toolsets.blueprint.BlueprintTools"

# The author: ADS speed is the same on every stock.
ADS = 1.3

SPECS = (
    # name, label, swap, reload, hip spread
    # Labels are the author's own words for them, not invented names.
    ("FastReload", "Stock Fast Reload", 1.0, 1.4, 1.0),
    ("FastSwap", "Stock Fast Swap", 1.6, 1.0, 1.0),
    ("LowSpread", "Stock Low Spread", 1.0, 1.0, 0.6),
    ("Stable", "Stock Stable", 1.2, 1.15, 0.85),
)


def tools():
    return unreal.AssetToolsHelpers.get_asset_tools()


def get_or_create_da(name):
    path = ROOT + "/" + name
    if EAL.does_asset_exist(path):
        return unreal.load_asset(path), False
    fac = unreal.DataAssetFactory()
    fac.set_editor_property("data_asset_class", unreal.WeaponAttachmentDefinition)
    a = tools().create_asset(name, ROOT, None, fac)
    print("CREATED:", path)
    return a, True


def step_assets():
    for key, label, swap, reload, spread in SPECS:
        att, new = get_or_create_da("DA_Attach_Stock_" + key)
        if new:
            att.set_editor_property("type", unreal.WeaponAttachmentType.STOCK)
            att.set_editor_property("display_name", label)
            att.set_editor_property("compatible_weapons", [unreal.ShooterWeapon])
            att.set_editor_property("swap_speed_multiplier", swap)
            att.set_editor_property("ads_speed_multiplier", ADS)
            att.set_editor_property("reload_speed_multiplier", reload)
            att.set_editor_property("hip_spread_multiplier", spread)
            EAL.save_loaded_asset(att)
        print("STOCK", att.get_name(),
              "swap", att.get_editor_property("swap_speed_multiplier"),
              "ads", att.get_editor_property("ads_speed_multiplier"),
              "reload", att.get_editor_property("reload_speed_multiplier"),
              "hip", att.get_editor_property("hip_spread_multiplier"))


def compile_bp(path):
    # Same call as setup_muzzle_assets.py, which worked on these 21 guns.
    import json
    res = unreal.ToolsetRegistry.execute_tool(BT, "compile_blueprint",
        json.dumps({"blueprint": {"refPath": path + "." + path.split("/")[-1]}}))
    return res.error if res.error else "ok"


def step_weapons():
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    done = 0
    for d in WEAPON_DIRS:
        for ad in ar.get_assets_by_path(d, recursive=True):
            if str(ad.asset_class_path.asset_name) != "Blueprint":
                continue
            name = str(ad.asset_name)
            if name in SKIP:
                continue
            path = str(ad.package_name)
            cls = EAL.load_blueprint_class(path)
            if not cls:
                continue
            cdo = unreal.get_default_object(cls)
            if not isinstance(cdo, unreal.ShooterWeapon) or isinstance(cdo, unreal.ShooterWeapon_Melee):
                continue
            slots = list(cdo.get_editor_property("attachment_slots"))
            if unreal.WeaponAttachmentType.STOCK in slots:
                print("HAS:", name)
                continue
            slots.append(unreal.WeaponAttachmentType.STOCK)
            cdo.set_editor_property("attachment_slots", slots)
            r = compile_bp(path)
            EAL.save_asset(path)
            print("MODIFIED:", name, "slots", len(slots), r)
            done += 1
    print("WEAPONS done", done)
