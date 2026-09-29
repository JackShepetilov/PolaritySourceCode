import unreal, json

# Insert the layer behind the IK chain with the engine alpha as its weight: three links, and the main
# line is put back at once if any of them fails.
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


fab_pose = pin(find('AnimGraphNode_Fabrik_8'), 'Pose', False)
ctl_in = pin(find('AnimGraphNode_ComponentToLocalSpace_5'), 'ComponentPose', True)
layer = find(W['MaskLayer'])
layer_base = pin(layer, 'BasePose', True)
layer_weight = pin(layer, 'BlendWeights_0', True)
layer_out = pin(layer, 'Pose', False)
alpha_out = pin(find(W['SlideAlphaFn']), 'ReturnValue', False)

# 1) free the IK output
log('main line before: %s' % [q.get_owning_node().get_name() for q in ctl_in.list_connected_pins()])
ctl_in.break_pin_links()

# 2) layer takes the IK pose as its base
layer_base.break_pin_links()
ok_base = layer_base.try_create_connection(fab_pose)
log('layer.BasePose <- Fabrik_8.Pose : %s' % ok_base)

# 3) weight from the engine alpha
ok_w = False
if ok_base:
    layer_weight.break_pin_links()
    ok_w = layer_weight.try_create_connection(alpha_out)
    log('layer.BlendWeights_0 <- GetSlideAlpha : %s' % ok_w)

# 4) the layer feeds the output
ok_out = False
if ok_base and ok_w:
    open_links = [q for q in layer_out.list_connected_pins()]
    for q in open_links:
        pass
    ctl_in.break_pin_links()
    ok_out = ctl_in.try_create_connection(layer_out)
    log('ComponentToLocalSpace_5.ComponentPose <- layer.Pose : %s' % ok_out)

if not (ok_base and ok_w and ok_out):
    log('ROLLBACK: restoring the main line')
    layer_base.break_pin_links()
    layer_weight.break_pin_links()
    ctl_in.break_pin_links()
    log('main line restored: %s' % ctl_in.try_create_connection(fab_pose))

log('--- state ---')
for nm, pn, side in (('AnimGraphNode_Fabrik_8', 'Pose', False),
                     ('AnimGraphNode_ComponentToLocalSpace_5', 'ComponentPose', True),
                     (W['MaskLayer'], 'BasePose', True),
                     (W['MaskLayer'], 'BlendWeights_0', True),
                     (W['MaskLayer'], 'Pose', False)):
    n = find(nm)
    log('%-32s %-16s %s' % (nm, pn, [q.get_owning_node().get_name() for q in pin(n, pn, side).list_connected_pins()]))

r = unreal.ToolsetRegistry.execute_tool(TOOLS, 'compile_blueprint', json.dumps({'blueprint': {'refPath': AB + '.ABP_AR02_LPSP_Test'}}))
log('compile: %s %s' % (r.is_complete, r.error))
log('save: %s' % unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/step3.log', 'w', encoding='utf-8').write('\n'.join(L))
