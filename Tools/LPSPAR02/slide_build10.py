import unreal, json

BS = unreal.BlueprintService
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
TOOLS = 'editor_toolset.toolsets.blueprint.BlueprintTools'
BASE = '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/'
pkg = unreal.load_asset(AB).get_outermost()
JSON = unreal.Paths.project_saved_dir() + 'LPSP_AR02/slide_nodes.json'
LOG = unreal.Paths.project_saved_dir() + 'LPSP_AR02/slide_build.log'
W = json.load(open(JSON, encoding='utf-8'))
LINES = []


def log(msg):
    LINES.append(str(msg))
    print(msg)


def names(substr):
    return {n.get_name() for n in unreal.ObjectIterator(unreal.EdGraphNode)
            if n.get_outermost() == pkg and n.get_outer() and n.get_outer().get_name() == GRAPH and substr in n.get_name()}


def find(name):
    for n in unreal.ObjectIterator(unreal.EdGraphNode):
        if n.get_outermost() == pkg and n.get_outer() and n.get_outer().get_name() == GRAPH and n.get_name() == name:
            return n
    return None


def create(key, x, y, substr):
    before = names(substr)
    BS.create_node_by_key(AB, GRAPH, key, x, y)
    new = names(substr) - before
    if len(new) != 1:
        log('   !! ambiguous %s %s' % (key, sorted(new)))
        return None
    log('   created %s' % sorted(new)[0])
    return find(sorted(new)[0])


def pin(node, name, input_side):
    for p in node.list_all_pins():
        if str(p.get_pin_name()) == name and (p.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_INPUT) == input_side:
            return p
    return None


def hard_link(src_name, src_pin, dst_name, dst_pin, tag=''):
    s, d = find(src_name), find(dst_name)
    if not s or not d:
        log('   missing node %s -> %s' % (src_name, dst_name))
        return False
    a, b = pin(s, src_pin, False), pin(d, dst_pin, True)
    if not a or not b:
        log('   missing pin %s.%s -> %s.%s' % (src_name, src_pin, dst_name, dst_pin))
        return False
    b.break_pin_links()
    ok = a.try_create_connection(b)
    log('   %s%s.%s -> %s.%s : %s' % (tag, src_name, src_pin, dst_name, dst_pin, ok))
    return ok


def set_blend(node_name, times):
    n = find(node_name)
    if not n:
        log('   missing %s' % node_name)
        return
    inner = n.get_editor_property('node')
    inner.set_editor_property('blend_time', times)
    n.set_editor_property('node', inner)
    log('   %s blend_time = %s' % (node_name, n.get_editor_property('node').get_editor_property('blend_time')))


log('== 1. exit clips: walk out and crouch out ==')
walk = find('AnimGraphNode_SequencePlayer_7')
wi = walk.get_editor_property('node')
wi.set_editor_property('sequence', unreal.load_asset(BASE + 'M_Neutral_Slide_FootOut_Out_Moving_Walk'))
walk.set_editor_property('node', wi)
log('walk-out clip: %s' % walk.get_editor_property('node').get_editor_property('sequence').get_name())

crouch_player = create('SPAWN AnimGraphNode_SequencePlayer|Sequence Player', 1500.0, -3000.0, 'SequencePlayer')
if crouch_player:
    W['SlideOutCrouch'] = crouch_player.get_name()
    ci = crouch_player.get_editor_property('node')
    ci.set_editor_property('sequence', unreal.load_asset(BASE + 'M_Neutral_Slide_FootOut_Out_Idle_Crouch'))
    for cand in ('bLoopAnimation', 'looping'):
        try:
            ci.set_editor_property(cand, False)
            break
        except Exception:
            continue
    crouch_player.set_editor_property('node', ci)
    log('crouch-out clip: %s' % crouch_player.get_editor_property('node').get_editor_property('sequence').get_name())


log('== 2. exit selector, jump suppression, bool sources ==')
exit_gate = create('SPAWN AnimGraphNode_BlendListByBool|Blend Poses by bool', -1200.0, -3600.0, 'BlendListByBool')
jump_gate = create('SPAWN AnimGraphNode_BlendListByBool|Blend Poses by bool', -600.0, -3600.0, 'BlendListByBool')
not_jump = create('FUNC KismetMathLibrary::Not_PreBool', -900.0, -4000.0, 'K2Node_CallFunction')
get_crouch = create('SPAWN K2Node_VariableGet|Get Crouching', -1700.0, -3300.0, 'K2Node_VariableGet')
get_jump = create('SPAWN K2Node_VariableGet|Get Jumping', -1700.0, -3600.0, 'K2Node_VariableGet')
for tag, node in (('ExitGate', exit_gate), ('JumpGate', jump_gate), ('NotJumping', not_jump),
                  ('GetCrouching', get_crouch), ('GetJumping', get_jump)):
    if node:
        W[tag] = node.get_name()
json.dump(W, open(JSON, 'w', encoding='utf-8'), indent=2)

log('== 3. wiring ==')
hard_link(W['SlideOutCrouch'], 'Pose', W['ExitGate'], 'BlendPose_0')
hard_link('AnimGraphNode_SequencePlayer_7', 'Pose', W['ExitGate'], 'BlendPose_1')
hard_link(W['GetCrouching'], 'Crouching', W['ExitGate'], 'bActiveValue')
set_blend(W['ExitGate'], [0.2, 0.2])

hard_link(W['ExitGate'], 'Pose', W['HoldGate'], 'BlendPose_0')

hard_link(W['GetJumping'], 'Jumping', W['NotJumping'], 'A')
hard_link(W['NotJumping'], 'ReturnValue', W['JumpGate'], 'bActiveValue')
hard_link('AnimGraphNode_UseCachedPose_19', 'Pose', W['JumpGate'], 'BlendPose_0')
hard_link(W['MaskLayer'], 'Pose', W['JumpGate'], 'BlendPose_1')
set_blend(W['JumpGate'], [0.2, 0.35])

hard_link(W['JumpGate'], 'Pose', W['OuterGate'], 'BlendPose_1')
set_blend(W['OuterGate'], [0.2, 0.45])

log('== 4. compile and save ==')
r = unreal.ToolsetRegistry.execute_tool(TOOLS, 'compile_blueprint', json.dumps({'blueprint': {'refPath': AB + '.ABP_AR02_LPSP_Test'}}))
log('compile: %s %s' % (r.is_complete, r.error))
log('status: %s' % unreal.load_asset(AB).get_editor_property('status'))
log('saved: %s' % unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))

open(LOG, 'w', encoding='utf-8').write('\n'.join(LINES))
