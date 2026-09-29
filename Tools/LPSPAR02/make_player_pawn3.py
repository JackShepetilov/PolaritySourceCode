import unreal, json

SRC = '/Game/Variant_Shooter/Blueprints/BP_ShooterCharacterCheat'
DST = '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterCharacter_AR02_Test'
ABP = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
MESH = '/Game/Variant_Shooter/Tests/LPSP_AR02/SK_TP_CH_AR02_Test'
LOG = unreal.Paths.project_saved_dir() + 'LPSP_AR02/player_pawn.log'
L = []


def log(m):
    L.append(str(m))
    print(m)


log('dst exists: %s' % unreal.EditorAssetLibrary.does_asset_exist(DST))
log('find_asset_data: %s' % unreal.EditorAssetLibrary.find_asset_data(DST))

if not unreal.EditorAssetLibrary.does_asset_exist(DST):
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    original = unreal.load_asset(SRC)
    log('original: %s' % original)
    dup = tools.duplicate_asset('BP_ShooterCharacter_AR02_Test',
                                '/Game/Variant_Shooter/Tests/LPSP_AR02', original)
    log('duplicate: %s' % dup)

bp = unreal.load_asset(DST)
log('bp: %s' % bp)
if bp:
    log('compile: %s' % unreal.BlueprintEditorLibrary.compile_blueprint(bp))
    cls = unreal.EditorAssetLibrary.load_blueprint_class(DST)
    cdo = unreal.get_default_object(cls) if cls else None
    log('class: %s cdo: %s' % (cls, cdo is not None))
    if cdo:
        mesh = cdo.get_editor_property('mesh')
        log('old mesh: %s' % (mesh.get_skinned_asset().get_name() if mesh.get_skinned_asset() else None))
        mesh.set_skinned_asset(unreal.load_asset(MESH))
        anim_cls = unreal.load_class(None, ABP + '.' + ABP.split('/')[-1] + '_C')
        for prop in ('anim_class', 'anim_instance_class'):
            try:
                mesh.set_editor_property(prop, anim_cls)
                log('set %s ok' % prop)
                break
            except Exception as e:
                log('set %s failed: %s' % (prop, e))
        log('new mesh: %s' % mesh.get_skinned_asset().get_name())
        try:
            log('anim class now: %s' % mesh.get_editor_property('anim_class'))
        except Exception as e:
            log('anim read err: %s' % e)
        for prop, value in (('third_person_weapon_socket', 'ik_hand_gun'),):
            try:
                cdo.set_editor_property(prop, value)
                log('%s = %s' % (prop, cdo.get_editor_property(prop)))
            except Exception as e:
                log('%s failed: %s' % (prop, e))
        log('compile again: %s' % unreal.BlueprintEditorLibrary.compile_blueprint(bp))
    log('saved: %s' % unreal.EditorAssetLibrary.save_asset(DST, only_if_is_dirty=False))

open(LOG, 'w', encoding='utf-8').write('\n'.join(L))
