# Put each imported double (Saved/TPRemodel/imported.json, from ue_import_all.py) on its weapon blueprint.
# Generalised from ue_connect_akx.py: three fields only, with a backup per blueprint:
# - Third Person Mesh = the double;
# - InfimaTPMagazineRowOverride = a row that does not exist, which turns off Infima's separate magazine
#   part (the magazine, box or rocket is in the double now, on the donor's bone; with both on the gun
#   shows two magazines). A gun that already had its own NoPart_ row keeps it;
# - InfimaTPMagazineDropClass = none (nothing falls out: the magazine is part of the mesh).
# The TP kit stays as it is: the mapping tool would put back stock montages the project replaced.
#   g = {}; exec(compile(open(p).read(), p, 'exec'), g); g['main'](['G3'], apply=False)
import unreal, json, os

TOOLS = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), 'Source/Tools/TPDouble/')
SAVED = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()) + 'TPRemodel/'
BACKUPS = '/Game/Variant_Shooter/Tests/LPSP_TP_Backups/'
EAL = unreal.EditorAssetLibrary
ROWS = {'RPG7': 'NoPart_RocketIsInTheMesh'}


def tp_comp(cdo):
    return next(c for c in cdo.get_components_by_class(unreal.SkeletalMeshComponent) if c.get_name() == 'Third Person Mesh')


def state(bp_path):
    cdo = unreal.get_default_object(EAL.load_blueprint_class(bp_path))
    return (tp_comp(cdo).get_editor_property('skeletal_mesh_asset').get_path_name(),
            str(cdo.get_editor_property('InfimaTPMagazineRowOverride')),
            str(cdo.get_editor_property('InfimaTPMagazineDropClass')))


def main(only=None, apply=False):
    assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), 'Stop PIE first'
    jobs = json.load(open(TOOLS + 'jobs.json', encoding='utf-8'))
    imported = json.load(open(SAVED + 'imported.json', encoding='utf-8'))
    for gun, mesh_path in imported.items():
        if only and gun not in only:
            continue
        bp_path = jobs['bp_root'] + jobs['guns'][gun]['bp']
        mesh_now, row_now, drop_now = state(bp_path)
        row = row_now if row_now.startswith('NoPart_') else ROWS.get(gun, 'NoPart_MagIsInTheMesh')
        print('BEFORE', gun, mesh_now, row_now, drop_now)
        print('  PLAN mesh', mesh_path, 'row', row, 'drop None')
        if not apply:
            continue
        backup = BACKUPS + 'BP_%s_BeforeFPDouble' % gun
        if not EAL.does_asset_exist(backup):
            assert EAL.duplicate_asset(bp_path, backup), backup
            print('  CREATED backup', backup)
        cdo = unreal.get_default_object(EAL.load_blueprint_class(bp_path))
        tp = tp_comp(cdo)
        tp.set_editor_property('skeletal_mesh_asset', unreal.load_asset(mesh_path))
        cdo.set_editor_property('InfimaTPMagazineRowOverride', unreal.Name(row))
        cdo.set_editor_property('InfimaTPMagazineDropClass', None)
        bp = EAL.load_asset(bp_path)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        print('  STATUS', bp.get_editor_property('status'), 'SAVED', EAL.save_loaded_asset(bp, False))
        print('AFTER', gun, *state(bp_path))
