import unreal, json

BS = unreal.BlueprintService
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
pkg = unreal.load_asset(AB).get_outermost()
JSON = unreal.Paths.project_saved_dir() + 'LPSP_AR02/slide_nodes.json'
W = json.load(open(JSON, encoding='utf-8'))


def find(name):
    for n in unreal.ObjectIterator(unreal.EdGraphNode):
        if n.get_outermost() == pkg and n.get_outer() and n.get_outer().get_name() == GRAPH and n.get_name() == name:
            return n
    return None


print('=== pins of PhaseGate (%s) ===' % W['PhaseGate'])
print([str(p.get_pin_name()) for p in find(W['PhaseGate']).list_all_pins()])
print('=== pins of MaskLayer ===')
print([str(p.get_pin_name()) for p in find(W['MaskLayer']).list_all_pins()])
print('=== pins of player _5 ===')
print([str(p.get_pin_name()) for p in find('AnimGraphNode_SequencePlayer_5').list_all_pins()])

print('\n=== searching comparison spawners ===')
for term in ('Less', 'Greater', 'Float', 'Compare', 'KismetMathLibrary', 'Promotable'):
    ks = BS.discover_nodes(AB, term, '', 40)
    hits = [k for k in ks if 'KismetMathLibrary' in str(k.spawner_key) or 'Promotable' in str(k.spawner_key)
            or 'Comparison' in str(k.spawner_key) or 'Less' in str(k.display_name)]
    print('--- %r (total %d, hits %d)' % (term, len(ks), len(hits)))
    for k in hits[:12]:
        print('     ', k.spawner_key, '|', k.display_name)
