import unreal, json

# Repair + move the slide layer behind the IK chain, in the correct order (free the source pin first).
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


def link(src, src_pin, dst, dst_pin, free_src=False, free_dst=True):
    s, d = find(src), find(dst)
    a, b = pin(s, src_pin, False), pin(d, dst_pin, True)
    if not a or not b:
        log('MISSING pin %s.%s -> %s.%s' % (src, src_pin, dst, dst_pin))
        return False
    if free_dst:
        b.break_pin_links()
    if free_src:
        a.break_pin_links()
    ok = a.try_create_connection(b)
    log('link %s.%s -> %s.%s : %s' % (src, src_pin, dst, dst_pin, ok))
    return ok


# a) undo the old insertion: the cache pose feeds Local To Component again
link('AnimGraphNode_UseCachedPose_19', 'Pose', 'AnimGraphNode_LocalToComponentSpace_5', 'LocalPose')

# b) free the IK output, give it to the layer and the gate as the base, and route the gate to the output
link('AnimGraphNode_Fabrik_8', 'Pose', W['MaskLayer'], 'BasePose', free_src=True)
link('AnimGraphNode_Fabrik_8', 'Pose', W['OuterGate'], 'BlendPose_0')
link(W['OuterGate'], 'Pose', 'AnimGraphNode_ComponentToLocalSpace_5', 'ComponentPose')
link(W['MaskLayer'], 'Pose', W['OuterGate'], 'BlendPose_1')

log('--- state ---')
for nm in ('AnimGraphNode_UseCachedPose_19', 'AnimGraphNode_LocalToComponentSpace_5', 'AnimGraphNode_Fabrik_8',
           'AnimGraphNode_ComponentToLocalSpace_5', W['MaskLayer'], W['OuterGate']):
    n = find(nm)
    if not n:
        continue
    log('%s:' % nm)
    for p in n.list_all_pins():
        q = p.list_connected_pins()
        if q or p.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_OUTPUT:
            log('   %-14s %s' % (str(p.get_pin_name()), [x.get_owning_node().get_name() for x in q]))

r = unreal.ToolsetRegistry.execute_tool(TOOLS, 'compile_blueprint', json.dumps({'blueprint': {'refPath': AB + '.ABP_AR02_LPSP_Test'}}))
log('compile: %s %s' % (r.is_complete, r.error))
log('world: %s' % (unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None))
log('save: %s' % unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/slide_after_ik2.log', 'w', encoding='utf-8').write('\n'.join(L))
