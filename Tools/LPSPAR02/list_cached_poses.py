import unreal

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
asset = unreal.load_asset(AB)
pkg = asset.get_outermost()

caches = {}
for n in unreal.ObjectIterator(unreal.EdGraphNode):
    if n.get_outermost() == pkg:
        cname = n.get_class().get_name()
        title = str(n.get_node_title()).replace('\r', '').replace('\n', ' ') if hasattr(n, 'get_node_title') else ''
        if 'SaveCachedPose' in cname:
            # find what feeds this cache
            in_links = []
            for p in n.list_all_pins():
                if p.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_INPUT:
                    for q in p.list_connected_pins():
                        in_links.append('%s.%s' % (q.get_owning_node().get_name(), q.get_pin_name()))
            caches[title] = (n.get_name(), in_links)

print('=== ALL CACHED POSES (%d) ===' % len(caches))
for title in sorted(caches):
    node_name, links = caches[title]
    print('  %s -> Node: %s, In: %s' % (title, node_name, ', '.join(links)))
