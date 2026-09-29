import unreal

BS = unreal.BlueprintService
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
BASE = '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/'

print('create_node_by_key doc:', BS.create_node_by_key.__doc__)
print('configure_node doc:', (BS.configure_node.__doc__ or '')[:600])

KEY = 'SPAWN AnimGraphNode_SequencePlayer|Sequence Player'
print('doc:', (BS.create_node_by_key.__doc__ or '')[:400])

items = [
    ('SlideIn', 0.0, -3000.0),
    ('SlideLoop', 500.0, -3000.0),
    ('SlideOut', 1000.0, -3000.0),
]

for ref, x, y in items:
    res = BS.create_node_by_key(AB, GRAPH, KEY, x, y)
    print('create', ref, '->', res)

# Find created nodes and configure inner props
for n in BS.get_nodes_in_graph(AB, GRAPH):
    if n.node_title.startswith('Sequence Player') and n.node_id not in ['']:
        pass

print('\n--- nodes in AnimGraph with Sequence Player title ---')
for n in BS.get_nodes_in_graph(AB, GRAPH):
    t = n.node_title.replace('\n', ' ')
    if t.startswith('Sequence Player'):
        print('   id=%s title=%s' % (n.node_id, t[:60]))
