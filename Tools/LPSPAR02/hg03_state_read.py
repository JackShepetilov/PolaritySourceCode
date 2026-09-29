import unreal

# Is Python alive after the access violation, and what does BP_M1911 hold right now. Read-only.

print('PING_OK')
WEP = '/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation/Pistols/BP_M1911'
FIELDS = ('bThirdPersonWeaponPoseFromAnimation', 'WeaponMeshFireAnimationTP', 'WeaponMeshReloadAnimationTP',
          'WeaponMeshSecondaryReloadAnimationTP', 'ReloadMontageTP', 'SecondaryReloadMontageTP',
          'DrawMontageTP', 'HolsterMontageTP')
print('PIE', bool(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()))
print('DIRTY', [str(p) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()][:6])
cls = unreal.EditorAssetLibrary.load_blueprint_class(WEP)
cdo = unreal.get_default_object(cls)
for name in FIELDS:
    try:
        print('V', name, '=', cdo.get_editor_property(name))
    except Exception as exc:
        print('V', name, 'ERR', exc)
for comp in cdo.get_components_by_class(unreal.SkeletalMeshComponent):
    print('V_MESH', comp.get_name(), comp.get_editor_property('skeletal_mesh_asset'))
print('BACKUP_EXISTS', unreal.EditorAssetLibrary.does_asset_exist(
    '/Game/Variant_Shooter/Tests/LPSP_Handgun_03/BP_M1911_BeforeTPKit'))
print('DONE')
