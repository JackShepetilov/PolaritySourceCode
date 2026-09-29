import unreal

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert world, 'PIE required'
subject = next(p for p in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.ShooterCharacter)
               if p.is_locally_controlled())
weapon = subject.get_current_weapon()
assert weapon and 'M1911' in weapon.get_name()
anim = subject.get_editor_property('mesh').get_anim_instance()
donor_path = '/Game/InfimaGames/LowPolyShooterPack/Usable/Weapons/BP_LPSP_WEP_Handgun_03'
donor_class = unreal.EditorAssetLibrary.load_blueprint_class(donor_path)
if not donor_class:
    donor_class = unreal.find_object(None, donor_path + '.BP_LPSP_WEP_Handgun_03_C')
assert donor_class, 'Donor class unavailable in PIE'
donor = unreal.get_default_object(donor_class)
poses_path = '/Game/InfimaGames/LowPolyShooterPack/Data/Weapons/Animations/DT_LPSP_CH_Handgun_03_Poses'
poses = unreal.EditorAssetLibrary.load_asset(poses_path)
if not poses:
    poses = unreal.find_object(None, poses_path + '.DT_LPSP_CH_Handgun_03_Poses')
assert poses
anim.set_editor_property('Actor Weapon', weapon)
anim.set_editor_property('Data Table Animation Poses', poses)
anim.set_editor_property('Settings Animation', donor.get_editor_property('Settings Animation'))
print('LIVE OVERRIDE', anim.get_editor_property('Actor Weapon').get_name(),
      anim.get_editor_property('Data Table Animation Poses').get_name())
