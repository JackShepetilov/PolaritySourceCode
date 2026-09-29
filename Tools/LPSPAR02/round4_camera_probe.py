import unreal
for cls,ns in [(unreal.PlayerController,['set_view_target_with_blend']),
               (unreal.SkinnedMeshComponent,['set_owner_no_see','set_only_owner_see'])]:
    for n in ns:print(n,getattr(cls,n).__doc__)
print('NEW_OBJECT',unreal.new_object.__doc__)
for path in ['/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Characters/Animations/_Base/BS_ALPW_TP_Look',
             '/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Characters/Animations/ARs/BS_TP_CH_AR_02_Look']:
    b=unreal.load_asset(path)
    print(path,b.get_editor_property('blend_parameters'),b.get_editor_property('sample_data'))
