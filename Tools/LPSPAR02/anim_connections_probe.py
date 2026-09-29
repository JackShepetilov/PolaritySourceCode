import json

import unreal

# Connections in the main AnimGraph around the standing/aiming merge and the new DefaultSlot nodes,
# so one slot can be moved to the merge point and the duplicate removed.

ABP = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/anim_connections.txt'
WATCH = ('DefaultSlot', 'CPose Standing State Slots', 'CPose Aiming State Slots', 'Blend Poses by bool')

BS = unreal.BlueprintService
lines = []
for edge in BS.get_connections(ABP, GRAPH):
    text = '%s.%s -> %s.%s' % (edge.source_node_title, edge.source_pin_name,
                               edge.target_node_title, edge.target_pin_name)
    lines.append('%s | %s -> %s | %s -> %s' % (text, edge.source_node_id, edge.source_pin_name,
                                               edge.target_node_id, edge.target_pin_name))
with open(OUT, 'w', encoding='utf-8') as handle:
    handle.write('edges=%d\n' % len(lines))
    handle.write('\n'.join(lines))

hits = [line for line in lines if any(token in line for token in WATCH)]
print('EDGES', len(lines), 'WATCHED', len(hits))
for line in hits:
    print(line.replace('\n', ' '))
