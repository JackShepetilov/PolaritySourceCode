import unreal
ROOT = "/Game/Variant_Shooter/Blueprints/Pickups/Attachments/Tactical/DA_Attach_Tactical_"
PD = "/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Weapons/_Common/Attachments/Models/"
dirty = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
mine = [d for d in dirty if "Tactical" in d]
if mine:
    print("DIRTY, not touching:", mine)
else:
    for n, m in (("Flashlight", "SM_ATT_Laser_Flashlight_01"), ("Shield", "SM_ATT_Laser_Flashlight_01"),
                 ("Freeze", "SM_ATT_Laser_Flashlight_01"), ("Laser", "SM_ATT_Laser_Sight_01")):
        a = unreal.load_asset(ROOT + n)
        a.set_editor_property("mesh", unreal.load_asset(PD + m))
        unreal.EditorAssetLibrary.save_asset(ROOT + n)
        print("MODIFIED:", n, a.get_editor_property("mesh").get_name())
