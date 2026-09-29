import unreal,json
path='/Game/InfimaGames/LowPolyShooterPack/Core/Characters/ABP_LPSP_TP_PCH'
b=unreal.load_asset(path)
n=next(n for n in unreal.ObjectIterator(unreal.AnimGraphNode_ModifyBone) if n.get_outermost()==b.get_outermost() and n.get_outer().get_name()=='AnimGraph' and n.get_name()=='AnimGraphNode_ModifyBone_10')
binding=n.get_editor_property('binding')
r=unreal.ToolsetRegistry.execute_tool('editor_toolset.toolsets.object.ObjectTools','get_properties',json.dumps({'instance':{'refPath':binding.get_path_name()},'properties':['PropertyBindings']}))
print('BINDING TOOL',r.is_complete,r.error,r.get_value_as_json_string())
for path in ['/Game/InfimaGames/LowPolyShooterPack/Core/Characters/BP_LPSP_PCH','/Game/InfimaGames/LowPolyShooterPack/Usable/Weapons/BP_LPSP_WEP_AR_02']:
    print('COMPONENTS',path)
    for c in unreal.BlueprintService.get_component_hierarchy(path):print(c.export_text())
