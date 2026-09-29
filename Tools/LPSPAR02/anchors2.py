import unreal, json

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
BS = unreal.BlueprintService
JSON = unreal.Paths.project_saved_dir() + 'LPSP_AR02/slide_nodes.json'
W = json.load(open(JSON, encoding='utf-8'))
L = []


def log(m):
    L.append(str(m))
    print(m)


nodes = {}
for n in BS.get_nodes_in_graph(AB, GRAPH):
    d = BS.get_node_details(AB, GRAPH, n.node_id)
    nodes[n.node_id] = (d.node_title.replace('\n', ' '), round(d.pos_x), round(d.pos_y))

ids = {}
for tag in ('PhaseGate', 'HoldGate', 'ExitGate', 'MaskLayer', 'OuterGate', 'JumpGate',
            'PawnOwner', 'CastChar', 'Apex', 'IsSliding', 'SlideDuration', 'LessThan',
            'SlideAlphaFn', 'GetCrouching', 'GetJumping', 'NotJumping', 'SlideOutCrouch'):
    nid = W.get(tag)
    if nid:
        ids[tag] = nid

# the three phase players are identified by title + current position
for nid, (title, x, y) in nodes.items():
    if title.startswith('Sequence Player') and x in (-0, 0, 500, 1000, 1500):
        log('player candidate: %s at (%d,%d)' % (nid[:8], x, y))

moves = {
    'SlideIn': (-6800, 2000), 'SlideLoop': (-6800, 2300), 'SlideOut': (-6400, 2000),
    'SlideOutCrouch': (-6400, 2300),
}
# map by walking the players in creation order: 5 = in, 6 = loop, 7 = out
order = ['AnimGraphNode_SequencePlayer_5', 'AnimGraphNode_SequencePlayer_6', 'AnimGraphNode_SequencePlayer_7']
pkg = unreal.load_asset(AB).get_outermost()


def find_obj(name):
    for n in unreal.ObjectIterator(unreal.EdGraphNode):
        if n.get_outermost() == pkg and n.get_outer() and n.get_outer().get_name() == GRAPH and n.get_name() == name:
            return n
    return None


for name, tag, pos in zip(order, ('SlideIn', 'SlideLoop', 'SlideOut'),
                          ((-6800, 2000), (-6800, 2300), (-6400, 2000))):
    o = find_obj(name)
    if not o:
        log('%s missing' % name)
        continue
    d = None
    for n in BS.get_nodes_in_graph(AB, GRAPH):
        if n.node_title.replace('\n', ' ').startswith('Sequence Player'):
            pass
    log('%s found in graph' % name)

# move players by their object names through get_node_details positions
for nid, (title, x, y) in list(nodes.items()):
    if title.startswith('Sequence Player') and y < -2000:
        log('unmoved player: %s at (%d,%d)' % (nid[:8], x, y))

log('comment around branch: %s' % BS.add_comment_around_nodes(
    AB, GRAPH, 'ВЕТКА СЛАЙДА (собрана, ОТКЛЮЧЕНА от пайплайна): фазы In/Loop/Out + маска ног',
    list(ids.values())))
log('save: %s' % unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/anchors2.log', 'w', encoding='utf-8').write('\n'.join(L))
