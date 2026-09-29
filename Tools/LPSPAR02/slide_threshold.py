import unreal, json

BS = unreal.BlueprintService
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
TOOLS = 'editor_toolset.toolsets.blueprint.BlueprintTools'
LOG = unreal.Paths.project_saved_dir() + 'LPSP_AR02/slide_threshold.log'
L = []


def log(m):
    L.append(str(m))
    print(m)


# The entry phase must not outlast the trimmed entry clip (0.50 s): set the compare to 0.45 s.
target_id = None
for n in BS.get_nodes_in_graph(AB, GRAPH):
    if 'float < float' in n.node_title:
        target_id = n.node_id
        break
log('LessThan node: %s' % target_id)
if target_id:
    log('set B = 0.45: %s' % BS.set_node_pin_value(AB, GRAPH, target_id, 'B', '0.45'))

r = unreal.ToolsetRegistry.execute_tool(TOOLS, 'compile_blueprint', json.dumps({'blueprint': {'refPath': AB + '.ABP_AR02_LPSP_Test'}}))
log('compile: %s %s' % (r.is_complete, r.error))
log('status: %s' % unreal.load_asset(AB).get_editor_property('status'))
log('saved: %s' % unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
open(LOG, 'w', encoding='utf-8').write('\n'.join(L))
