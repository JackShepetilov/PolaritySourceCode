import unreal

BS = unreal.BlueprintService
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
BASE = '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/'

# Delete the three just created (we know their GUIDs)
for guid in ('DAF3EBD547FEBEA094DF93891C4037FB', '0EE1A0064CC9B8343C0F7983847FC210',
             '5273C6024439008CF39695975C6131E2'):
    print('delete', guid[:8], BS.delete_node(AB, GRAPH, guid))

KEY = 'SPAWN AnimGraphNode_SequencePlayer|Sequence Player'
ITEMS = [
    (BASE + 'M_Neutral_Slide_FootOut_Into_Lfoot', False, 'SlideIn', 0.0),
    (BASE + 'M_Neutral_Slide_FootOut_Loop', True, 'SlideLoop', 500.0),
    (BASE + 'M_Neutral_Slide_FootOut_Out_Moving_Run', False, 'SlideOut', 1000.0),
]

pkg = unreal.load_asset(AB).get_outermost()


def find_empty_players():
    out = []
    for n in unreal.ObjectIterator(unreal.EdGraphNode):
        if n.get_outermost() != pkg or 'SequencePlayer' not in n.get_name():
            continue
        try:
            if n.get_editor_property('node').get_editor_property('sequence') is None:
                out.append(n)
        except Exception:
            pass
    return out


print('empty before:', [n.get_name() for n in find_empty_players()])

for asset_path, looping, tag, x in ITEMS:
    nid = BS.create_node_by_key(AB, GRAPH, KEY, x, -3000.0)
    print('created', tag, nid)
    empties = find_empty_players()
    if len(empties) != 1:
        print('  !! expected exactly 1 empty, got', [e.get_name() for e in empties])
        continue
    n = empties[0]
    inner = n.get_editor_property('node')
    loop_prop = None
    for cand in ('looping', 'bLooping', 'b_looping', 'bShouldLoop'):
        try:
            inner.get_editor_property(cand)
            loop_prop = cand
            break
        except Exception:
            pass
    inner.set_editor_property('sequence', unreal.load_asset(asset_path))
    if loop_prop:
        inner.set_editor_property(loop_prop, looping)
    n.set_editor_property('node', inner)
    back = n.get_editor_property('node')
    print('  configured %s -> seq=%s loop=%s (%s)' % (
        tag, back.get_editor_property('sequence').get_name(),
        back.get_editor_property(loop_prop) if loop_prop else 'NOPROPS', n.get_name()))

print('final empty:', [n.get_name() for n in find_empty_players()])
