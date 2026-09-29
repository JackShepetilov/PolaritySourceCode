import unreal, json

# HoldGate (which phase is shown) was gated by the graph's IsSliding call, which we could not verify.
# Gate it by the engine alpha instead: it was measured to be 1.0 during a slide and 0 otherwise.
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
BS = unreal.BlueprintService
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


before = {n.get_name() for n in unreal.ObjectIterator(unreal.EdGraphNode)
          if n.get_outermost() == pkg and n.get_outer() and n.get_outer().get_name() == GRAPH}
BS.create_node_by_key(AB, GRAPH, 'FUNC KismetMathLibrary::Greater_DoubleDouble', -4600.0, 3300.0)
new = {n.get_name() for n in unreal.ObjectIterator(unreal.EdGraphNode)
       if n.get_outermost() == pkg and n.get_outer() and n.get_outer().get_name() == GRAPH} - before
log('new compare node: %s' % sorted(new))
if len(new) == 1:
    cmp_node = find(sorted(new)[0])
    W['SlideCmp'] = sorted(new)[0]
    alpha = pin(find(W['SlideAlphaFn']), 'ReturnValue', False)
    log('A <- GetSlideAlpha : %s' % pin(cmp_node, 'A', True).try_create_connection(alpha))
    hold = find(W['HoldGate'])
    active = pin(hold, 'bActiveValue', True)
    active.break_pin_links()
    log('HoldGate.bActiveValue <- compare : %s' % active.try_create_connection(pin(cmp_node, 'ReturnValue', False)))

json.dump(W, open(JSON, 'w', encoding='utf-8'), indent=2)

# report what is still missing for the insertion
src = [q.get_owning_node().get_name() for q in pin(find('AnimGraphNode_ComponentToLocalSpace_5'), 'ComponentPose', True).list_connected_pins()]
layer = find('AnimGraphNode_LayeredBoneBlend_6')
log('main line: CTL_5.ComponentPose <- %s' % src)
if layer:
    log('layer pins:')
    for p in layer.list_all_pins():
        q = [x.get_owning_node().get_name() for x in p.list_connected_pins()]
        if q or p.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_OUTPUT:
            log('   %-14s %s' % (str(p.get_pin_name()), q))

r = unreal.ToolsetRegistry.execute_tool(TOOLS, 'compile_blueprint', json.dumps({'blueprint': {'refPath': AB + '.ABP_AR02_LPSP_Test'}}))
log('compile: %s %s' % (r.is_complete, r.error))
log('save: %s' % unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/gate_signal.log', 'w', encoding='utf-8').write('\n'.join(L))
