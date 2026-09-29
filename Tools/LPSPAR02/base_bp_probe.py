import unreal

# What the base weapon Blueprint is actually called and how it loads.

FOLDER = '/Game/Variant_Shooter/Blueprints/Weapons'
print('LIST', [str(p) for p in unreal.EditorAssetLibrary.list_assets(FOLDER, recursive=False)][:20])
for path in ('/Game/Variant_Shooter/Blueprints/Weapons/BP_ShooterWeaponBase',
             '/Game/Variant_Shooter/Blueprints/Weapons/BP_ShooterWeaponBase.BP_ShooterWeaponBase'):
    print('EXISTS', path, unreal.EditorAssetLibrary.does_asset_exist(path))
    asset = unreal.EditorAssetLibrary.load_asset(path)
    print('LOAD', path, asset, asset.get_class().get_name() if asset else None)
print('DONE')
