# Import every gun's DOUBLE.fbx (build_double.py) as a FRESH skeletal mesh on its donor's skeleton, in
# /Game/Variant_Shooter/Meshes/Weapons_TP/SK_<Donor>_<Gun>, with the first person mesh's materials by
# slot index. Generalised from ue_import_akx.py; the same gotchas hold:
# - fresh import, never over a copy of the donor (a copy keeps its old slots next to the new ones);
# - a rebuild deletes only its own mesh, and only when nothing references it;
# - old FBX importer (Interchange turns SOCKET_* bones into sockets and drops the prefix);
# - the donor skeleton must not come out dirty.
# A name already taken by a referenced mesh (MGX5's remodel) gets _Double.
#   g = {}; exec(compile(open(p).read(), p, 'exec'), g); g['main'](['G3', 'MX16A4'])
import unreal, json, os

TOOLS = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), 'Source/Tools/TPDouble/')
SAVED = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()) + 'TPRemodel/'
DEST_DIR = '/Game/Variant_Shooter/Meshes/Weapons_TP'
EAL = unreal.EditorAssetLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()


def refs(path):
    return [str(r) for r in AR.get_referencers(path, unreal.AssetRegistryDependencyOptions()) if str(r) != path.rsplit('.', 1)[0]]


def comp(cdo, name):
    return next(c for c in cdo.get_components_by_class(unreal.SkeletalMeshComponent) if c.get_name() == name)


def dest_name(gun, donor):
    name = '%s_%s' % (donor.split('/')[-1], gun.replace('-', ''))
    path = DEST_DIR + '/' + name
    if EAL.does_asset_exist(path) and refs(path) and not EAL.does_asset_exist(path + '_Double'):
        # Taken by something in use that is not ours from a previous run (MGX5's remodel).
        marker = EAL.get_metadata_tag(EAL.load_asset(path), 'TPDouble')
        if marker != gun:
            return name + '_Double'
    return name


def assign_materials(gun, mesh, fp):
    # The double's material i in Blender is the FP slot i (build_double.py keeps the order), and its name
    # is in build.log. UE drops slots no face uses (SVD: 15 became 10) and turns '.' into '_', so the
    # match goes by name, case-insensitive; by index only when the counts agree and no log is there.
    src, mats = list(fp.materials), list(mesh.materials)
    blender = []
    log = SAVED + gun + '/build.log'
    if os.path.exists(log):
        line = next((l for l in open(log, encoding='utf-8', errors='replace') if l.startswith('MATS ')), '')
        blender = [n.replace('.', '_').lower() for n in eval(line[5:])] if line else []
    pairs = []
    for i, m in enumerate(mats):
        name = str(m.material_slot_name).lower()
        k = blender.index(name) if name in blender else (i if len(mats) == len(src) else -1)
        mi = src[k].material_interface if 0 <= k < len(src) else None
        m.material_interface = mi
        mats[i] = m
        pairs.append((str(m.material_slot_name), k, mi.get_name() if mi else None))
    mesh.set_editor_property('materials', mats)
    return pairs


def import_one(gun, job, jobs, world):
    donor = EAL.load_asset(jobs['infima'] + job['donor'])
    skel = donor.skeleton
    name = dest_name(gun, job['donor'])
    dest = DEST_DIR + '/' + name
    if EAL.does_asset_exist(dest):
        r = refs(dest)
        assert not r, '%s referenced, will not delete: %s' % (dest, r)
        print('DELETED', dest, EAL.delete_asset(dest))

    ui = unreal.FbxImportUI()
    ui.import_mesh = True
    ui.import_as_skeletal = True
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    ui.skeleton = skel
    ui.import_materials = False
    ui.import_textures = False
    ui.import_animations = False
    ui.create_physics_asset = False
    task = unreal.AssetImportTask()
    task.filename = SAVED + gun + '/DOUBLE.fbx'
    task.destination_path = DEST_DIR
    task.destination_name = name
    task.replace_existing = True
    task.automated = True
    task.save = False
    task.options = ui
    unreal.SystemLibrary.execute_console_command(world, 'Interchange.FeatureFlags.Import.FBX 0')
    try:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    finally:
        unreal.SystemLibrary.execute_console_command(world, 'Interchange.FeatureFlags.Import.FBX 1')

    mesh = EAL.load_asset(dest)
    assert mesh, 'import failed: ' + dest
    assert mesh.skeleton == skel, 'wrong skeleton %s' % mesh.skeleton
    cdo = unreal.get_default_object(EAL.load_blueprint_class(jobs['bp_root'] + job['bp']))
    fp = comp(cdo, 'First Person Mesh').get_editor_property('skeletal_mesh_asset')
    pairs = assign_materials(gun, mesh, fp)
    src, mats = list(fp.materials), list(mesh.materials)
    EAL.set_metadata_tag(mesh, 'TPDouble', gun)
    c = unreal.SkeletalMeshComponent(); c.set_skeletal_mesh_asset(mesh)
    bones = [str(c.get_bone_name(i)) for i in range(c.get_num_bones())]
    c2 = unreal.SkeletalMeshComponent(); c2.set_skeletal_mesh_asset(donor)
    dbones = [str(c2.get_bone_name(i)) for i in range(c2.get_num_bones())]
    mesh.modify()
    saved = EAL.save_asset(dest, only_if_is_dirty=False)
    print('IMPORTED %s -> %s slots %d/%d bones_match %s saved %s' % (gun, dest, len(mats), len(src), bones == dbones, saved))
    if len(mats) != len(src) or bones != dbones:
        print('  CHECK slots', pairs, 'bones', bones, 'donor', dbones)
    print('  BOUNDS', mesh.get_bounds().box_extent, 'donor', donor.get_bounds().box_extent)
    return dest


def main(only=None):
    assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), 'Stop PIE first'
    jobs = json.load(open(TOOLS + 'jobs.json', encoding='utf-8'))
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    before = set(p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    out = {}
    for gun, job in jobs['guns'].items():
        if only and gun not in only:
            continue
        out[gun] = import_one(gun, job, jobs, world)
    after = set(p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    print('DIRTY before', sorted(before), 'new', sorted(after - before))
    done = {}
    if os.path.exists(SAVED + 'imported.json'):
        done = json.load(open(SAVED + 'imported.json', encoding='utf-8'))
    done.update(out)
    json.dump(done, open(SAVED + 'imported.json', 'w', encoding='utf-8'), indent=1)
    return out
