import unreal

# With DefaultSlot in the shared graph, the pack's own montages can be used directly: point the weapons
# at them and delete the project copies made earlier by the per-weapon workaround.

assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), 'PIE is running'

CH = '/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Characters/Animations/'
HANDFUN = CH + 'Handguns/'
AR = CH + 'ARs/'
PISTOL = '/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation/Pistols/BP_M1911'
RIFLE = '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_AR02_Integration_Test'

PLAN = {
    PISTOL: {
        'FiringMontageTP': HANDFUN + 'AM_TP_CH_Handgun_Fire',
        'ReloadMontageTP': HANDFUN + 'AM_TP_CH_Handgun_Reload_Empty',
        'DrawMontageTP': HANDFUN + 'AM_TP_CH_Handgun_Unholster',
        'HolsterMontageTP': HANDFUN + 'AM_TP_CH_Handgun_Holster',
    },
    RIFLE: {
        'ReloadMontageTP': AR + 'AM_TP_CH_AR_02_Reload_Empty',
    },
}
COPIES = ('/Game/Variant_Shooter/Tests/LPSP_Handgun_03/AM_HG03_TP_Reload_Empty',
          '/Game/Variant_Shooter/Tests/LPSP_Handgun_03/AM_HG03_TP_Fire',
          '/Game/Variant_Shooter/Tests/LPSP_Handgun_03/AM_HG03_TP_Holster',
          '/Game/Variant_Shooter/Tests/LPSP_Handgun_03/AM_HG03_TP_Unholster',
          '/Game/Variant_Shooter/Tests/LPSP_AR02/AM_AR02_TP_Reload_Empty')

for weapon, fields in PLAN.items():
    cdo = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(weapon))
    for name, path in fields.items():
        asset = unreal.EditorAssetLibrary.load_asset(path)
        print('SET', weapon.split('/')[-1], name, path.split('/')[-1], bool(asset))
        assert asset, path
        cdo.set_editor_property(name, asset)
    blueprint = unreal.EditorAssetLibrary.load_asset(weapon)
    print('COMPILE', unreal.BlueprintEditorLibrary.compile_blueprint(blueprint))
    print('SAVE', unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False))

for copy in COPIES:
    if not unreal.EditorAssetLibrary.does_asset_exist(copy):
        print('COPY_ABSENT', copy)
        continue
    users = [str(u) for u in unreal.EditorAssetLibrary.find_package_referencers_for_asset(copy)]
    print('COPY', copy.split('/')[-1], 'referencers=', users)
    if users:
        print('COPY_KEPT', copy)
        continue
    print('COPY_DELETE', unreal.EditorAssetLibrary.delete_asset(copy))

print('DONE')
