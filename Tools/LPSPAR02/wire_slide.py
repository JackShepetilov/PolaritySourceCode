import unreal, json

# Insert a FRESH masked layer at the very end of the chain (between the last node feeding
# Component To Local and that node) with the engine alpha as its weight. Fresh nodes accept links.
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


log('world: %s' % (unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_world() is not None))

# what feeds Component To Local right now
ctl = find('AnimGraphNode_ComponentToLocalSpace_5')
ctl_in = pin(ctl, 'ComponentPose', True)
src_name = [q.get_owning_node().get_name() for q in ctl_in.list_connected_pins()]
log('ComponentToLocalSpace_5.ComponentPose <- %s' % src_name)
if not src_name:
    log('nothing feeds the output; abort')
    open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/wire.log', 'w', encoding='utf-8').write('\n'.join(L))
    raise SystemExit

src = find(src_name[0])
src_out = pin(src, 'Pose', False)

# fresh layer
before = {n.get_name() for n in unreal.ObjectIterator(unreal.EdGraphNode)
          if n.get_outermost() == pkg and n.get_outer() and n.get_outer().get_name() == GRAPH}
BS.create_node_by_key(AB, GRAPH, 'SPAWN AnimGraphNode_LayeredBoneBlend|Layered blend per bone', -4100.0, 2400.0)
new_names = {n.get_name() for n in unreal.ObjectIterator(unreal.EdGraphNode)
             if n.get_outermost() == pkg and n.get_outer() and n.get_outer().get_name() == GRAPH} - before
log('new nodes: %s' % sorted(new_names))
layer = find(sorted(n for n in new_names if 'LayeredBoneBlend' in n)[0]) if any('LayeredBoneBlend' in n for n in new_names) else None
if not layer:
    log('layer not created; abort')
    open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/wire.log', 'w', encoding='utf-8').write('\n'.join(L))
    raise SystemExit

# mask: legs only
mi = layer.get_editor_property('node')
pose = unreal.InputBlendPose()
pose.import_text('(BranchFilters=((BoneName="pelvis"),(BoneName="thigh_l",BlendDepth=-1),(BoneName="thigh_r",BlendDepth=-1)))')
mi.set_editor_property('layer_setup', [pose])
layer.set_editor_property('node', mi)
log('mask: %s' % layer.get_editor_property('node').get_editor_property('layer_setup')[0].export_text())

# wire: base <- chain, phases <- HoldGate, weight <- engine alpha, output <- Component To Local
ctl_in.break_pin_links()
ok_base = pin(layer, 'BasePose', True).try_create_connection(src_out)
log('base <- %s : %s' % (src_name[0], ok_base))

hold = find(W['HoldGate'])
phases = pin(layer, 'BlendPoses_0', True)
ok_ph = phases.try_create_connection(pin(hold, 'Pose', False))
log('phases <- HoldGate.Pose : %s' % ok_ph)

alpha = pin(find(W['SlideAlphaFn']), 'ReturnValue', False)
ok_w = pin(layer, 'BlendWeights_0', True).try_create_connection(alpha)
log('weight <- GetSlideAlpha : %s' % ok_w)

ok_out = ctl_in.try_create_connection(pin(layer, 'Pose', False))
log('output <- layer.Pose : %s' % ok_out)

if not (ok_base and ok_out):
    log('ROLLBACK')
    pin(layer, 'BasePose', True).break_pin_links()
    ctl_in.break_pin_links()
    ctl_in.try_create_connection(src_out)

r = unreal.ToolsetRegistry.execute_tool(TOOLS, 'compile_blueprint', json.dumps({'blueprint': {'refPath': AB + '.ABP_AR02_LPSP_Test'}}))
log('compile: %s %s' % (r.is_complete, r.error))
log('save: %s' % unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/wire.log', 'w', encoding='utf-8').write('\n'.join(L))
