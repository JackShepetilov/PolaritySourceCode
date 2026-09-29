import json

import unreal

# What get_graph_definition offers for the AnimGraph, and how a slot node looks in it.

ABP = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/graph_def_slots.txt'
BS = unreal.BlueprintService
definition = BS.get_graph_definition(ABP, 'AnimGraph')
lines = []
count = 0
for entry in definition:
    text = json.dumps(entry, default=str) if not isinstance(entry, str) else entry
    if 'slot' in text.lower():
        lines.append(text)
    count += 1
with open(OUT, 'w', encoding='utf-8') as handle:
    handle.write('entries=%d\n' % count)
    handle.write('\n'.join(lines[:40]))
print('GRAPH DEF', count, 'slot-ish', len(lines))
print('\n'.join(lines[:6])[:1500])
