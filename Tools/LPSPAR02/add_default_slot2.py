import unreal

# Insert a DefaultSlot node between every 'Action ...' slot and its 'Overlay ...' consumer in the main
# AnimGraph, so the pack's DefaultSlot-authored montages play on the body without project copies.
# Node ids come from get_nodes_in_graph, see node_titles_probe.py.

ABP = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
WIRES = (('904821CA475BE88FC7B07FB8FC6006E7', 'Action Standing', '68B01A5442534F231AEBC8806A6B0CEB'),
         ('023A2FF2479FE2049B7147B0F91964B5', 'Action Aiming', '6E304C1D45234617D4D285B71EF6CD48'))
KEY = 'NODE AnimGraphNode_Slot'

assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), 'PIE is running'
BS = unreal.BlueprintService
created_ids = []
for index, (source_id, label, target_id) in enumerate(WIRES):
    created = BS.create_node_by_key(ABP, GRAPH, KEY, 0, 1700 + index * 250)
    print('CREATED', label, created)
    assert created, label
    print('  CONFIGURE', BS.configure_node(ABP, GRAPH, created, 'SlotName', 'DefaultSlot'))
    print('  BREAK_OVERLAY_IN', BS.disconnect_pin(ABP, GRAPH, target_id, 'Source'))
    print('  WIRE_SOURCE', BS.connect_nodes(ABP, GRAPH, source_id, 'Pose', created, 'Source'))
    print('  WIRE_TARGET', BS.connect_nodes(ABP, GRAPH, created, 'Pose', target_id, 'Source'))
    created_ids.append(created)

for node_id in created_ids:
    for pin in BS.get_node_pins(ABP, GRAPH, node_id):
        print('PIN', pin.get_pin_name(), pin.get_pin_value(), pin.is_connected())

blueprint = unreal.EditorAssetLibrary.load_asset(ABP)
print('COMPILE', unreal.BlueprintEditorLibrary.compile_blueprint(blueprint))
print('SAVE', unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False))
print('STATUS', blueprint.get_editor_property('status'))
print('DONE')
