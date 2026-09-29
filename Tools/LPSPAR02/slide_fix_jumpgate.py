import unreal, json

# The slide layer must not depend on the Jumping variable: a jump already forbids a slide in the
# movement component, while this graph's Jumping (= IsFalling) can be true for other reasons and was
# suppressing the whole layer. Route the masked layer straight into the outer gate.
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
TOOLS = 'editor_toolset.toolsets.blueprint.BlueprintTools'
pkg = unreal.load_asset(AB).get_outermost()
JSON = unreal.Paths.project_saved_dir() + 'LPSP_AR02/slide_nodes.json'
W = json.load(open(JSON, encoding='utf-8'))
L = []


def log(m):
    L.append(str(m))
    print(m)


def find(name):
    for n in unreal.ObjectIterator(unreal.EdGraphNode):
        if n.get_outermost() == pkg and n.get_outer() and n.get_outer().get_name() == GRAPH and n.get_name() == name:
            return n
    return None


def pin(node, name, input_side):
    for p in node.list_all_pins():
        if str(p.get_pin_name()) == name and (p.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_INPUT) == input_side:
            return p
    return None


ml = find(W['MaskLayer'])
og = find(W['OuterGate'])
src = pin(ml, 'Pose', False)
dst = pin(og, 'BlendPose_1', True)
log('before: layer pose links %d, gate B1 links %d' % (len(src.list_connected_pins()), len(dst.list_connected_pins())))
dst.break_pin_links()
log('outer gate BlendPose_1 cleared: %d' % len(dst.list_connected_pins()))
# the layer's pose may currently go to the jump gate; link it straight into the outer gate
if not [q for q in src.list_connected_pins() if q.get_owning_node() == og]:
    log('direct link layer -> outer gate: %s' % src.try_create_connection(dst))
log('after: %s' % [(q.get_owning_node().get_name(), str(q.get_pin_name())) for q in src.list_connected_pins()])

r = unreal.ToolsetRegistry.execute_tool(TOOLS, 'compile_blueprint', json.dumps({'blueprint': {'refPath': AB + '.ABP_AR02_LPSP_Test'}}))
log('compile: %s %s' % (r.is_complete, r.error))
log('saved: %s' % unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/slide_fix.log', 'w', encoding='utf-8').write('\n'.join(L))
