import unreal, json

# Repair + move the layer, using the call form that actually works: receiver.try_create_connection(source),
# and never break_pin_links on an output.
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
TOOLS = 'editor_toolset.toolsets.blueprint.BlueprintTools'
pkg = unreal.load_asset(AB).get_outermost()
JSON = unreal.Paths.project_saved_dir() + 'LPSPAR02/slide_nodes.json'.replace('LPSPAR02', 'LPSP_AR02')
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


def connect(src_name, src_pin, dst_name, dst_pin):
    """Free the receiver, then create the link in the receiver->source form."""
    s, d = find(src_name), find(dst_name)
    a, b = pin(s, src_pin, False), pin(d, dst_pin, True)
    if not a or not b:
        log('MISSING %s.%s -> %s.%s' % (src_name, src_pin, dst_name, dst_pin))
        return False
    b.break_pin_links()
    ok = b.try_create_connection(a)
    if not ok:
        ok = a.try_create_connection(b)
    log('connect %s.%s -> %s.%s : %s' % (src_name, src_pin, dst_name, dst_pin, ok))
    return ok


log('world: %s' % (unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None))

# 1) the receiver of the IK output is what has to be freed first
pin(find('AnimGraphNode_ComponentToLocalSpace_5'), 'ComponentPose', True).break_pin_links()
log('freed ComponentToLocalSpace_5.ComponentPose')

connect('AnimGraphNode_Fabrik_8', 'Pose', W['MaskLayer'], 'BasePose')
connect('AnimGraphNode_Fabrik_8', 'Pose', W['OuterGate'], 'BlendPose_0')
connect(W['MaskLayer'], 'Pose', W['OuterGate'], 'BlendPose_1')
connect(W['OuterGate'], 'Pose', 'AnimGraphNode_ComponentToLocalSpace_5', 'ComponentPose')

log('--- path check ---')
for nm in ('AnimGraphNode_Fabrik_8', 'AnimGraphNode_ComponentToLocalSpace_5', 'AnimGraphNode_LocalToComponentSpace_5',
           W['MaskLayer'], W['OuterGate']):
    n = find(nm)
    log('%s:' % nm)
    for p in n.list_all_pins():
        q = p.list_connected_pins()
        if q or p.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_OUTPUT:
            log('   %-14s %s' % (str(p.get_pin_name()), [x.get_owning_node().get_name() for x in q]))

r = unreal.ToolsetRegistry.execute_tool(TOOLS, 'compile_blueprint', json.dumps({'blueprint': {'refPath': AB + '.ABP_AR02_LPSP_Test'}}))
log('compile: %s %s' % (r.is_complete, r.error))
log('save: %s' % unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/step1b.log', 'w', encoding='utf-8').write('\n'.join(L))
