import unreal, json

DST = '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterCharacter_AR02_Test'
ABP = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
MESH = '/Game/Variant_Shooter/Tests/LPSP_AR02/SK_TP_CH_AR02_Test'
LOG = unreal.Paths.project_saved_dir() + 'LPSP_AR02/player_pawn.log'
L = []


def log(m):
    L.append(str(m))
    print(m)


bp = unreal.load_asset(DST)
log('bp: %s' % bp)
cls = unreal.EditorAssetLibrary.load_blueprint_class(DST)
cdo = unreal.get_default_object(cls) if cls else None
log('cdo: %s' % (cdo is not None))

if cdo:
    mesh = cdo.get_editor_property('mesh')
    log('mesh comp: %s' % mesh.get_name())
    log('mesh asset methods: %s' % [m for m in dir(mesh) if 'mesh' in m.lower() and 'set' in m.lower()][:12])
    asset = unreal.load_asset(MESH)
    done = False
    for call in ('set_skeletal_mesh', 'set_skinned_asset', 'set_skeletal_mesh_asset'):
        if hasattr(mesh, call):
            try:
                getattr(mesh, call)(asset)
                log('   %s(%s) ok' % (call, MESH))
                done = True
                break
            except Exception as e:
                log('   %s failed: %s' % (call, e))
    if not done:
        for prop in ('skeletal_mesh_asset', 'skeletal_mesh', 'skinned_asset'):
            try:
                mesh.set_editor_property(prop, asset)
                log('   set_editor_property(%s) ok' % prop)
                done = True
                break
            except Exception as e:
                log('   set_editor_property(%s) failed: %s' % (prop, e))
    try:
        log('mesh asset now: %s' % mesh.get_editor_property('skeletal_mesh_asset').get_name())
    except Exception as e:
        log('mesh read err: %s' % e)

    anim_cls = unreal.load_class(None, ABP + '.' + ABP.split('/')[-1] + '_C')
    for prop in ('anim_class', 'anim_instance_class', 'animation_blueprint'):
        try:
            mesh.set_editor_property(prop, anim_cls)
            log('   set %s ok' % prop)
            break
        except Exception as e:
            log('   set %s failed: %s' % (prop, e))
    try:
        log('anim class now: %s' % mesh.get_editor_property('anim_class'))
    except Exception as e:
        log('anim read err: %s' % e)

    try:
        cdo.set_editor_property('third_person_weapon_socket', 'ik_hand_gun')
        log('socket: %s' % cdo.get_editor_property('third_person_weapon_socket'))
    except Exception as e:
        log('socket failed: %s' % e)

    log('compile: %s' % unreal.BlueprintEditorLibrary.compile_blueprint(bp))
    log('saved: %s' % unreal.EditorAssetLibrary.save_asset(DST, only_if_is_dirty=False))

open(LOG, 'w', encoding='utf-8').write('\n'.join(L))
