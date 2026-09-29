import unreal
p='/Game/InfimaGames/LowPolyShooterPack/Core/Characters/BP_LPSP_PCH'
for c in ['Full Body Character Mesh','Root','Socket Hand Right','Inventory Component']:
    for prop in ['RelativeRotation','RelativeLocation','AttachSocketName','SkeletalMeshAsset']:
        print(c,prop,unreal.BlueprintService.get_component_property(p,c,prop))
b=unreal.load_asset('/Game/InfimaGames/LowPolyShooterPack/Usable/Weapons/BP_LPSP_WEP_AR_02')
print('PARENT',b.get_editor_property('parent_class'))
