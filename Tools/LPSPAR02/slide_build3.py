import unreal

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
BASE = '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/'
pkg = unreal.load_asset(AB).get_outermost()

DESIRE = {  # keyed by NodePosX of creation
    0: (BASE + 'M_Neutral_Slide_FootOut_Into_Lfoot', False, 'SlideIn'),
    500: (BASE + 'M_Neutral_Slide_FootOut_Loop', True, 'SlideLoop'),
    1000: (BASE + 'M_Neutral_Slide_FootOut_Out_Moving_Run', False, 'SlideOut'),
}

empties = []
for n in unreal.ObjectIterator(unreal.EdGraphNode):
    if n.get_outermost() != pkg or 'SequencePlayer' not in n.get_name():
        continue
    try:
        inner = n.get_editor_property('node')
        seq = inner.get_editor_property('sequence')
    except Exception as e:
        continue
    if seq is None:
        px = n.get_editor_property('node_pos_x')
        empties.append((n.get_name(), px, n, inner))

print('empty sequence players:', [(e[0], e[1]) for e in empties])

# candidate inner property names for looping
for name, px, n, inner in empties:
    if px not in DESIRE:
        print('  UNEXPECTED pos', name, px)
        continue
    asset_path, looping, tag = DESIRE[px]
    asset = unreal.load_asset(asset_path)
    print('---', tag, name, 'props on inner:')
    # find loop property name
    loop_prop = None
    for cand in ('looping', 'bLooping', 'b_looping', 'loop', 'bShouldLoop'):
        try:
            inner.get_editor_property(cand)
            loop_prop = cand
            break
        except Exception:
            pass
    print('    loop prop =', loop_prop)
    inner.set_editor_property('sequence', asset)
    if loop_prop:
        inner.set_editor_property(loop_prop, looping)
    n.set_editor_property('node', inner)
    # verify
    back = n.get_editor_property('node')
    print('    verify seq=%s loop=%s' % (back.get_editor_property('sequence').get_name(),
                                         back.get_editor_property(loop_prop) if loop_prop else '?'))
