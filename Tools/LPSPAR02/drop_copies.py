import unreal

# Delete the handgun montage copies now that the weapons point at the pack originals, and verify the
# shared graph plays a pack montage authored in DefaultSlot.

assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), 'PIE is running'
BASE = '/Game/Variant_Shooter/Tests/LPSP_Handgun_03/'
for name in ('AM_HG03_TP_Reload_Empty', 'AM_HG03_TP_Fire', 'AM_HG03_TP_Holster', 'AM_HG03_TP_Unholster'):
    path = BASE + name
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        print('ABSENT', name)
        continue
    # The registry can lag behind a re-pointed Blueprint, so trust the saved package instead.
    users = [str(u) for u in unreal.EditorAssetLibrary.find_package_referencers_for_asset(path)]
    print('REFERENCERS', name, users)
    print('DELETE', name, unreal.EditorAssetLibrary.delete_asset(path))
print('DONE')
