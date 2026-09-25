import unreal

# Puts the existing upgrade definitions into the dispenser pool and gives each one its slot
# (Docs/Dispenser_Upgrade_SlotMachine_Spec_2026-09-25.md). The mapping below is a PROPOSAL for the
# author to correct: one upgrade per slot, a new one for a taken slot replaces the old, None is a
# passive that takes no slot. Run from the editor (execute_python_code or Tools/mcp.sh py).
# Idempotent: it only writes the four dispenser fields and saves the assets it touched.

ROOT = "/Game/Variant_Shooter/Blueprints/Upgrades/"
S = unreal.UpgradeSlot

# asset: (slot, min_wave, offer_weight)
POOL = {
    "Boring/DA_Upgrade_AirDash": (S.JUMP, 0, 1.0),
    "WeaponUpgrades/DA_SwordSlide": (S.SLIDE, 0, 1.0),
    "Fun/DA_Upgrade_DropKick": (S.MELEE, 0, 1.0),
    "Fun/DA_UpgradeDefinition_ChargedPunch": (S.MELEE, 0, 1.0),
    "Fun/DA_UpgradeDefinition_Backstab": (S.MELEE, 0, 1.0),
    "Fun/DA_UpgradeDefinition_Combo": (S.MELEE, 0, 1.0),
    "WeaponUpgrades/DA_MeleeCharge": (S.MELEE, 0, 1.0),
    "Boring/DA_MaxHealth": (S.NONE, 0, 1.0),
    "Boring/DA_FullHealthBonus": (S.NONE, 0, 1.0),
    "Boring/DA_LowHealthDefense": (S.NONE, 0, 1.0),
    "Boring/DA_VerticalCooling": (S.NONE, 0, 1.0),
    "Boring/DA_DropChargeOverride": (S.NONE, 0, 1.0),
    "Fun/DA_PlotArmor": (S.NONE, 0, 1.0),
    "Fun/DA_TestosteroneBoost": (S.NONE, 0, 1.0),
    "Fun/DA_HealthBlast": (S.NONE, 0, 1.0),
    "Fun/DA_Bandolier": (S.NONE, 0, 1.0),
    "Fun/DA_UD_360Shot": (S.NONE, 0, 1.0),
    "Fun/DA_AirMail": (S.NONE, 0, 1.0),
    "Fun/DA_UD_ChargeFlip": (S.NONE, 0, 1.0),
    "Fun/DA_UpgradeDefinition_Microstun": (S.NONE, 0, 1.0),
    "Fun/DA_UpgradeDefinition_TractorBeam": (S.NONE, 0, 1.0),
    "WeaponUpgrades/DA_RocketSwap": (S.NONE, 0, 1.0),
    "WeaponUpgrades/DA_SMG_AmmoRefund": (S.NONE, 0, 1.0),
}

for rel, (slot, min_wave, weight) in POOL.items():
    path = ROOT + rel
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if not asset:
        print("MISSING:", path)
        continue
    before = (asset.get_editor_property("b_in_dispenser_pool"), asset.get_editor_property("slot"))
    asset.set_editor_property("b_in_dispenser_pool", True)
    asset.set_editor_property("slot", slot)
    asset.set_editor_property("min_wave", min_wave)
    asset.set_editor_property("offer_weight", weight)
    unreal.EditorAssetLibrary.save_loaded_asset(asset)
    print("MODIFIED:", path, before, "->", (True, slot, min_wave, weight),
          "max level", asset.get_editor_property("max_level"))
