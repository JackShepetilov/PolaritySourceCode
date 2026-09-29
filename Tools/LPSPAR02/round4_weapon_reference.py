import unreal
p='/Game/InfimaGames/LowPolyShooterPack/Usable/Weapons/BP_LPSP_WEP_AR_02'
print(unreal.BlueprintService.get_blueprint_info(p).export_text())
bp=unreal.load_asset(p)
cdo=unreal.get_default_object(bp.generated_class())
for name in ['get_class','get_super_class']:
    print(name,getattr(cdo.get_class(),name,None))
for a in unreal.ObjectIterator(unreal.SkeletalMeshComponent):
    if 'LPSP_WEP' in a.get_path_name() and 'UEDPIE_' not in a.get_path_name():
        print(a.get_path_name(),a.get_relative_transform(),a.get_editor_property('skeletal_mesh_asset'))
