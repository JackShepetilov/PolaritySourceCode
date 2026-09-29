import unreal

# Insert a DefaultSlot node into the shared TP AnimBP where the pack's own slots sit: after
# 'Action Standing' / 'Action Aiming' and before 'Overlay Standing' / 'Overlay Aiming'. That is the place
# where a body montage has to win, and it is what lets the pack's DefaultSlot-authored montages
# (_Fire, _Reload_Empty, _Holster, _Unholster) play without project copies.

ABP = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
PAIRS = (('Action Standing', 'Overlay Standing'),
         ('Action Aiming', 'Overlay Aiming'))

assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), 'PIE is running'
BS = unreal.BlueprintService
INFO = {}
for info in BS.get_nodes_in_graph(ABP, GRAPH):
    INFO[str(info.node_title).replace('\n', ' ')] = str(info.node_id)
print('GRAPH_NODES', len(INFO))


def find_id(slot_name):
    for title, node in INFO.items():
        if title.endswith("'%s'" % slot_name):
            return node
    return None


found = BS.discover_nodes(ABP, 'Slot', '', 20)
key = 'SPAWN AnimGraphNode_Slot'
if found:
    for item in found:
        candidate = str(getattr(item, 'spawner_key', ''))
        print('DISCOVERED', candidate, '|', getattr(item, 'display_name', ''))
        if candidate.endswith('|Slot'):
            key = candidate
print('KEY', key)
print('TITLES', [t for t in INFO if 'Standing' in t or 'Aiming' in t][:10])
for index, (source_slot, target_slot) in enumerate(PAIRS):
    source, target = find_id(source_slot), find_id(target_slot)
    print('PAIR', source_slot, source, '->', target_slot, target)
    if not source or not target:
        continue
    created = BS.create_node_by_key(ABP, GRAPH, key, 0, 1500 + index * 200)
    print('CREATED', created)
    assert created, key
    print('SET_SLOT', BS.set_node_pin_value(ABP, GRAPH, created, 'SlotName', 'DefaultSlot'))
    print('BREAK', BS.disconnect_pin(ABP, GRAPH, target, 'Source'))
    print('WIRE_IN', BS.connect_nodes(ABP, GRAPH, source, 'Pose', created, 'Source'))
    print('WIRE_OUT', BS.connect_nodes(ABP, GRAPH, created, 'Pose', target, 'Source'))
    details = BS.get_node_pins(ABP, GRAPH, created)
    for pin in details:
        print('   PIN', pin.get_pin_name(), pin.get_pin_value(), pin.is_connected())
blueprint = unreal.EditorAssetLibrary.load_asset(ABP)
print('COMPILE', unreal.BlueprintEditorLibrary.compile_blueprint(blueprint))
print('SAVE', unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False))
print('STATUS', blueprint.get_editor_property('status'))
print('DONE')
