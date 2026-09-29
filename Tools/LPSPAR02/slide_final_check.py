import unreal, json

BS = unreal.BlueprintService
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
TOOLS = 'editor_toolset.toolsets.blueprint.BlueprintTools'
pkg = unreal.load_asset(AB).get_outermost()
JSON = unreal.Paths.project_saved_dir() + 'LPSP_AR02/slide_nodes.json'
LOG = unreal.Paths.project_saved_dir() + 'LPSP_AR02/slide_final.log'
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


ml = find(W['MaskLayer'])
# 1) layer weight must stay 1: the fading is the outer gate's job
w = pin(ml, 'BlendWeights_0', True)
if w and w.list_connected_pins():
    w.break_pin_links()
    log('weight unplugged from engine alpha, links now %d' % len(w.list_connected_pins()))
mi = ml.get_editor_property('node')
try:
    log('blend_weights in struct: %s' % (mi.get_editor_property('blend_weights'),))
except Exception as e:
    log('blend_weights read err: %s' % e)
# 2) mask: legs + body lean, arms and head stay on the base pose
pose = unreal.InputBlendPose()
pose.import_text('(BranchFilters=((BoneName="pelvis"),(BoneName="spine_01"),(BoneName="thigh_l",BlendDepth=-1),(BoneName="thigh_r",BlendDepth=-1),(BoneName="ik_foot_root",BlendDepth=-1)))')
mi.set_editor_property('layer_setup', [pose])
ml.set_editor_property('node', mi)
log('mask: %s' % ml.get_editor_property('node').get_editor_property('layer_setup')[0].export_text())

# 3) full dump of the slide branch
for tag in ('MaskLayer', 'ExitGate', 'JumpGate', 'HoldGate', 'PhaseGate', 'OuterGate', 'NotJumping',
            'GetCrouching', 'GetJumping'):
    n = find(W.get(tag, ''))
    if not n:
        log('%s: MISSING' % tag)
        continue
    log('%s (%s)' % (tag, n.get_name()))
    for p in n.list_all_pins():
        links = ['%s.%s' % (q.get_owning_node().get_name(), q.get_pin_name()) for q in p.list_connected_pins()]
        if links or p.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_OUTPUT:
            log('    %-14s %-4s %s' % (str(p.get_pin_name()),
                                       'IN' if p.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_INPUT else 'OUT',
                                       ', '.join(links)))

log('players:')
for nm in ('AnimGraphNode_SequencePlayer_5', 'AnimGraphNode_SequencePlayer_6', 'AnimGraphNode_SequencePlayer_7',
           W.get('SlideOutCrouch', '')):
    n = find(nm)
    if not n:
        continue
    seq = n.get_editor_property('node').get_editor_property('sequence')
    log('    %-38s %-42s loop=%s' % (nm, seq.get_name() if seq else None,
                                     n.get_editor_property('node').get_editor_property('bLoopAnimation')))

r = unreal.ToolsetRegistry.execute_tool(TOOLS, 'compile_blueprint', json.dumps({'blueprint': {'refPath': AB + '.ABP_AR02_LPSP_Test'}}))
log('compile: %s %s' % (r.is_complete, r.error))
log('status: %s' % unreal.load_asset(AB).get_editor_property('status'))
log('saved: %s' % unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
open(LOG, 'w', encoding='utf-8').write('\n'.join(L))
