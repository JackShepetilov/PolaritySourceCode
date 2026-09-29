import json
import unreal

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
IN = unreal.Paths.project_saved_dir() + 'LPSP_AR02/hg03_temp_profile_before.json'
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
with open(IN, encoding='utf-8') as handle:
    before = json.load(handle)
bp = unreal.EditorAssetLibrary.load_asset(AB)
cdo = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(AB))
poses = unreal.EditorAssetLibrary.load_asset(before['Data Table Animation Poses'])
assert poses
settings = cdo.get_editor_property('Settings Animation')
settings.import_text(before['Settings Animation'])
cdo.set_editor_property('Data Table Animation Poses', poses)
cdo.set_editor_property('Settings Animation', settings)
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False)
print('RESTORED AR02 DEFAULTS', poses.get_name())
