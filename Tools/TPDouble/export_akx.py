import unreal
OUT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()) + 'TPRemodel/AKX203/'
JOBS = {'/FPSAnimationPack/Meshes/Weapons/AKX200/SKM_AKX200': 'SKM_AKX200.fbx',
        '/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Weapons/ARs/SK_AR_01': 'SK_AR_01.fbx'}
for path, name in JOBS.items():
    t = unreal.AssetExportTask()
    t.object = unreal.EditorAssetLibrary.load_asset(path)
    t.filename = OUT + name
    t.automated = True
    t.prompt = False
    t.replace_identical = True
    t.options = unreal.FbxExportOption()
    print('EXPORT', name, unreal.Exporter.run_asset_export_task(t))
for path in JOBS:
    m = unreal.EditorAssetLibrary.load_asset(path)
    print('MESH', path, 'skel', m.skeleton.get_path_name(), 'bounds', m.get_bounds().origin, m.get_bounds().box_extent)
    print('  MATS', [(str(s.material_slot_name), s.material_interface.get_path_name() if s.material_interface else None) for s in m.materials])
    for i in range(m.num_sockets()):
        so = m.get_socket_by_index(i)
        print('  SOCKET', so.socket_name, so.bone_name, so.relative_location, so.relative_rotation, 'outer', so.get_outer().get_name())
cdo = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class('/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation/ARs/BP_AKX203'))
fp = cdo.get_editor_property('first_person_mesh') if False else None
print('DONE')
