import unreal

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
asset = unreal.load_asset(AB)
pkg = asset.get_outermost()

# 1) Real pin names of an existing LayeredBoneBlend and TwoWayBlend in the AnimGraph
for target in ('AnimGraphNode_LayeredBoneBlend_1', 'AnimGraphNode_TwoWayBlend_0'):
    for n in unreal.ObjectIterator(unreal.EdGraphNode):
        if n.get_outermost() == pkg and n.get_name() == target:
            print('=== pins of %s (%s)' % (target, n.get_outer().get_name()))
            for p in n.list_all_pins():
                print('   %s %s' % (p.get_pin_name(), p.get_pin_direction()))
            break

# 2) What does the BP EventGraph do on BlueprintUpdateAnimation? Node list of EventGraph
print('\n=== EventGraph nodes ===')
BS = unreal.BlueprintService
for n in BS.get_nodes_in_graph(AB, 'EventGraph'):
    print('   [%s] %s' % (n.node_id, n.node_title.replace('\n', ' ')[:80]))

# 3) SlideAlpha interp speed from the movement component default
comp_cls = unreal.load_class(None, '/Script/Polarity.ApexMovementComponent')
cdo = unreal.get_default_object(comp_cls)
for prop in ('SlideAlphaInterpSpeed', 'CrouchAlphaInterpSpeed', 'SlideMaxDuration', 'SlideCooldown'):
    try:
        print('COMP default %s = %s' % (prop, cdo.get_editor_property(prop)))
    except Exception as e:
        print('COMP default %s ERR' % prop)
