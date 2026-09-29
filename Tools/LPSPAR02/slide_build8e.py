import unreal, json

BS = unreal.BlueprintService
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
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


# 1) correct variable node for slide alpha
ks = BS.discover_nodes(AB, 'SlideAlpha', '', 10)
print('vars found for SlideAlpha:')
for k in ks:
    print('   ', k.spawner_key, '|', k.display_name)
vkey = None
for k in ks:
    if str(k.spawner_key).startswith('SPAWN K2Node_VariableGet') and str(k.display_name).strip() in ('SlideAlpha', 'Get SlideAlpha'):
        vkey = str(k.spawner_key)
        break
print('chosen key:', vkey)
if vkey:
    before = names('K2Node_VariableGet')
    BS.create_node_by_key(AB, GRAPH, vkey, -1900.0, -3300.0)
    new = names('K2Node_VariableGet') - before
    print('created var node:', sorted(new))
    if len(new) == 1:
        node = find(sorted(new)[0])
        W['SlideAlphaVar'] = sorted(new)[0]
        print('   pins:', [str(p.get_pin_name()) for p in node.list_all_pins()])

# 2) mask + blend times on the layer
mask = find(W['MaskLayer'])
inner = mask.get_editor_property('node')
setup = inner.get_editor_property('layer_setup')
print('layer_setup before:', setup[0].export_text() if setup else None)
pose = unreal.InputBlendPose()
pose.import_text('(BranchFilters=((BoneName="pelvis"),(BoneName="thigh_l",BlendDepth=-1),(BoneName="thigh_r",BlendDepth=-1),(BoneName="ik_foot_root",BlendDepth=-1)))')
inner.set_editor_property('layer_setup', [pose])
mask.set_editor_property('node', inner)
print('layer_setup after :', mask.get_editor_property('node').get_editor_property('layer_setup')[0].export_text())

for gate in ('PhaseGate', 'HoldGate'):
    g = find(W[gate])
    gi = g.get_editor_property('node')
    try:
        print('%s blend_time before: %s' % (gate, gi.get_editor_property('blend_time')))
    except Exception as e:
        print('%s blend_time read err: %s' % (gate, e))

# 3) wire the weight and re-insert into the pose chain
sv = find(W['SlideAlphaVar'])
ml = find(W['MaskLayer'])
ltc = find('AnimGraphNode_LocalToComponentSpace_5')
uc = find('AnimGraphNode_UseCachedPose_19')

print('ltc pins:', [str(p.get_pin_name()) for p in ltc.list_all_pins()] if ltc else None)

if sv and ml:
    a = get_pin(sv, 'SlideAlpha', False)
    b = get_pin(ml, 'BlendWeights_0', True)
    if a and b:
        print('weight link:', a.try_create_connection(b))
    else:
        print('weight pins missing: out=%s in=%s' % (bool(a), bool(b)))

if uc and ltc:
    src = get_pin(uc, 'Pose', False)
    dst = get_pin(ltc, 'LocalPose', True)
    if src and dst:
        src.break_pin_links()
        print('broken, remaining links on source:', len(src.list_connected_pins()))
        print('insert link:', get_pin(ml, 'Pose', False).try_create_connection(dst))
    else:
        print('chain pins missing: src=%s dst=%s' % (bool(src), bool(dst)))

json.dump(W, open(JSON, 'w', encoding='utf-8'), indent=2)
print('nodes:', W)
