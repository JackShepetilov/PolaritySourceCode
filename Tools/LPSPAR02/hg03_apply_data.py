import unreal

# Pistol (M1911 -> Handgun_03) data step, no animation copies.
#
# The pack's own TP body graph has exactly the slots Action Standing / Action Aiming / four arm slots;
# its handgun table lists the TP montages we point at here. Writes go through the class CDO with plain
# set_editor_property (the BlueprintService.set_property path died with an access violation last time),
# and every step prints before it runs so a failure is attributable.

assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), 'PIE is running'

WEP = '/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation/Pistols/BP_M1911'
BASE = '/Game/Variant_Shooter/Tests/LPSP_Handgun_03'
BACKUP = BASE + '/BP_M1911_BeforeTPKit'
ANIM = '/Game/InfimaGames/AnimatedLowPolyWeapons/Art/'
WANIM = ANIM + 'Weapons/Handguns/Animations/'
CANIM = ANIM + 'Characters/Animations/Handguns/'
SK = ANIM + 'Weapons/Handguns/SK_Handgun_03'
COPIES = [BASE + '/AM_HG03_TP_Reload_Empty', BASE + '/AM_HG03_TP_Holster', BASE + '/AM_HG03_TP_Unholster']

for copy in COPIES:
    if not unreal.EditorAssetLibrary.does_asset_exist(copy):
        continue
    users = unreal.EditorAssetLibrary.find_package_referencers_for_asset(copy)
    print('COPY', copy, 'referencers=', [str(u) for u in users])
    if users:
        print('COPY_KEPT', copy)
        continue
    print('COPY_DELETE', copy, unreal.EditorAssetLibrary.delete_asset(copy))

if not unreal.EditorAssetLibrary.does_asset_exist(BACKUP):
    print('BACKUP', unreal.EditorAssetLibrary.duplicate_asset(WEP, BACKUP))

OBJECT_FIELDS = {
    'WeaponMeshFireAnimationTP': WANIM + 'AM_TP_WEP_Handgun_03_Fire',
    'WeaponMeshReloadAnimationTP': WANIM + 'AM_TP_WEP_Handgun_03_Reload_Empty',
    'WeaponMeshSecondaryReloadAnimationTP': WANIM + 'AM_TP_WEP_Handgun_03_Reload',
    'ReloadMontageTP': CANIM + 'AM_TP_CH_Handgun_Reload',
    'SecondaryReloadMontageTP': CANIM + 'AM_TP_CH_Handgun_Reload',
    'DrawMontageTP': CANIM + 'AM_TP_CH_Handgun_Unholster',
    'HolsterMontageTP': CANIM + 'AM_TP_CH_Handgun_Holster',
}
BOOL_FIELDS = {'bThirdPersonWeaponPoseFromAnimation': True}

cls = unreal.EditorAssetLibrary.load_blueprint_class(WEP)
cdo = unreal.get_default_object(cls)
print('READY', bool(cls), bool(cdo))

for name, path in OBJECT_FIELDS.items():
    asset = unreal.EditorAssetLibrary.load_asset(path)
    print('WRITE', name, path, bool(asset))
    if asset:
        cdo.set_editor_property(name, asset)

for name, value in BOOL_FIELDS.items():
    print('WRITE', name, value)
    cdo.set_editor_property(name, value)

for component in cdo.get_components_by_class(unreal.SkeletalMeshComponent):
    if component.get_name() != 'Third Person Mesh':
        continue
    print('WRITE_MESH', SK)
    component.set_editor_property('skeletal_mesh_asset', unreal.EditorAssetLibrary.load_asset(SK))

blueprint = unreal.load_asset(WEP)
print('COMPILE', bool(unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)))
print('SAVE', unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False))

cdo = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(WEP))
for name in list(OBJECT_FIELDS) + list(BOOL_FIELDS):
    print('VERIFY', name, cdo.get_editor_property(name))
print('VERIFY_MESH', [(c.get_name(), str(c.get_editor_property('skeletal_mesh_asset')))
                      for c in cdo.get_components_by_class(unreal.SkeletalMeshComponent)])
print('DONE')
