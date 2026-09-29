import unreal

assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), 'PIE must be stopped'
bp_path = '/Game/Variant_Shooter/Blueprints/Pickups/BP_ShooterWeaponBase'
graph = 'ALPW: Get Droppable Magazine Mesh'
service = unreal.BlueprintService
nodes = service.get_nodes_in_graph(bp_path, graph)
result = next((n for n in nodes if str(n.node_type) == 'K2Node_FunctionResult'), None)
assert result, 'Interface return node missing'
getter = next((n for n in nodes if 'Get ThirdPersonMagazineMesh' in str(n.node_title)), None)
if not getter:
    built = service.build_graph(bp_path, graph,
                                [{'ref': 'MagazineComponent', 'type': 'variable_get',
                                  'params': {'variable': 'ThirdPersonMagazineMesh'}}],
                                [], [], False, False)
    assert not built.errors and not built.nodes_failed, str(built)
    getter_id = str(built.ref_to_node_id['MagazineComponent'])
    print('ADDED', bp_path, graph, getter_id)
else:
    getter_id = str(getter.node_id)
    print('EXISTS', getter_id)
result_id = str(result.node_id)
connected = any(str(e.source_node_id) == getter_id and e.source_pin_name == 'ThirdPersonMagazineMesh'
                and str(e.target_node_id) == result_id and e.target_pin_name == 'Mesh'
                for e in service.get_connections(bp_path, graph))
if not connected:
    assert service.connect_nodes(bp_path, graph, getter_id, 'ThirdPersonMagazineMesh',
                                 result_id, 'Mesh'), 'Could not wire magazine getter'
    print('CONNECTED', getter_id, '->', result_id)
bp = unreal.EditorAssetLibrary.load_asset(bp_path)
assert unreal.BlueprintEditorLibrary.compile_blueprint(bp), 'Base weapon compile failed'
assert unreal.EditorAssetLibrary.save_loaded_asset(bp, False), 'Base weapon save failed'
assert any(str(e.source_node_id) == getter_id and e.source_pin_name == 'ThirdPersonMagazineMesh'
           and str(e.target_node_id) == result_id and e.target_pin_name == 'Mesh'
           for e in service.get_connections(bp_path, graph)), 'Readback connection missing'
print('VERIFIED', bp_path, graph, 'returns ThirdPersonMagazineMesh')
