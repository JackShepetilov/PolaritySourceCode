import json

import unreal

# Implement the pack's magazine interface once in the base weapon Blueprint, so every weapon inherits it
# and no per-weapon graph work is needed. The events only forward to the C++ helpers.

BP = '/Game/Variant_Shooter/Blueprints/Pickups/BP_ShooterWeaponBase'
GRAPH = 'EventGraph'
EVENTS = (
    ('VisibilityEvent', 'EVENT BPI_ALPW_Droppable_Magazine_Target_C::ALPW: Set Magazine Visibility',
     'CallSetVisible', 'SetMagazineVisible'),
    ('DropEvent', 'EVENT BPI_ALPW_Droppable_Magazine_Target_C::ALPW: Drop Magazine',
     'CallDrop', 'DropMagazineProp'),
)

assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), 'PIE is running'
BS = unreal.BlueprintService
IFACE = '/Game/InfimaGames/AnimatedLowPolyWeapons/Core/BPI_ALPW_Droppable_Magazine_Target'

# The interface has to be on the class before its event nodes can be placed: discovery finds nothing
# while the base Blueprint does not implement it.
print('ADD_IFACE', BS.add_interface(BP, IFACE))

# What the toolset offers for interfaces, in case the interface has to be added explicitly.
schema = unreal.ToolsetRegistry.get_all_toolset_json_schemas()
print('INTERFACE_TOOLS', [name for name in ('add_interface', 'implement_interface', 'add_interface_function')
                          if name in str(schema)])

ids = {}
for event_ref, event_key, call_ref, function_name in EVENTS:
    existing = [info for info in BS.get_nodes_in_graph(BP, GRAPH)
                if str(info.node_title).replace('\n', ' ').startswith('Event ' + event_key.split('::')[-1])]
    if existing:
        print('EVENT_ALREADY', event_ref, str(existing[0].node_id))
        ids[event_ref] = str(existing[0].node_id)
    else:
        message = event_key.split('::')[-1]
        found = BS.discover_nodes(BP, message, '', 20)
        print('DISCOVERED', [(str(getattr(item, 'spawner_key', '')), str(getattr(item, 'display_name', '')))
                             for item in found][:8])
        key = event_key
        for item in found:
            candidate = str(getattr(item, 'spawner_key', ''))
            if candidate.startswith('EVENT') and message in candidate:
                key = candidate
                break
        print('KEY', key)
        event_id = BS.create_node_by_key(BP, GRAPH, key, 600, 900)
        print('EVENT', event_ref, event_id)
        assert event_id, key
        ids[event_ref] = event_id

    built = BS.build_graph(BP, GRAPH, [{'ref': call_ref, 'type': 'function_call',
                                        'params': {'class': 'ShooterWeapon', 'function': function_name}}],
                           [], [], False, False)
    print('BUILD', call_ref, built.errors, built.nodes_failed, dict(built.ref_to_node_id))
    ids.update(dict(built.ref_to_node_id))

print('IDS', json.dumps(ids, default=str))
for event_ref, event_key, call_ref, function_name in EVENTS:
    print('WIRE_EXEC', event_ref, BS.connect_nodes(BP, GRAPH, ids[event_ref], 'then',
                                                   ids[call_ref], 'execute'))
print('WIRE_BOOL', BS.connect_nodes(BP, GRAPH, ids['VisibilityEvent'], 'Visibility',
                                    ids['CallSetVisible'], 'bVisible'))

blueprint = unreal.EditorAssetLibrary.load_asset(BP)
print('COMPILE', unreal.BlueprintEditorLibrary.compile_blueprint(blueprint))
print('SAVE', unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False))
print('STATUS', blueprint.get_editor_property('status'))
for info in BS.get_nodes_in_graph(BP, GRAPH):
    title = str(info.node_title).replace('\n', ' ')
    if 'ALPW' in title or 'Magazine' in title:
        print('NODE', str(info.node_id), '|', title)
print('DONE')
