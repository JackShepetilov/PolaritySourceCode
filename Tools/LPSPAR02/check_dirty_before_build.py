import unreal

assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), 'PIE must be stopped'
print('DIRTY_MAPS', [str(p) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()])
for path in ('/Game/Variant_Shooter/Blueprints/Pickups/BP_ShooterWeaponBase',
             '/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation/Pistols/BP_M1911'):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    print('SAVE_WEAPON', path, unreal.EditorAssetLibrary.save_loaded_asset(asset, False))
