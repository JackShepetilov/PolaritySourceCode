import json
import unreal

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
DONOR = '/Game/InfimaGames/LowPolyShooterPack/Usable/Weapons/BP_LPSP_WEP_Handgun_03'
POSES = '/Game/InfimaGames/LowPolyShooterPack/Data/Weapons/Animations/DT_LPSP_CH_Handgun_03_Poses'
OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/hg03_temp_profile_before.json'
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
dirty = [str(p) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
assert not any('ABP_AR02_LPSP_Test' in p for p in dirty), 'Adapter has unsaved edits'
bp = unreal.EditorAssetLibrary.load_asset(AB)
cdo = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(AB))
donor = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(DONOR))
poses = unreal.EditorAssetLibrary.load_asset(POSES)
assert poses
before = {
    'Data Table Animation Poses': cdo.get_editor_property('Data Table Animation Poses').get_path_name(),
    'Settings Animation': cdo.get_editor_property('Settings Animation').export_text(),
}
with open(OUT, 'w', encoding='utf-8') as handle:
    json.dump(before, handle, indent=2)
cdo.set_editor_property('Data Table Animation Poses', poses)
cdo.set_editor_property('Settings Animation', donor.get_editor_property('Settings Animation'))
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
print('TEMP PROFILE SAVED', AB, poses.get_name(), OUT)
