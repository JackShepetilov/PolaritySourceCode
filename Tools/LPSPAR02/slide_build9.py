import unreal, json

BS = unreal.BlueprintService
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
TOOLS = 'editor_toolset.toolsets.blueprint.BlueprintTools'
pkg = unreal.load_asset(AB).get_outermost()
JSON = unreal.Paths.project_saved_dir() + 'LPSP_AR02/slide_nodes.json'
W = json.load(open(JSON, encoding='utf-8'))


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


uc = find('AnimGraphNode_UseCachedPose_19')
ml = find(W['MaskLayer'])
og = find(W['OuterGate'])
ltc = find('AnimGraphNode_LocalToComponentSpace_5')
is_sliding = find(W['IsSliding'])
alpha_fn = find(W['SlideAlphaFn'])

# 1) layer weight: drop the fast engine alpha, let the outer gate do the fading
w = get_pin(ml, 'BlendWeights_0', True)
if w:
    w.break_pin_links()
    print('weight link to GetSlideAlpha removed; links now:', len(w.list_connected_pins()))
mi = ml.get_editor_property('node')
try:
    print('layer blend weights in struct:', mi.get_editor_property('blend_weights'))
except Exception as e:
    print('blend_weights read err:', e)

# 2) outer gate: base vs masked slide layer, faded by IsSliding with real blend times
print('gate links:', get_pin(og, 'BlendPose_0', True).try_create_connection(get_pin(uc, 'Pose', False)))
print('gate links:', get_pin(og, 'BlendPose_1', True).try_create_connection(get_pin(ml, 'Pose', False)))
print('gate active:', get_pin(og, 'bActiveValue', True).try_create_connection(get_pin(is_sliding, 'ReturnValue', False)))
gi = og.get_editor_property('node')
gi.set_editor_property('blend_time', [0.2, 0.45])
og.set_editor_property('node', gi)
print('gate blend times:', og.get_editor_property('node').get_editor_property('blend_time'))

# 3) re-route the chain through the gate
src = get_pin(ml, 'Pose', False)
src.break_pin_links()
print('chain: layer.Pose now goes to OuterGate only:', len(src.list_connected_pins()))
print('gate into pipeline:', get_pin(og, 'Pose', False).try_create_connection(get_pin(ltc, 'LocalPose', True)))

# 4) wider mask: body lean comes too, arms and head stay aimed
pose = unreal.InputBlendPose()
pose.import_text('(BranchFilters=((BoneName="pelvis"),(BoneName="spine_01"),(BoneName="thigh_l",BlendDepth=-1),(BoneName="thigh_r",BlendDepth=-1),(BoneName="ik_foot_root",BlendDepth=-1)))')
mi.set_editor_property('layer_setup', [pose])
ml.set_editor_property('node', mi)
print('mask now:', ml.get_editor_property('node').get_editor_property('layer_setup')[0].export_text())

# 5) the entry clip: it sits still for the first 0.5 s, so start it inside the drop and play it faster
pin_player = find('AnimGraphNode_SequencePlayer_5')
pi = pin_player.get_editor_property('node')
props = {}
for cand in ('play_rate', 'PlayRate', 'start_position', 'StartPosition', 'b_start_from_matching_pose'):
    try:
        props[cand] = pi.get_editor_property(cand)
    except Exception:
        pass
print('entry player props:', props)
for cand, val in (('play_rate', 1.5), ('start_position', 0.5)):
    try:
        pi.set_editor_property(cand, val)
    except Exception as e:
        print('  cannot set', cand, e)
pin_player.set_editor_property('node', pi)
pi2 = pin_player.get_editor_property('node')
print('entry player after:', {k: pi2.get_editor_property(k) for k in props})

# 6) compile + save
r = unreal.ToolsetRegistry.execute_tool(TOOLS, 'compile_blueprint', json.dumps({'blueprint': {'refPath': AB + '.ABP_AR02_LPSP_Test'}}))
print('compile:', r.is_complete, r.error)
print('status:', unreal.load_asset(AB).get_editor_property('status'))
print('saved:', unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
