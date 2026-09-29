import unreal, json

# Step-by-step move of the slide layer behind the IK chain. Each step is verified; the pipeline is
# only re-pointed after the layer actually took the IK pose as its base.
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


def links(node_name, pin_name, in_side):
    return [q.get_owning_node().get_name() for q in pin(find(node_name), pin_name, in_side).list_connected_pins()]


fab_pose = pin(find('AnimGraphNode_Fabrik_8'), 'Pose', False)

# step 1: layer base <- IK output
b = pin(find(W['MaskLayer']), 'BasePose', True)
b.break_pin_links()
ok1 = b.try_create_connection(fab_pose)
log('step1 MaskLayer.BasePose <- Fabrik_8.Pose : %s' % ok1)

# step 2: gate base <- IK output
d = pin(find(W['OuterGate']), 'BlendPose_0', True)
d.break_pin_links()
ok2 = d.try_create_connection(fab_pose)
log('step2 OuterGate.BlendPose_0 <- Fabrik_8.Pose : %s' % ok2)

# step 3: only now re-point the output to the gate
if ok1 and ok2:
    ctl_in = pin(find('AnimGraphNode_ComponentToLocalSpace_5'), 'ComponentPose', True)
    ctl_in.break_pin_links()
    ok3 = ctl_in.try_create_connection(pin(find(W['OuterGate']), 'Pose', False))
    log('step3 ComponentToLocalSpace_5.ComponentPose <- OuterGate.Pose : %s' % ok3)
else:
    log('step3 skipped (a base link failed)')
    # make sure the main line is still whole
    ctl_in = pin(find('AnimGraphNode_ComponentToLocalSpace_5'), 'ComponentPose', True)
    if not ctl_in.list_connected_pins():
        log('restore main line: %s' % ctl_in.try_create_connection(fab_pose))

log('--- state ---')
for nm, pn, side in (('AnimGraphNode_Fabrik_8', 'Pose', False),
                     ('AnimGraphNode_ComponentToLocalSpace_5', 'ComponentPose', True),
                     (W['MaskLayer'], 'BasePose', True),
                     (W['OuterGate'], 'BlendPose_0', True),
                     (W['OuterGate'], 'Pose', False)):
    log('%-32s %-14s %s' % (nm, pn, links(nm, pn, side)))

r = unreal.ToolsetRegistry.execute_tool(TOOLS, 'compile_blueprint', json.dumps({'blueprint': {'refPath': AB + '.ABP_AR02_LPSP_Test'}}))
log('compile: %s %s' % (r.is_complete, r.error))
log('save: %s' % unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/step2.log', 'w', encoding='utf-8').write('\n'.join(L))
