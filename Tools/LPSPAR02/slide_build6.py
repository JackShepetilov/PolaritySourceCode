import unreal

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
BASE = '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/'
asset = unreal.load_asset(AB)
pkg = asset.get_outermost()

# Find graph object of AnimGraph
anim_graph = None
for n in unreal.ObjectIterator(unreal.EdGraphNode):
    if n.get_outermost() == pkg and n.get_outer().get_name() == 'AnimGraph':
        anim_graph = n.get_outer()
        break
print('AnimGraph object:', anim_graph)

rows = []
for n in unreal.ObjectIterator(unreal.EdGraphNode):
    if n.get_outermost() != pkg or 'SequencePlayer' not in n.get_name():
        continue
    if n.get_outer() != anim_graph:
        continue
    inner = n.get_editor_property('node')
    seq = None
    prop_used = None
    for cand in ('sequence', 'AnimationToPlay', 'animation', 'anim_to_play', 'Sequence'):
        try:
            v = inner.get_editor_property(cand)
            prop_used = cand
            seq = v
            break
        except Exception:
            continue
    # numeric suffix
    num = int(n.get_name().rsplit('_', 1)[-1])
    rows.append((num, n.get_name(), seq.get_name() if seq else None, prop_used))

rows.sort()
print('--- SequencePlayer nodes in AnimGraph (sorted by suffix) ---')
for r in rows:
    print('   %3d %-40s seq=%s [%s]' % r)

# stash: the three with seq=None and highest numbers are ours
print('\nprop candidates result on known node with asset: rows with seq set above show the working prop.')
