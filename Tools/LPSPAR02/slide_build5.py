import unreal

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
BASE = '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/'
pkg = unreal.load_asset(AB).get_outermost()

WANT = {
    '495E2A3F40AA2EA06A12CF96046D4E8C': (BASE + 'M_Neutral_Slide_FootOut_Into_Lfoot', False, 'SlideIn'),
    'FF01BDBF41B2A104C1A3C090A3FAA3D0': (BASE + 'M_Neutral_Slide_FootOut_Loop', True, 'SlideLoop'),
    '44A4874A410983F707F25DBA307F6A7B': (BASE + 'M_Neutral_Slide_FootOut_Out_Moving_Run', False, 'SlideOut'),
}

targets = {}
for n in unreal.ObjectIterator(unreal.EdGraphNode):
    if n.get_outermost() != pkg or 'SequencePlayer' not in n.get_name():
        continue
    g = None
    for cand in ('guid', 'node_guid'):
        try:
            v = n.get_editor_property(cand)
            g = str(v).replace('-', '').upper()
            break
        except Exception:
            pass
    if g and g in WANT:
        targets[g] = n

print('matched by guid:', {k[:8]: v.get_name() for k, v in targets.items()})

for g, (asset_path, looping, tag) in WANT.items():
    n = targets.get(g)
    if not n:
        print('!! NOT FOUND', tag, g[:8])
        continue
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
    print('OK %s node=%s loopprop=%s -> %s / loop=%s' % (
        tag, n.get_name(), loop_prop, back.get_editor_property('sequence').get_name(),
        back.get_editor_property(loop_prop) if loop_prop else 'NONE'))
