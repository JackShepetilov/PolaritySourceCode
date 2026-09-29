import unreal

W = '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_AR02_Integration_Test'
pkg = unreal.load_asset(W).get_outermost()
print('=== skeletal mesh components in the weapon BP and their anim classes ===')
for c in unreal.ObjectIterator(unreal.SkeletalMeshComponent):
    if c.get_outermost() != pkg:
        continue
    sk = c.get_skinned_asset()
    skel = None
    if sk:
        try:
            skel = sk.get_editor_property('skeleton')
        except Exception:
            skel = None
    ai = None
    for prop in ('anim_class', 'anim_instance_class'):
        try:
            ai = c.get_editor_property(prop)
            break
        except Exception:
            continue
    print('  %-28s mesh=%-22s skeleton=%-18s anim_class=%s' % (
        c.get_name(), sk.get_name() if sk else None,
        skel.get_name() if skel else None, ai))
