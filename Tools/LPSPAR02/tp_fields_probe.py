import unreal

# Are the C++ TP fields and functions present in the RUNNING binary, or only in a Live Coding patch?
# Read-only. Names are taken verbatim from Source/Polarity/Variant_Shooter/Weapons/ShooterWeapon.h.

BP = '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_AR02_Integration_Test'
cdo = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(BP))
for name in ('bThirdPersonWeaponPoseFromAnimation', 'WeaponMeshFireAnimationTP', 'WeaponMeshReloadAnimationTP',
             'WeaponMeshSecondaryReloadAnimationTP', 'WeaponMeshReloadEndAnimationTP', 'FiringMontageTP',
             'ReloadMontageTP', 'SecondaryReloadMontageTP', 'ReloadEndMontageTP', 'ThirdPersonAimHoldTime'):
    try:
        print('PROP', name, '=', cdo.get_editor_property(name))
    except Exception as exc:
        print('PROP', name, 'ERR', exc)
for fn in ('get_third_person_shot_count', 'is_third_person_aiming_after_shot', 'get_third_person_reload_montage',
           'attach_third_person_weapon_mesh', 'get_bullet_count', 'is_reloading'):
    print('FN', fn, hasattr(cdo, fn))
print('DONE')
