import unreal, json

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
pkg = unreal.load_asset(AB).get_outermost()
JSON = unreal.Paths.project_saved_dir() + 'LPSP_AR02/slide_nodes.json'
W = json.load(open(JSON, encoding='utf-8'))

graph_obj = None
for n in unreal.ObjectIterator(unreal.EdGraphNode):
    if n.get_outermost() == pkg and n.get_outer().get_name() == GRAPH:
        graph_obj = n.get_outer()
        break


def find(name):
    for n in unreal.ObjectIterator(unreal.EdGraphNode):
        if n.get_outermost() == pkg and n.get_outer() and n.get_outer().get_name() == GRAPH and n.get_name() == name:
            return n
    return None


def dump_node(name):
    n = find(name)
    if not n:
        print('MISSING node', name)
        return
    print('%s  [%s]' % (name, str(n.get_node_title()).replace('\n', ' ')[:50]))
    for p in n.list_all_pins():
        links = ['%s.%s' % (q.get_owning_node().get_name(), q.get_pin_name()) for q in p.list_connected_pins()]
        if links or p.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_OUTPUT:
            print('    %-14s %-5s %s' % (str(p.get_pin_name()),
                                         'IN' if p.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_INPUT else 'OUT',
                                         ', '.join(links)))


names_to_dump = [W.get(k) for k in ('MaskLayer', 'PhaseGate', 'HoldGate', 'LessThan', 'SlideAlphaFn',
                                    'IsSliding', 'SlideDuration', 'Apex', 'CastChar', 'PawnOwner')]
for nm in names_to_dump:
    if nm:
        dump_node(nm)

print('\n=== the three players ===')
for nm in ('AnimGraphNode_SequencePlayer_5', 'AnimGraphNode_SequencePlayer_6', 'AnimGraphNode_SequencePlayer_7'):
    n = find(nm)
    inner = n.get_editor_property('node')
    seq = inner.get_editor_property('sequence')
    print('%-38s seq=%-42s loop=%s -> %s' % (
        nm, seq.get_name() if seq else None, inner.get_editor_property('bLoopAnimation'),
        ', '.join('%s.%s' % (q.get_owning_node().get_name(), q.get_pin_name())
                  for p in n.list_all_pins() if p.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_OUTPUT
                  for q in p.list_connected_pins())))

print('\n=== the insertion link ===')
ltc = find('AnimGraphNode_LocalToComponentSpace_5')
for p in ltc.list_all_pins():
    links = ['%s.%s' % (q.get_owning_node().get_name(), q.get_pin_name()) for q in p.list_connected_pins()]
    print('   %-14s %s' % (str(p.get_pin_name()), ', '.join(links)))
