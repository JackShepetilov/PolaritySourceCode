import unreal

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
BS = unreal.BlueprintService
L = []


def log(m):
    L.append(str(m))
    print(m)


nodes = {}
for n in BS.get_nodes_in_graph(AB, GRAPH):
    d = BS.get_node_details(AB, GRAPH, n.node_id)
    nodes[(round(d.pos_x), round(d.pos_y))] = n.node_id

PLAN = {(-4040, -3368): (-6800, 2000),   # SlideIn
        (-3476, -3376): (-6800, 2300),   # SlideLoop
        (-2976, -3376): (-6400, 2000)}   # SlideOut
for cur, new in PLAN.items():
    nid = nodes.get(cur)
    if not nid:
        log('not found at %s' % (cur,))
        continue
    log('move %s -> %s : %s' % (cur, new, BS.set_node_position(AB, GRAPH, nid, new[0], new[1])))

# comment over the phase players so they read as one block with the branch
ids = [nid for cur, nid in nodes.items() if cur in PLAN]
if ids:
    log('players comment: %s' % BS.add_comment_around_nodes(
        AB, GRAPH, 'Клипы слайда: Into (вход), Loop (скольжение), Out_Moving_Walk (выход в бег)', ids))
log('save: %s' % unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/anchors3.log', 'w', encoding='utf-8').write('\n'.join(L))
