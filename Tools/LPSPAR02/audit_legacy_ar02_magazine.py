import unreal

assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), 'PIE must be stopped'
path = '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_AR02_Integration_Test'
print('STATUS', unreal.EditorAssetLibrary.load_asset(path).get_editor_property('status'))
print('COMPONENTS', [str(c) for c in unreal.BlueprintService.list_components(path)])
for graph in ('UserConstructionScript', 'EventGraph', 'ALPW: Get Droppable Magazine Mesh'):
    try:
        nodes = unreal.BlueprintService.get_nodes_in_graph(path, graph)
    except Exception as exc:
        print('GRAPH_ERROR', graph, exc)
        continue
    print('GRAPH', graph, 'NODE_COUNT', len(nodes))
    for node in nodes:
        title = str(node.node_title).replace('\n', ' ')
        if any(word in title.lower() for word in ('magazine', 'infimatp', 'staticmesh', 'drop')):
            print('NODE', graph, str(node.node_id), title)
