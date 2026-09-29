import unreal

BS = unreal.BlueprintService
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'

for term, mx in (('State Machine', 50), ('AnimState', 30), ('Transition Rule', 30),
                 ('Blend', 60), ('Alpha', 30)):
    ks = BS.discover_nodes(AB, term, '', mx)
    spawns = [k for k in ks if str(k.spawner_key).startswith('SPAWN AnimGraph')]
    print('--- %r total=%d anim_spawns=%d' % (term, len(ks), len(spawns)))
    for k in spawns:
        print('     ', k.spawner_key, '|', k.display_name)

# TwoWayBlend variants: does another one expose an Alpha float pin?
asset = unreal.load_asset(AB)
pkg = asset.get_outermost()
print('\n=== TwoWayBlend pin sets ===')
for n in unreal.ObjectIterator(unreal.EdGraphNode):
    if n.get_outermost() == pkg and 'TwoWayBlend' in n.get_name():
        print('  %s:' % n.get_name(), [str(p.get_pin_name()) for p in n.list_all_pins()])
