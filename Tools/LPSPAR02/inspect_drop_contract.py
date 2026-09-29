import unreal

assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), 'Stop PIE first'
root = '/Game/InfimaGames/AnimatedLowPolyWeapons/Core/'
prop_path = root + 'BP_ALPW_Magazine'
notify_path = root + 'BP_ALPW_AN_Magazine_Drop'
prop_class = unreal.EditorAssetLibrary.load_blueprint_class(prop_path)
assert prop_class, prop_path
cdo = unreal.get_default_object(prop_class)
print('PROP_CLASS', prop_class.get_name())
for name in ('Mesh Magazine', 'MeshMagazine', 'InitialLifeSpan'):
    try:
        value = cdo.get_editor_property(name)
        print('PROP', name, value, type(value))
        if isinstance(value, unreal.StaticMeshComponent):
            print('COMPONENT_MESH', value.get_editor_property('static_mesh'))
            print('COMPONENT_PHYSICS', value.is_simulating_physics())
    except Exception as exc:
        print('NO_PROP', name, exc)
print('PROP_COMPONENTS', [str(c) for c in unreal.BlueprintService.list_components(prop_path)])
print('COMPONENT_API', [name for name in dir(unreal.BlueprintService)
                        if 'component' in name.lower() and ('get_' in name or 'list_' in name)])
print('COMPONENT_GET_DOC', unreal.BlueprintService.get_component_property.__doc__)
print('COMPONENT_LIST_DOC', unreal.BlueprintService.list_component_properties.__doc__)
for name in ('StaticMesh', 'Mobility', 'SimulatePhysics', 'CollisionEnabled', 'CollisionProfileName',
             'LinearDamping', 'AngularDamping'):
    try:
        print('COMPONENT_PROP', name, unreal.BlueprintService.get_component_property(prop_path, 'Mesh Magazine', name))
    except Exception as exc:
        print('COMPONENT_PROP_ERROR', name, exc)
for item in unreal.BlueprintService.list_component_properties(prop_path, 'Mesh Magazine'):
    name = str(item.property_name)
    if any(word in name.lower() for word in ('physics', 'bodyinstance', 'collision')):
        print('COMPONENT_DETAIL', name, str(item.value)[:600])
        if name == 'BodyInstance':
            raw = str(item.value)
            for token in ('bSimulatePhysics', 'bLockTranslation', 'bLockRotation', 'CollisionEnabled', 'CollisionProfileName'):
                offset = raw.find(token)
                print('BODY_TOKEN', token, raw[offset:offset + 100] if offset >= 0 else 'missing')
for folder in ('/Game/InfimaGames/LowPolyShooterPack/Core/Notifies',
               '/Game/InfimaGames/AnimatedLowPolyWeapons/Core'):
    print('MAG_NOTIFIES', folder, [path for path in unreal.EditorAssetLibrary.list_assets(folder, False, False)
                                  if 'Magazin' in path or 'magazin' in path])
if unreal.EditorAssetLibrary.does_asset_exist(notify_path):
    for graph in unreal.BlueprintService.list_graphs(notify_path):
        graph_name = str(graph.graph_name)
        print('NOTIFY_GRAPH', graph_name)
        for node in unreal.BlueprintService.get_nodes_in_graph(notify_path, graph_name):
            title = str(node.node_title).replace('\n', ' ')
            if any(word in title.lower() for word in ('magazine', 'interface', 'visible', 'drop')):
                print('NOTIFY_NODE', title)
for graph in unreal.BlueprintService.list_graphs(prop_path):
    graph_name = str(graph.graph_name)
    print('PROP_GRAPH', graph_name)
    for node in unreal.BlueprintService.get_nodes_in_graph(prop_path, graph_name):
        title = str(node.node_title).replace('\n', ' ')
        print('PROP_NODE', title)
