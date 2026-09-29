import unreal

# Where the base weapon Blueprint really lives.

hits = [str(path) for path in unreal.EditorAssetLibrary.list_assets('/Game/Variant_Shooter', recursive=True)
        if 'ShooterWeaponBase' in str(path) or 'ShooterWeaponBase' in str(path)]
print('ENGINE_HITS', hits[:10])
hits2 = [str(path) for path in unreal.EditorAssetLibrary.list_assets('/Game', recursive=True)
         if 'ShooterWeaponBase' in str(path)]
print('GAME_HITS', hits2[:10])
print('DONE')
