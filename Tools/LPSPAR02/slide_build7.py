import unreal

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
BASE = '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/'
pkg = unreal.load_asset(AB).get_outermost()

MAP = {
    'AnimGraphNode_SequencePlayer_5': (BASE + 'M_Neutral_Slide_FootOut_Into_Lfoot', False, 'SlideIn'),
    'AnimGraphNode_SequencePlayer_6': (BASE + 'M_Neutral_Slide_FootOut_Loop', True, 'SlideLoop'),
    'AnimGraphNode_SequencePlayer_7': (BASE + 'M_Neutral_Slide_FootOut_Out_Moving_Run', False, 'SlideOut'),
}

found = {}
for n in unreal.ObjectIterator(unreal.EdGraphNode):
    if n.get_outermost() == pkg and n.get_name() in MAP and n.get_outer().get_name() == 'AnimGraph':
        found[n.get_name()] = n
print('found in AnimGraph:', sorted(found))

for name, (asset_path, looping, tag) in MAP.items():
    n = found.get(name)
    if not n:
        print('MISSING', name)
        continue
    inner = n.get_editor_property('node')
    before = inner.get_editor_property('sequence')
    loop_prop = None
    for cand in ('bLoopAnimation', 'looping', 'bLooping', 'loop', 'bShouldLoop'):
        try:
            inner.get_editor_property(cand)
            loop_prop = cand
            break
        except Exception:
            pass
    print('%s: before=%s loop_prop=%s' % (tag, before.get_name() if before else None, loop_prop))
    inner.set_editor_property('sequence', unreal.load_asset(asset_path))
    if loop_prop:
        inner.set_editor_property(loop_prop, looping)
    n.set_editor_property('node', inner)
    back = n.get_editor_property('node')
    print('   AFTER %s: seq=%s loop=%s' % (
        tag, back.get_editor_property('sequence').get_name(),
        back.get_editor_property(loop_prop) if loop_prop else '??'))
    print('   pins:', [str(p.get_pin_name()) for p in n.list_all_pins()])
