import unreal

# Pistol step 2: the data half of the kit on the production weapon.
#   - backup copy of the asset first,
#   - TP mesh -> the pack's handgun mesh (the first person mesh stays Kinemation),
#   - TP animations and body montages filled from the pack, project copies where the pack's slots are wrong.
# First person stack is untouched.

assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), 'PIE is running'

WEP = '/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation/Pistols/BP_M1911'
BASE = '/Game/Variant_Shooter/Tests/LPSP_Handgun_03'
BACKUP = BASE + '/BP_M1911_BeforeTPKit'
ANIM = '/Game/InfimaGames/AnimatedLowPolyWeapons/Art/'
WANIM = ANIM + 'Weapons/Handguns/Animations/'
CANIM = ANIM + 'Characters/Animations/Handguns/'
SK = ANIM + 'Weapons/Handguns/SK_Handgun_03'

if not unreal.EditorAssetLibrary.does_asset_exist(BACKUP):
    assert unreal.EditorAssetLibrary.duplicate_asset(WEP, BACKUP), BACKUP
    print('BACKUP', BACKUP)

FIELDS = {
    'bThirdPersonWeaponPoseFromAnimation': 'True',
    'WeaponMeshFireAnimationTP': WANIM + 'AM_TP_WEP_Handgun_03_Fire',
    'WeaponMeshReloadAnimationTP': WANIM + 'AM_TP_WEP_Handgun_03_Reload_Empty',
    'WeaponMeshSecondaryReloadAnimationTP': WANIM + 'AM_TP_WEP_Handgun_03_Reload',
    'ReloadMontageTP': BASE + '/AM_HG03_TP_Reload_Empty',
    'SecondaryReloadMontageTP': CANIM + 'AM_TP_CH_Handgun_Reload',
    'DrawMontageTP': BASE + '/AM_HG03_TP_Unholster',
    'HolsterMontageTP': BASE + '/AM_HG03_TP_Holster',
}
for name, value in FIELDS.items():
    print('SET', name, unreal.BlueprintService.set_property(WEP, name, value))

print('SET_MESH_COMPONENT', unreal.BlueprintService.set_component_property(
    WEP, 'Third Person Mesh', 'SkeletalMeshAsset', SK))

blueprint = unreal.load_asset(WEP)
unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
assert unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False), 'save failed'

cls = unreal.EditorAssetLibrary.load_blueprint_class(WEP)
cdo = unreal.get_default_object(cls)
for name in FIELDS:
    print('VERIFY', name, cdo.get_editor_property(name))
for comp in cdo.get_components_by_class(unreal.SkeletalMeshComponent):
    print('VERIFY_MESH', comp.get_name(), comp.get_editor_property('skeletal_mesh_asset'))
print('STATUS', blueprint.get_editor_property('status'))
print('DONE')
