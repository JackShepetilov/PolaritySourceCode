import unreal, json

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
TOOLS = 'editor_toolset.toolsets.blueprint.BlueprintTools'
L = []


def log(m):
    L.append(str(m))
    print(m)


es = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
log('game world: %s' % (es.get_game_world() is not None))
log('dirty: %s' % [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()])

# verify the fix is present in memory
pkg = unreal.load_asset(AB).get_outermost()


def find(name):
    for n in unreal.ObjectIterator(unreal.EdGraphNode):
        if n.get_outermost() == pkg and n.get_outer() and n.get_outer().get_name() == GRAPH and n.get_name() == name:
            return n
    return None


og = find('AnimGraphNode_BlendListByBool_5')
if og:
    for p in og.list_all_pins():
        if str(p.get_pin_name()) == 'BlendPose_1':
            log('OuterGate.BlendPose_1 <- %s' % [q.get_owning_node().get_name() for q in p.list_connected_pins()])

r = unreal.ToolsetRegistry.execute_tool(TOOLS, 'compile_blueprint', json.dumps({'blueprint': {'refPath': AB + '.ABP_AR02_LPSP_Test'}}))
log('compile: %s %s' % (r.is_complete, r.error))
log('save: %s' % unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
log('dirty after: %s' % [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()])
open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/save_again.log', 'w', encoding='utf-8').write('\n'.join(L))
