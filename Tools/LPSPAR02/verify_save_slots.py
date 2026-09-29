import unreal

# Verify the two DefaultSlot nodes are in place and wired, then compile and save the AnimBP.

ABP = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
BS = unreal.BlueprintService
OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/slot_chain_after.txt'

assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), 'PIE is running'

nodes = BS.get_nodes_in_graph(ABP, GRAPH)
ids = {}
for info in nodes:
    title = str(info.node_title).replace('\n', ' ')
    if 'Slot' in str(info.node_type):
        ids[title] = str(info.node_id)
        print('SLOT_NODE', str(info.node_id), '|', title)

lines = []
for info in nodes:
    title = str(info.node_title).replace('\n', ' ')
    if not title.endswith("'Action Standing'") and not title.endswith("'Action Aiming'") \
            and not title.endswith("'Overlay Standing'") and not title.endswith("'Overlay Aiming'") \
            and 'DefaultSlot' not in title:
        continue
    node_id = str(info.node_id)
    lines.append('NODE %s | %s' % (node_id, title))
    for pin in BS.get_node_pins(ABP, GRAPH, node_id):
        lines.append('   %-12s %-10s connected=%s' % (pin.pin_name, pin.pin_type, pin.is_connected))
with open(OUT, 'w', encoding='utf-8') as handle:
    handle.write('\n'.join(lines))
print('CHAIN_WRITTEN', len(lines))

blueprint = unreal.EditorAssetLibrary.load_asset(ABP)
print('COMPILE', unreal.BlueprintEditorLibrary.compile_blueprint(blueprint))
print('SAVE', unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False))
print('STATUS', blueprint.get_editor_property('status'))
print('DONE')
