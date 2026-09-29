import unreal, json

# Test player pawn with the ENEMY's third person body and anim graph, so the slide work in
# ABP_AR02_LPSP_Test is visible on a player watched from the side in multiplayer.
DST = '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterCharacter_AR02_Test'
ABP = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
LOG = unreal.Paths.project_saved_dir() + 'LPSP_AR02/player_pawn.log'
L = []


def log(m):
    L.append(str(m))
    print(m)


ar = unreal.AssetRegistryHelpers.get_asset_registry()
mesh_path = None
src_path = None
for a in ar.get_assets(unreal.ARFilter(class_names=['SkeletalMesh'],
                                       package_paths=['/Game/Variant_Shooter/Tests/LPSP_AR02'],
                                       recursive_paths=True)):
    if str(a.asset_name) == 'SK_TP_CH_AR02_Test':
        mesh_path = str(a.package_name)
for a in ar.get_assets(unreal.ARFilter(class_names=['Blueprint'], package_paths=['/Game'],
                                       recursive_paths=True)):
    n = str(a.asset_name)
    if n in ('BP_ShooterCharacterCheat', 'BP_ShooterCharacter'):
        log('found pawn bp: %s' % a.package_name)
        if n == 'BP_ShooterCharacterCheat' or src_path is None:
            src_path = str(a.package_name)
log('mesh: %s' % mesh_path)
log('source: %s' % src_path)
if not src_path or not mesh_path:
    open(LOG, 'w', encoding='utf-8').write('\n'.join(L))
    raise SystemExit

if not unreal.EditorAssetLibrary.does_asset_exist(DST):
    log('duplicate -> %s' % unreal.EditorAssetLibrary.duplicate_asset(src_path, DST))

cls = unreal.load_class(None, DST + '.' + DST.split('/')[-1] + '_C')
cdo = unreal.get_default_object(cls) if cls else None
log('class %s cdo %s' % (cls, cdo is not None))
if cdo:
    mesh = cdo.get_editor_property('mesh')
    log('old TP mesh: %s' % (mesh.get_skinned_asset().get_name() if mesh.get_skinned_asset() else None))
    mesh.set_skinned_asset(unreal.load_asset(mesh_path))
    anim_cls = unreal.load_class(None, ABP + '.' + ABP.split('/')[-1] + '_C')
    for prop in ('anim_class', 'anim_instance_class'):
        try:
            mesh.set_editor_property(prop, anim_cls)
            log('   set %s ok' % prop)
            break
        except Exception as e:
            log('   set %s failed: %s' % (prop, e))
    log('new TP mesh: %s anim: %s' % (mesh.get_skinned_asset().get_name(),
                                      mesh.get_editor_property('anim_class').get_name()
                                      if mesh.get_editor_property('anim_class') else None))
    for prop, value in (('third_person_weapon_socket', 'ik_hand_gun'),
                        ('left_hand_grip_socket', 'GripPoint_002')):
        try:
            cdo.set_editor_property(prop, value)
            log('%s = %s' % (prop, cdo.get_editor_property(prop)))
        except Exception as e:
            log('%s failed: %s' % (prop, e))

unreal.EditorAssetLibrary.save_asset(DST, only_if_is_dirty=False)
log('saved %s' % DST)
open(LOG, 'w', encoding='utf-8').write('\n'.join(L))
