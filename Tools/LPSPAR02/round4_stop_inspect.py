import unreal
r=unreal.ToolsetRegistry.execute_tool('EditorToolset.EditorAppToolset','StopPIE','{}')
print('STOP',r.is_complete,r.error)
bp=unreal.load_asset('/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test')
for n in unreal.ObjectIterator(unreal.EdGraphNode):
    if n.get_outermost()!=bp.get_outermost(): continue
    if n.get_name() in ['AnimGraphNode_ApplyMeshSpaceAdditive_0','AnimGraphNode_BlendSpaceEvaluator_0','AnimGraphNode_LayeredBoneBlend_1','K2Node_DynamicCast_0']:
        try: print(n.get_name(),n.get_editor_property('node').export_text())
        except Exception as e: print(str(e))
print('PLAY SETTINGS', [x for x in dir(unreal.LevelEditorPlaySettings) if 'client' in x or 'net_mode' in x])
