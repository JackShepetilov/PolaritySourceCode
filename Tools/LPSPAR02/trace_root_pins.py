import unreal

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
asset = unreal.load_asset(AB)
pkg = asset.get_outermost()

for n in unreal.ObjectIterator(unreal.EdGraphNode):
    if n.get_outermost() == pkg and 'AnimGraphNode_Root' in n.get_name():
        print('Root node pins:')
        for p in n.list_all_pins():
            print(' ', p.get_pin_name(), p.get_pin_direction())
            for q in p.list_connected_pins():
                print('    connected to:', q.get_owning_node().get_name(), q.get_pin_name())
