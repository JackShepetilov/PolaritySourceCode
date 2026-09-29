import unreal

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
asset = unreal.load_asset(AB)
pkg = asset.get_outermost()

print('=== ANIM GRAPH NODES IN %s ===' % AB)
blends = []
for n in unreal.ObjectIterator(unreal.EdGraphNode):
    if n.get_outermost() != pkg:
        continue
    cname = n.get_class().get_name()
    title = str(n.get_node_title()) if hasattr(n, 'get_node_title') else ''
    if any(k in cname.lower() or k in title.lower() for k in ('layered', 'blend', 'slot', 'crouch')):
        pins = []
        for p in n.list_all_pins():
            links = [q.get_owning_node().get_name() for q in p.list_connected_pins()]
            if links or p.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_INPUT:
                pins.append('%s(%s)=[%s]' % (p.get_pin_name(), 'In' if p.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_INPUT else 'Out', ','.join(links)))
        blends.append((n.get_name(), cname, title.replace('\n', ' '), pins))

for b in blends:
    print('Node:', b[0], '| Class:', b[1], '| Title:', b[2])
    print('  Pins:', ' '.join(b[3]))
