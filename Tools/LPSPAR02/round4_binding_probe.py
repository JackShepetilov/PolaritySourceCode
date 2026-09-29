import unreal
from pathlib import Path
lines=[]
for path in ['/Game/InfimaGames/LowPolyShooterPack/Core/Characters/ABP_LPSP_TP_PCH','/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test']:
    bp=unreal.load_asset(path)
    for n in unreal.ObjectIterator(unreal.AnimGraphNode_Base):
        if n.get_outermost()!=bp.get_outermost() or n.get_outer().get_name()!='AnimGraph':continue
        if n.get_class().get_name() not in ['AnimGraphNode_ModifyBone','AnimGraphNode_CopyBone','AnimGraphNode_BlendSpaceEvaluator','AnimGraphNode_LinkedAnimGraph']:continue
        lines.append(path+' '+n.get_name()+' '+str(n.get_node_title()))
        binding=n.get_editor_property('binding')
        if binding:
            try:
                for k,v in binding.get_editor_property('PropertyBindings').items():lines.append(str(k)+' '+v.export_text())
            except Exception as e:lines.append(str(e))
for path in ['/Game/InfimaGames/LowPolyShooterPack/Core/Characters/BP_LPSP_PCH','/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterCharacter_AR02_Test']:
    bp=unreal.load_asset(path)
    if bp:
        c=unreal.get_default_object(bp.generated_class())
        for m in c.get_components_by_class(unreal.SkeletalMeshComponent):lines.append(path+' '+m.get_name()+' '+str(m.get_relative_transform()))
Path(unreal.Paths.project_saved_dir(),'LPSP_AR02/round4_bindings.txt').write_text('\n'.join(lines),encoding='utf-8')
print('WROTE',len(lines))
