import unreal, json

# Step 3.2 correction: LPSP's Root Yaw Moving Smooth is in the MESH frame (mesh yaw = actor yaw - 90).
# PIE samples 2026-09-27 showed Movement Forward/Right rotated by 90 degrees with actor rotation.
# Replace Owner->K2_GetActorRotation with GetOwningComponent->K2_GetComponentRotation in
# Get Forward Vector / Get Right Vector. Idempotent: skips a graph that already has GetComponentRotation.

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
BS = unreal.BlueprintService


def t(n):
    return n.node_title.replace('\r', '').split('\n')[0]


def fix(g):
    nodes = BS.get_nodes_in_graph(AB, g)
    if any(t(n) == 'Get World Rotation' or 'ComponentRotation' in n.node_title.replace(' ', '') for n in nodes):
        print('ALREADY:', g)
        return
    old = [n for n in nodes if t(n) == 'Get Actor Rotation'][0]
    own = [n for n in nodes if t(n) == 'Get Owner'][0]
    edges = [c for c in BS.get_connections(AB, g) if c.source_node_id == old.node_id and c.source_pin_name == 'ReturnValue']
    assert len(edges) == 1, edges
    e = edges[0]
    r = BS.build_graph(AB, g,
                       [{'ref': 'Comp', 'type': 'function_call', 'params': {'class': 'AnimInstance', 'function': 'GetOwningComponent'}},
                        {'ref': 'Rot', 'type': 'function_call', 'params': {'class': 'SceneComponent', 'function': 'K2_GetComponentRotation'}}],
                       [{'from_': 'Comp.ReturnValue', 'to': 'Rot.self'},
                        {'from_': 'Rot.ReturnValue', 'to': e.target_node_id + '.' + e.target_pin_name}],
                       [], False, False)
    print('BUILD', g, r.nodes_created, r.connections_made, r.nodes_failed, r.connections_failed, list(r.errors))
    if r.nodes_failed == 0 and r.connections_failed == 0:
        print('DELETED old', BS.delete_node(AB, g, old.node_id), BS.delete_node(AB, g, own.node_id))


for g in ['Get Forward Vector', 'Get Right Vector']:
    fix(g)
r = unreal.ToolsetRegistry.execute_tool('editor_toolset.toolsets.blueprint.BlueprintTools', 'compile_blueprint',
                                        json.dumps({'blueprint': {'refPath': AB + '.ABP_AR02_LPSP_Test'}}))
print('compile', r.is_complete, r.error, 'saved', unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
for g in ['Get Forward Vector', 'Get Right Vector']:
    for c in BS.get_connections(AB, g):
        if 'Looking' in c.source_node_title or 'Is Valid' in c.source_node_title:
            continue
        print('  C', g, t(type('x', (), {'node_title': c.source_node_title})), c.source_pin_name, '->', c.target_node_title.split('\n')[0][:28], c.target_pin_name)
