import unreal, json

# Reload the asset (all pins rebuilt clean) then try the insertion again.
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
BS = unreal.BlueprintService
TOOLS = 'editor_toolset.toolsets.blueprint.BlueprintTools'
JSON = unreal.Paths.project_saved_dir() + 'LPSP_AR02/slide_nodes.json'
W = json.load(open(JSON, encoding='utf-8'))
L = []


def log(m):
    L.append(str(m))
    print(m)


log('reload: %s' % unreal.EditorAssetLibrary.reload_asset(AB))
pkg = unreal.load_asset(AB).get_outermost()


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


ctl = find('AnimGraphNode_ComponentToLocalSpace_5')
ctl_in = pin(ctl, 'ComponentPose', True)
src = [q.get_owning_node().get_name() for q in ctl_in.list_connected_pins()]
log('CTL_5.ComponentPose <- %s' % src)
if not src:
    open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/wire2.log', 'w', encoding='utf-8').write('\n'.join(L))
    raise SystemExit
src_node = find(src[0])
src_out = pin(src_node, 'Pose', False)

layer = find('AnimGraphNode_LayeredBoneBlend_6')
if not layer:
    log('layer nodes present: %s' % [n for n in (find(x) for x in [])])
    open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/wire2.log', 'w', encoding='utf-8').write('\n'.join(L))
    raise SystemExit

mi = layer.get_editor_property('node')
pose = unreal.InputBlendPose()
pose.import_text('(BranchFilters=((BoneName="pelvis"),(BoneName="thigh_l",BlendDepth=-1),(BoneName="thigh_r",BlendDepth=-1)))')
mi.set_editor_property('layer_setup', [pose])
layer.set_editor_property('node', mi)

ctl_in.break_pin_links()
ok_base = pin(layer, 'BasePose', True).try_create_connection(src_out)
log('base <- %s : %s' % (src[0], ok_base))
ok_out = ctl_in.try_create_connection(pin(layer, 'Pose', False))
log('output <- layer.Pose : %s' % ok_out)

# phase chain and weight
hold = find(W['HoldGate'])
ok_ph = pin(layer, 'BlendPoses_0', True).try_create_connection(pin(hold, 'Pose', False))
ok_w = pin(layer, 'BlendWeights_0', True).try_create_connection(pin(find(W['SlideAlphaFn']), 'ReturnValue', False))
log('phases: %s  weight: %s' % (ok_ph, ok_w))

if not (ok_base and ok_out):
    log('ROLLBACK')
    pin(layer, 'BasePose', True).break_pin_links()
    ctl_in.break_pin_links()
    ctl_in.try_create_connection(src_out)

r = unreal.ToolsetRegistry.execute_tool(TOOLS, 'compile_blueprint', json.dumps({'blueprint': {'refPath': AB + '.ABP_AR02_LPSP_Test'}}))
log('compile: %s %s' % (r.is_complete, r.error))
log('save: %s' % unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/wire2.log', 'w', encoding='utf-8').write('\n'.join(L))
