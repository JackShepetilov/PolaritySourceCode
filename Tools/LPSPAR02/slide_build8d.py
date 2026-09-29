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
        if n.get_outermost() == pkg and n.get_outer() == graph_obj and n.get_name() == name:
            return n
    return None


def create(key, x, y, substr):
    before = names(substr)
    r = BS.create_node_by_key(AB, GRAPH, key, x, y)
    new = names(substr) - before
    if len(new) != 1:
        print('   !! ambiguous', key, sorted(new))
        return None
    print('   created', sorted(new)[0])
    return find(sorted(new)[0])


def get_pin(node, name, input_side):
    for p in node.list_all_pins():
        if str(p.get_pin_name()) == name:
            is_in = p.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_INPUT
            if is_in == input_side:
                return p
    return None


def connect(src_name, src_pin, dst_name, dst_pin):
    s = find(src_name)
    d = find(dst_name)
    if not s or not d:
        print('   !! node missing for %s -> %s' % (src_name, dst_name))
        return False
    a = get_pin(s, src_pin, False)
    b = get_pin(d, dst_pin, True)
    if not a or not b:
        print('   !! pin missing %s.%s -> %s.%s' % (src_name, src_pin, dst_name, dst_pin))
        return False
    ok = a.try_create_connection(b)
    print('   %-42s %-14s -> %-34s %-14s : %s' % (src_name, src_pin, dst_name, dst_pin, ok))
    return ok


# --- new helper nodes ---
ks = BS.discover_nodes(AB, 'Slide Alpha', '', 10)
vkey = None
for k in ks:
    if str(k.spawner_key).startswith('SPAWN K2Node_VariableGet'):
        vkey = str(k.spawner_key)
        break
print('variable get key:', vkey)
if vkey:
    n = create(vkey, -1900.0, -3900.0, 'K2Node_VariableGet')
    if n:
        W['SlideAlphaVar'] = n.get_name()

n = create('FUNC KismetMathLibrary::Less_DoubleDouble', -1900.0, -4200.0, 'K2Node_CallFunction')
if n:
    W['LessThan'] = n.get_name()

json.dump(W, open(JSON, 'w', encoding='utf-8'), indent=2)
print('nodes:', W)

# --- wiring ---
print('\n== state chain ==')
connect(W['PawnOwner'], 'ReturnValue', W['CastChar'], 'Object')
connect(W['CastChar'], 'AsPolarity Character', W['Apex'], 'self')
connect(W['Apex'], 'ReturnValue', W['IsSliding'], 'self')
connect(W['Apex'], 'ReturnValue', W['SlideDuration'], 'self')
connect(W['SlideDuration'], 'ReturnValue', W['LessThan'], 'A')
connect(W['LessThan'], 'ReturnValue', W['PhaseGate'], 'bActiveValue')

print('\n== phases ==')
connect('AnimGraphNode_SequencePlayer_6', 'Pose', W['PhaseGate'], 'BlendPose_0')   # Loop on false
connect('AnimGraphNode_SequencePlayer_5', 'Pose', W['PhaseGate'], 'BlendPose_1')   # In on true
connect('AnimGraphNode_SequencePlayer_7', 'Pose', W['HoldGate'], 'BlendPose_0')    # Out on false
connect(W['PhaseGate'], 'Pose', W['HoldGate'], 'BlendPose_1')
connect(W['IsSliding'], 'ReturnValue', W['HoldGate'], 'bActiveValue')

print('\n== masked layer ==')
connect('AnimGraphNode_UseCachedPose_19', 'Pose', W['MaskLayer'], 'BasePose')
connect(W['HoldGate'], 'Pose', W['MaskLayer'], 'BlendPoses_0')
if W.get('SlideAlphaVar'):
    connect(W['SlideAlphaVar'], 'SlideAlpha', W['MaskLayer'], 'BlendWeights_0')

print('\n== insert into the pose chain ==')
uc = find('AnimGraphNode_UseCachedPose_19')
ltc = find('AnimGraphNode_LocalToComponentSpace_5')
if uc and ltc:
    src = get_pin(uc, 'Pose', False)
    dst = get_pin(ltc, 'ComponentPose', True)
    if src and dst:
        src.break_pin_links()
        print('   old link broken:', not src.list_connected_pins())
        connect(W['MaskLayer'], 'Pose', 'AnimGraphNode_LocalToComponentSpace_5', 'ComponentPose')
