# Import SK_AR_01_AKX203 (the AKX200 double on SKEL_AR_01) as a FRESH mesh: reimporting over a copy of
# SK_AR_01 kept its six slots next to the seven new ones. Nothing references it yet, so a rebuild is safe.
import unreal

SKELETON = '/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Weapons/ARs/SKEL_AR_01'
DEST_DIR = '/Game/Variant_Shooter/Meshes/Weapons_TP'
DEST_NAME = 'SK_AR_01_AKX203'
FBX = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()) + 'TPRemodel/AKX203/SK_AR_01_AKX203.fbx'
MAT_SRC = '/FPSAnimationPack/Meshes/Weapons/AKX200/SKM_AKX200'
EAL = unreal.EditorAssetLibrary


def main():
    assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), 'Stop PIE first'
    dest = DEST_DIR + '/' + DEST_NAME
    if EAL.does_asset_exist(dest):
        refs = unreal.AssetRegistryHelpers.get_asset_registry().get_referencers(dest, unreal.AssetRegistryDependencyOptions())
        assert not refs, 'referenced, will not delete: %s' % refs
        print('DELETED', dest, EAL.delete_asset(dest))

    ui = unreal.FbxImportUI()
    ui.import_mesh = True
    ui.import_as_skeletal = True
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    ui.skeleton = unreal.load_asset(SKELETON)
    ui.import_materials = False
    ui.import_textures = False
    ui.import_animations = False
    ui.create_physics_asset = False
    task = unreal.AssetImportTask()
    task.filename = FBX
    task.destination_path = DEST_DIR
    task.destination_name = DEST_NAME
    task.replace_existing = True
    task.automated = True
    task.save = False
    task.options = ui
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    unreal.SystemLibrary.execute_console_command(world, 'Interchange.FeatureFlags.Import.FBX 0')
    try:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    finally:
        unreal.SystemLibrary.execute_console_command(world, 'Interchange.FeatureFlags.Import.FBX 1')
    print('IMPORTED', list(task.imported_object_paths))

    mesh = EAL.load_asset(dest)
    print('SKELETON', mesh.skeleton.get_path_name() if mesh.skeleton else None)
    print('BOUNDS', mesh.get_bounds().origin, mesh.get_bounds().box_extent)
    src = EAL.load_asset(MAT_SRC).materials
    mats = list(mesh.materials)
    for i in range(min(len(mats), len(src))):
        m = mats[i]
        m.material_interface = src[i].material_interface
        mats[i] = m
    mesh.set_editor_property('materials', mats)
    print('MATS', [(str(s.material_slot_name), s.material_interface.get_name() if s.material_interface else None) for s in mesh.materials])
    c = unreal.SkeletalMeshComponent(); c.set_skeletal_mesh_asset(mesh)
    print('BONES', [str(c.get_bone_name(i)) for i in range(c.get_num_bones())])
    print('SKEL DIRTY', [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()])
    mesh.modify()
    print('SAVED', EAL.save_asset(dest, only_if_is_dirty=False))


main()
