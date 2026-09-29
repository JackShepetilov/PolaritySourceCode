import unreal
import time

# v2 of the slide probe. Adds the root bone and computes the pelvis/thigh transforms RELATIVE TO ROOT,
# so the live pose can be compared with the clip's own local tracks (pelvis local z = 16.7 in the Loop).
# Phases: crouch run at speed -> slide at the same speed -> stop -> standing slide.
OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/slide_live2.txt'
DUR = 34.0
RTS = unreal.RelativeTransformSpace.RTS_COMPONENT
BONES = ('root', 'Pelvis', 'spine_01', 'Thigh_L', 'Foot_L', 'Hand_R', 'ik_hand_gun')
S = {'h': None, 't0': time.time(), 'last': -1.0, 'done': set()}


def w(line):
    with open(OUT, 'a', encoding='utf-8') as f:
        f.write(line + '\n')


def prop(o, n, d='n/a'):
    try:
        return o.get_editor_property(n)
    except Exception:
        return d


def rel(mesh, bone, base):
    try:
        a = mesh.get_socket_transform(base, RTS)
        b = mesh.get_socket_transform(bone, RTS)
        inv = unreal.MathLibrary.inverse_transform(a)
        return unreal.MathLibrary.compose_transforms(inv, b)
    except Exception:
        return None


def _tick(dt):
    t = time.time() - S['t0']
    if t > DUR:
        unreal.unregister_slate_post_tick_callback(S['h'])
        w('%.1f END' % t)
        return
    wg = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if not wg:
        return
    if S.get('t_world') is None:
        S['t_world'] = t
        w('%.1f WORLD UP' % t)
    tw = t - S['t_world']
    pawn = unreal.GameplayStatics.get_player_pawn(wg, 0)
    if not pawn:
        return
    mov = prop(pawn, 'character_movement')
    mesh = prop(pawn, 'mesh')
    if mov == 'n/a' or mesh == 'n/a':
        return
    fwd = pawn.get_actor_forward_vector()

    # phase 1: crouch run, no slide
    if 2.0 < tw and 'crouch' not in S['done']:
        S['done'].add('crouch')
        try:
            pawn.crouch()
        except Exception as e:
            w('%.1f crouch ERR %s' % (t, e))
        w('%.1f PHASE crouch-run begins' % t)
    if 2.0 < tw < 9.0:
        mov.set_editor_property('velocity', fwd * 1170.0)
    # phase 2: slide at the same speed
    if 9.0 < tw < 9.3 and 'slide1' not in S['done']:
        S['done'].add('slide1')
        w('%.1f PHASE slide1 (crouched) can_slide=%s' % (t, mov.can_slide()))
        mov.start_slide()
    if 9.0 < tw < 17.0:
        mov.set_editor_property('velocity', fwd * 1170.0)
    # phase 3: stand up and slide again
    if 17.0 < tw < 17.3 and 'stand' not in S['done']:
        S['done'].add('stand')
        try:
            pawn.un_crouch()
        except Exception as e:
            w('%.1f uncrouch ERR %s' % (t, e))
        w('%.1f PHASE standing' % t)
    if 17.0 < tw < 20.0:
        mov.set_editor_property('velocity', unreal.Vector(0, 0, 0))
    if 20.0 < tw < 20.3 and 'slide2' not in S['done']:
        S['done'].add('slide2')
        mov.set_editor_property('velocity', fwd * 1170.0)
        w('%.1f PHASE slide2 (standing) can_slide=%s' % (t, mov.can_slide()))
        mov.start_slide()
    if 20.0 < tw < 28.0:
        mov.set_editor_property('velocity', fwd * 1170.0)

    if t - S['last'] < 0.15:
        return
    S['last'] = t
    ai = mesh.get_anim_instance()
    row = ['%.2f' % t, 'spd=%4.0f' % pawn.get_velocity().length(),
           'malpha=%.2f' % mov.get_slide_alpha(), 'mdur=%.2f' % mov.get_slide_duration()]
    for v in ('SlideAlpha', 'Crouching', 'Jumping'):
        val = prop(ai, v)
        if isinstance(val, float):
            val = '%.2f' % val
        row.append('%s=%s' % (v, val))
    tr = {}
    for b in BONES:
        try:
            tr[b] = mesh.get_socket_transform(b, RTS)
        except Exception:
            tr[b] = None
    for b in ('root', 'Pelvis', 'Thigh_L', 'Foot_L', 'Hand_R', 'ik_hand_gun'):
        if tr.get(b):
            row.append('%s_z=%.1f' % (b, tr[b].translation.z))
    pl = rel(mesh, 'Pelvis', 'root')
    tl = rel(mesh, 'Thigh_L', 'root')
    if pl:
        row.append('pelvis_local_z=%.1f' % pl.translation.z)
    if tl:
        row.append('thighLocal_z=%.1f' % tl.translation.z)
    if tr.get('ik_hand_gun') and tr.get('Hand_R'):
        row.append('gunHandR=%.1f' % (tr['ik_hand_gun'].translation - tr['Hand_R'].translation).length())
    w(' '.join(str(x) for x in row))


open(OUT, 'w', encoding='utf-8').write('')
wg = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
if not wg:
    try:
        r = unreal.ToolsetRegistry.execute_tool(
            'EditorToolset.EditorAppToolset', 'StartPIE',
            '{"options": {"bSimulate": false, "playMode": "PlayMode_InViewPort", "warmupSeconds": 3.0}}')
        print('StartPIE is_complete=%s error=%s' % (r.is_complete, r.error))
    except Exception as e:
        print('StartPIE ERR %s' % e)
S['h'] = unreal.register_slate_post_tick_callback(_tick)
print('PIE SLIDE PROBE v2 ARMED -> %s' % OUT)
