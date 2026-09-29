import unreal, json

BS = unreal.BlueprintService
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
TOOLS = 'editor_toolset.toolsets.blueprint.BlueprintTools'
pkg = unreal.load_asset(AB).get_outermost()
JSON = unreal.Paths.project_saved_dir() + 'LPSP_AR02/slide_nodes.json'
W = json.load(open(JSON, encoding='utf-8'))

graph_obj = None
for n in unreal.ObjectIterator(unreal.EdGraphNode):
    if n.get_outermost() == pkg and n.get_outer().get_name() == GRAPH:
        graph_obj = n.get_outer()
        break


def names(substr):
    return {n.get_name() for n in unreal.ObjectIterator(unreal.EdGraphNode)
            if n.get_outermost() == pkg and n.get_outer() == graph_obj and substr in n.get_name()}


def find(name):
    for n in unreal.ObjectIterator(unreal.EdGraphNode):
        if n.get_outermost() == pkg and n.get_outer() and n.get_outer().get_name() == GRAPH and n.get_name() == name:
            return n
    return None


def get_pin(node, name, input_side):
    for p in node.list_all_pins():
        if str(p.get_pin_name()) == name and (p.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_INPUT) == input_side:
            return p
    return None


# 1) weight of the slide layer straight from the movement component
before = names('K2Node_CallFunction')
BS.create_node_by_key(AB, GRAPH, 'FUNC ApexMovementComponent::GetSlideAlpha', -1900.0, -2900.0)
new = names('K2Node_CallFunction') - before
print('created GetSlideAlpha:', sorted(new))
if len(new) == 1:
    node = find(sorted(new)[0])
    W['SlideAlphaFn'] = sorted(new)[0]
    apex = find(W['Apex'])
    ml = find(W['MaskLayer'])
    ok1 = get_pin(apex, 'ReturnValue', False).try_create_connection(get_pin(node, 'self', True))
    ok2 = get_pin(node, 'ReturnValue', False).try_create_connection(get_pin(ml, 'BlendWeights_0', True))
    print('weight wiring:', ok1, ok2)

# 2) blend times on the gates
for tag, times in (('PhaseGate', [0.2, 0.2]), ('HoldGate', [0.25, 0.25])):
    g = find(W[tag])
    gi = g.get_editor_property('node')
    try:
        gi.set_editor_property('blend_time', times)
        g.set_editor_property('node', gi)
        print('%s blend_time -> %s' % (tag, g.get_editor_property('node').get_editor_property('blend_time')))
    except Exception as e:
        print('%s blend time err: %s' % (tag, e))

# 3) entry threshold on the comparison: node_id by unique title
target_id = None
for n in BS.get_nodes_in_graph(AB, GRAPH):
    if 'float < float' in n.node_title:
        target_id = n.node_id
        break
print('LessThan node_id:', target_id)
if target_id:
    print('set B=0.75:', BS.set_node_pin_value(AB, GRAPH, target_id, 'B', '0.75'))

# 4) compile and save
json.dump(W, open(JSON, 'w', encoding='utf-8'), indent=2)
r = unreal.ToolsetRegistry.execute_tool(TOOLS, 'compile_blueprint', json.dumps({'blueprint': {'refPath': AB + '.ABP_AR02_LPSP_Test'}}))
print('compile:', r.is_complete, r.error)
print('status:', unreal.load_asset(AB).get_editor_property('status'))
print('saved:', unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
