import unreal

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
BS = unreal.BlueprintService
L = []


def log(m):
    L.append(str(m))
    print(m)


cands = []
for n in BS.get_nodes_in_graph(AB, GRAPH):
    t = n.node_title.replace('\n', ' ')
    if t.startswith('FABRIK') or t.startswith('Component To Local'):
        cands.append((n.node_id, t))
log('candidates: %d' % len(cands))
for cid, title in cands:
    d = BS.get_node_details(AB, GRAPH, cid)
    txt = str(d)
    log('--- %s | %s' % (cid[:8], title))
    log('    %s' % txt[:400])

log('connect_nodes doc: %s' % (BS.connect_nodes.__doc__ or '')[:300])
log('refresh_node doc: %s' % (BS.refresh_node.__doc__ or '')[:300])
open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/diag_ids.log', 'w', encoding='utf-8').write('\n'.join(L))
