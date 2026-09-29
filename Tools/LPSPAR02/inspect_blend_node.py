import unreal

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
asset = unreal.load_asset(AB)
pkg = asset.get_outermost()

for n in unreal.ObjectIterator(unreal.EdGraphNode):
    if n.get_outermost() == pkg and n.get_name() == 'AnimGraphNode_BlendListByBool_0':
        print('Node title:', n.get_node_title())
        for p in n.list_all_pins():
            links = ['%s.%s' % (q.get_owning_node().get_name(), q.get_pin_name()) for q in p.list_connected_pins()]
            print('  Pin %s (%s): [%s]' % (p.get_pin_name(), p.get_pin_direction(), ', '.join(links)))
