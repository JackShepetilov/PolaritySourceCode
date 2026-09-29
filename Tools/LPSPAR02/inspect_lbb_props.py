import unreal

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
asset = unreal.load_asset(AB)
pkg = asset.get_outermost()

for n in unreal.ObjectIterator(unreal.EdGraphNode):
    if n.get_outermost() == pkg and 'LayeredBoneBlend_1' in n.get_name() and n.get_outer().get_name() == 'Standing -> Crouching':
        inner = n.get_editor_property('node')
        setup = inner.get_editor_property('layer_setup')
        print('layer_setup count:', len(setup))
        if len(setup) > 0:
            item = setup[0]
            print('export_text:', item.export_text())
            try:
                print('to_dict:', item.to_dict())
            except Exception as e:
                print('to_dict err:', e)
