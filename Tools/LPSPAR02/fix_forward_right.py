import unreal, json

# Step 3.2: Get Forward Vector / Get Right Vector read the owner's actor rotation instead of
# BPSC_LPSP_TP_Looks.RootYawMovingSmooth (our NPC has no such component, so both returned zero and
# Movement Forward/Right/Backward/Left stayed 0). Entry is wired straight to the valid Return node,
# the Is Valid gate is left unconnected. Idempotent: skips a graph that already has K2_GetActorRotation.

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
BS = unreal.BlueprintService


def title(n):
    return n.node_title.replace('\r', '').split('\n')[0]


def fix(g, rot_target_title, rot_pin):
    nodes = BS.get_nodes_in_graph(AB, g)
    if any('GetActorRotation' in n.node_title.replace(' ', '') for n in nodes):
        print('ALREADY:', g)
        return
    entry = [n for n in nodes if title(n) == g][0]
    results = [n for n in nodes if title(n) == 'Return Node']
    valid = [r for r in results if any(p.pin_name == 'ReturnValue' and p.is_connected for p in BS.get_node_pins(AB, g, r.node_id))][0]
    tgt = [n for n in nodes if title(n) == rot_target_title][0]
    r = BS.build_graph(AB, g,
                       [{'ref': 'Own', 'type': 'variable_get', 'params': {'variable': 'Owner'}},
                        {'ref': 'Rot', 'type': 'function_call', 'params': {'class': 'Actor', 'function': 'K2_GetActorRotation'}}],
                       [{'from_': 'Own.Owner', 'to': 'Rot.self'},
                        {'from_': 'Rot.ReturnValue', 'to': tgt.node_id + '.' + rot_pin}],
                       [], False, False)
    print('BUILD', g, r.nodes_created, r.connections_made, r.nodes_failed, r.connections_failed, list(r.errors))
    BS.disconnect_pin(AB, g, entry.node_id, 'then')
    print('EXEC', g, BS.connect_nodes(AB, g, entry.node_id, 'then', valid.node_id, 'execute'))


fix('Get Forward Vector', 'Get Rotation X Vector', 'InRot')
fix('Get Right Vector', 'LPSP - Add Rotators', 'A')
r = unreal.ToolsetRegistry.execute_tool('editor_toolset.toolsets.blueprint.BlueprintTools', 'compile_blueprint',
                                        json.dumps({'blueprint': {'refPath': AB + '.ABP_AR02_LPSP_Test'}}))
print('compile', r.is_complete, r.error)
print('saved', unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
for g in ['Get Forward Vector', 'Get Right Vector']:
    for c in BS.get_connections(AB, g):
        print('  C', g, c.source_node_title.split('\n')[0][:30], c.source_pin_name, '->', c.target_node_title.split('\n')[0][:30], c.target_pin_name)
