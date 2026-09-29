import unreal

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
asset = unreal.load_asset(AB)
pkg = asset.get_outermost()

for n in unreal.ObjectIterator(unreal.EdGraphNode):
    if n.get_outermost() != pkg:
        continue
    cname = n.get_class().get_name()
    if 'UseCachedPose' in cname:
        title = str(n.get_node_title()).replace('\r', '').replace('\n', ' ') if hasattr(n, 'get_node_title') else ''
        out_links = []
        for p in n.list_all_pins():
            if p.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_OUTPUT:
                for q in p.list_connected_pins():
                    out_links.append('%s.%s' % (q.get_owning_node().get_name(), q.get_pin_name()))
        if any(k in title.lower() for k in ('sprint', 'crouch', 'stance', 'solved', 'ground')):
            print('  %s -> feeds: %s' % (title, ', '.join(out_links)))
