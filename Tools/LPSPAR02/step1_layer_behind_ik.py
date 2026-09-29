import unreal, json

# Step 1: insert the slide layer BEHIND the IK chain (after the last FABRIK), mask on the legs only.
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


def link(src, src_pin, dst, dst_pin, free_src=False):
    s, d = find(src), find(dst)
    a, b = pin(s, src_pin, False), pin(d, dst_pin, True)
    if not a or not b:
        log('MISSING %s.%s -> %s.%s' % (src, src_pin, dst, dst_pin))
        return False
    if free_src:
        a.break_pin_links()
    b.break_pin_links()
    ok = a.try_create_connection(b)
    log('link %s.%s -> %s.%s : %s' % (src, src_pin, dst, dst_pin, ok))
    return ok


log('game world: %s' % (unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None))

# free the IK output first, then feed the layer and the gate with it
link('AnimGraphNode_Fabrik_8', 'Pose', W['MaskLayer'], 'BasePose', free_src=True)
link('AnimGraphNode_Fabrik_8', 'Pose', W['OuterGate'], 'BlendPose_0')
link(W['MaskLayer'], 'Pose', W['OuterGate'], 'BlendPose_1')
link(W['OuterGate'], 'Pose', 'AnimGraphNode_ComponentToLocalSpace_5', 'ComponentPose')

# mask: legs only, the torso must ignore the slide
ml = find(W['MaskLayer'])
mi = ml.get_editor_property('node')
pose = unreal.InputBlendPose()
pose.import_text('(BranchFilters=((BoneName="pelvis"),(BoneName="thigh_l",BlendDepth=-1),(BoneName="thigh_r",BlendDepth=-1)))')
mi.set_editor_property('layer_setup', [pose])
ml.set_editor_property('node', mi)
log('mask: %s' % ml.get_editor_property('node').get_editor_property('layer_setup')[0].export_text())
log('layer weight: %s' % ml.get_editor_property('node').get_editor_property('blend_weights'))

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
open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/step1_layer_behind_ik.log', 'w', encoding='utf-8').write('\n'.join(L))
