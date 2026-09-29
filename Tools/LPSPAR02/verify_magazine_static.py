import unreal

assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), 'PIE must be stopped'
base = '/Game/Variant_Shooter/Blueprints/Pickups/BP_ShooterWeaponBase'
target = '/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation/Pistols/BP_M1911'
cdo = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(target))
bp = unreal.EditorAssetLibrary.load_asset(target)
print('BP_STATUS', bp.get_editor_property('status'))
print('BASE_STATUS', unreal.EditorAssetLibrary.load_asset(base).get_editor_property('status'))
for graph in ('EventGraph', 'ALPW: Get Droppable Magazine Mesh'):
    try:
        nodes = unreal.BlueprintService.get_nodes_in_graph(base, graph)
        print('BASE_GRAPH', graph, [(str(n.node_title).replace('\n', ' '), str(n.node_id))
                                    for n in nodes if 'Magazine' in str(n.node_title)])
    except Exception as exc:
        print('BASE_GRAPH_ERROR', graph, exc)
print('GETTER_CONNECTIONS', [(str(e.source_node_title), e.source_pin_name,
                               str(e.target_node_title), e.target_pin_name)
                              for e in unreal.BlueprintService.get_connections(
                                  base, 'ALPW: Get Droppable Magazine Mesh')])
mesh = cdo.get_editor_property('ThirdPersonMesh')
print('WEAPON_MESH', mesh, mesh.get_editor_property('skeletal_mesh_asset') if mesh else None)
for name in ('SOCKET_Magazine', 'SOCKET_Magazine_TP'):
    print('COMPONENT_SOCKET', name, mesh.does_socket_exist(name) if mesh else None)
donor = unreal.get_default_object(cdo.get_editor_property('InfimaTPDonorClass'))
handle = donor.get_editor_property('Row Handle Settings Magazine')
print('MAG_ROW', handle.data_table, handle.row_name)
print('DROP_CLASS', cdo.get_editor_property('InfimaTPMagazineDropClass'))
for field in ('ReloadMontageTP', 'SecondaryReloadMontageTP',
              'WeaponMeshReloadAnimationTP', 'WeaponMeshSecondaryReloadAnimationTP'):
    montage = cdo.get_editor_property(field)
    print('MONTAGE', field, montage.get_name() if montage else None)
    if not montage:
        continue
    try:
        events = montage.get_editor_property('notifies')
        print('NOTIFIES', field, [str(e)[:250] for e in events])
    except Exception as exc:
        print('NOTIFIES_ERROR', field, exc)
print('MONTAGE_API', [name for name in dir(unreal.AnimMontageService) if 'notify' in name.lower()])
folder = '/Game/InfimaGames/LowPolyShooterPack/Core/Notifies'
for path in unreal.EditorAssetLibrary.list_assets(folder, False, False):
    if 'Magazine' not in path:
        continue
    asset_path = path.split('.')[0]
    for graph in unreal.BlueprintService.list_graphs(asset_path):
        for node in unreal.BlueprintService.get_nodes_in_graph(asset_path, str(graph.graph_name)):
            title = str(node.node_title).replace('\n', ' ')
            if 'Droppable Magazine' in title or 'Magazine Drop' in title:
                print('PACK_INTERFACE_USE', asset_path, title)
