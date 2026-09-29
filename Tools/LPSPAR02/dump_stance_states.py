import unreal

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
asset = unreal.load_asset(AB)
pkg = asset.get_outermost()

print('=== SMachine States Stance DETAILS ===')
for n in unreal.ObjectIterator(unreal.EdGraphNode):
    if n.get_outermost() == pkg and n.get_outer().get_name() == 'SMachine States Stance':
        s_name = getattr(n, 'node_title', '')
        # check inner state name
        prop_state = ''
        try:
            prop_state = n.get_editor_property('state_name')
        except Exception:
            pass
        print('  Node:', n.get_name(), 'Class:', n.get_class().get_name(), 'StateName:', prop_state)
