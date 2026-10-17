# Every gun in jobs.json: its first person mesh (read from the blueprint) and its Infima donor mesh to
# Saved/TPRemodel/<Gun>/ as FBX, plus a report of skeletons, slots, sockets and component scales.
# LOD0 only: the FP meshes carry LODs and a LOD group would import as several mesh objects.
import unreal, json, os

TOOLS = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), 'Source/Tools/TPDouble/')
OUT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()) + 'TPRemodel/'
EAL = unreal.EditorAssetLibrary


def export(obj, path):
    t = unreal.AssetExportTask()
    t.object = obj
    t.filename = path
    t.automated = True
    t.prompt = False
    t.replace_identical = True
    o = unreal.FbxExportOption()
    o.level_of_detail = False
    o.collision = False
    t.options = o
    return unreal.Exporter.run_asset_export_task(t)


def comp(cdo, name):
    return next((c for c in cdo.get_components_by_class(unreal.SkeletalMeshComponent) if c.get_name() == name), None)


def main(only=None):
    jobs = json.load(open(TOOLS + 'jobs.json', encoding='utf-8'))
    report = {}
    for gun, j in jobs['guns'].items():
        if only and gun not in only:
            continue
        d = OUT + gun + '/'
        os.makedirs(d, exist_ok=True)
        cdo = unreal.get_default_object(EAL.load_blueprint_class(jobs['bp_root'] + j['bp']))
        fp, tp = comp(cdo, 'First Person Mesh'), comp(cdo, 'Third Person Mesh')
        fpm = fp.get_editor_property('skeletal_mesh_asset')
        dm = EAL.load_asset(jobs['infima'] + j['donor'])
        r = {'fp_mesh': fpm.get_path_name(), 'fp_scale': str(fp.get_editor_property('relative_scale3d')),
             'tp_mesh_now': tp.get_editor_property('skeletal_mesh_asset').get_path_name(),
             'tp_scale': str(tp.get_editor_property('relative_scale3d')), 'donor': dm.get_path_name(),
             'donor_skel': dm.skeleton.get_path_name(),
             'row': str(cdo.get_editor_property('InfimaTPMagazineRowOverride')),
             'drop': str(cdo.get_editor_property('InfimaTPMagazineDropClass'))}
        r['export_fp'] = export(fpm, d + 'FP.fbx')
        r['export_donor'] = export(dm, d + 'DONOR.fbx')
        for key, m in (('fp', fpm), ('donor', dm)):
            r[key + '_mats'] = [str(s.material_slot_name) for s in m.materials]
            r[key + '_lods'] = m.get_num_lods() if hasattr(m, 'get_num_lods') else -1
            socks = []
            for i in range(m.num_sockets()):
                s = m.get_socket_by_index(i)
                socks.append([str(s.socket_name), str(s.bone_name), str(s.relative_location), s.get_outer().get_name()])
            r[key + '_sockets'] = socks
        report[gun] = r
        print('EXPORTED', gun, r['export_fp'], r['export_donor'])
    json.dump(report, open(OUT + 'export_report.json', 'w', encoding='utf-8'), indent=1)
    print('REPORT', OUT + 'export_report.json')


main()
