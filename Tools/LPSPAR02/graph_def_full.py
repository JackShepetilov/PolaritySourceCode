import unreal

# Full shape of get_graph_definition for the AnimGraph: nodes, connections, defaults.

ABP = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/graph_def_full.txt'
definition = unreal.BlueprintService.get_graph_definition(ABP, 'AnimGraph')
with open(OUT, 'w', encoding='utf-8') as handle:
    handle.write('type=%s len=%d\n' % (type(definition), len(definition) if hasattr(definition, '__len__') else -1))
    for index, item in enumerate(definition):
        text = item if isinstance(item, str) else str(item)
        handle.write('--- item %d (len=%d)\n%s\n' % (index, len(text), text[:6000]))
print('GRAPH DEF FULL DONE')
for index, item in enumerate(definition):
    text = item if isinstance(item, str) else str(item)
    print('ITEM', index, 'len', len(text), 'head:', text[:180].replace('\n', ' '))
