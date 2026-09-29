import json

import unreal

# Exact signatures of the two interface messages the pack's notify calls, so the events in the base
# weapon Blueprint are wired with the right pins.

IFACE = '/Game/InfimaGames/AnimatedLowPolyWeapons/Core/BPI_ALPW_Droppable_Magazine_Target'
OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/iface_signatures.jsonl'
BS = unreal.BlueprintService
open(OUT, 'w').close()

for graph in ('ALPW: Set Magazine Visibility', 'ALPW: Drop Magazine', 'ALPW: Get Droppable Magazine Mesh'):
    row = {'graph': graph, 'nodes': []}
    try:
        for info in BS.get_nodes_in_graph(IFACE, graph):
            row['nodes'].append({'id': str(info.node_id), 'type': str(info.node_type),
                                 'title': str(info.node_title).replace('\n', ' '),
                                 'pins': str(getattr(info, 'pin_names', ''))})
    except Exception as exc:
        row['error'] = str(exc)
    with open(OUT, 'a', encoding='utf-8') as handle:
        handle.write(json.dumps(row, default=str) + '\n')
    print(graph, '->', json.dumps(row['nodes'], default=str)[:400])
print('IFACE DONE', OUT)
