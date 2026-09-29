import unreal

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
asset = unreal.load_asset(AB)
pkg = asset.get_outermost()

# Look for StateMachine_3 states
for n in unreal.ObjectIterator(unreal.EdGraphNode):
    if n.get_outermost() == pkg and n.get_name() == 'AnimGraphNode_StateMachine_3':
        print('StateMachine_3 title:', n.get_node_title())
        # print sub-graph / states
        outer = n.get_outer()
        print('outer:', outer.get_name())

# Find all AnimStateNode in this package
print('\n=== ALL STATES IN ANIM BP ===')
for n in unreal.ObjectIterator(unreal.EdGraphNode):
    if n.get_outermost() == pkg:
        cname = n.get_class().get_name()
        if 'AnimStateNode' in cname:
            title = str(n.get_node_title()).replace('\r', '').replace('\n', ' ') if hasattr(n, 'get_node_title') else ''
            print('  State: %s [%s] in Graph: %s' % (n.get_name(), title, n.get_outer().get_name()))
