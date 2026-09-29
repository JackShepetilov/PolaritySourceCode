import unreal, json

BS = unreal.BlueprintService
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
BASE = '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/'
pkg = unreal.load_asset(AB).get_outermost()

graph_obj = None
for n in unreal.ObjectIterator(unreal.EdGraphNode):
    if n.get_outermost() == pkg and n.get_outer().get_name() == GRAPH:
        graph_obj = n.get_outer()
        break
print('AnimGraph object:', graph_obj is not None)


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
    BS.create_node_by_key(AB, GRAPH, key, x, y)
    new = names(substr) - before
    if len(new) != 1:
        print('  !! ambiguous creation for', key, sorted(new))
        return None
    return find(sorted(new)[0])


# ---------- 1. the three phase players ----------
CFG = {
    'AnimGraphNode_SequencePlayer_5': (BASE + 'M_Neutral_Slide_FootOut_Into_Lfoot', False, 'SlideIn'),
    'AnimGraphNode_SequencePlayer_6': (BASE + 'M_Neutral_Slide_FootOut_Loop', True, 'SlideLoop'),
    'AnimGraphNode_SequencePlayer_7': (BASE + 'M_Neutral_Slide_FootOut_Out_Moving_Run', False, 'SlideOut'),
}
for nm, (asset_path, looping, tag) in CFG.items():
    n = find(nm)
    if not n:
        print('MISSING player', nm)
        continue
    inner = n.get_editor_property('node')
    inner.set_editor_property('sequence', unreal.load_asset(asset_path))
    used = None
    for cand in ('bLoopAnimation', 'looping', 'bLooping'):
        try:
            inner.set_editor_property(cand, looping)
            used = cand
            break
        except Exception:
            continue
    n.set_editor_property('node', inner)
    back = n.get_editor_property('node')
    print('player %s -> %s (loop prop %s = %s)' % (
        nm, back.get_editor_property('sequence').get_name(), used,
        back.get_editor_property(used) if used else '?'))

# ---------- 2. gates and the masked layer ----------
CREATED = {}
KEY_BOOL = 'SPAWN AnimGraphNode_BlendListByBool|Blend Poses by bool'
KEY_LAYER = 'SPAWN AnimGraphNode_LayeredBoneBlend|Layered blend per bone'

for tag, key, substr, x in (
        ('PhaseGate', KEY_BOOL, 'BlendListByBool', -2500.0),
        ('HoldGate', KEY_BOOL, 'BlendListByBool', -2000.0),
        ('MaskLayer', KEY_LAYER, 'LayeredBoneBlend', -1500.0),
        ('OuterGate', KEY_BOOL, 'BlendListByBool', -1000.0)):
    node = create(key, x, -3600.0, substr)
    if node:
        CREATED[tag] = node.get_name()
        print('created %-10s -> %s' % (tag, node.get_name()))

out = unreal.Paths.project_saved_dir() + 'LPSP_AR02/slide_nodes.json'
open(out, 'w', encoding='utf-8').write(json.dumps(CREATED, indent=2))
print('WROTE', out, CREATED)
