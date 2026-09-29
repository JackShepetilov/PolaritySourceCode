import unreal
import time

# Arms a sampler that drives the test pawn itself and writes every sample straight to disk, so the
# numbers can be read while PIE is still running. Three phases for comparison:
#   idle -> slide -> crouch-walk -> slide again
# For each sample: pelvis height, leg pose, and the distance between the gun bone and the hand bone
# (a broken grip shows up as that distance growing).
OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/slide_live.txt'
DUR = 30.0
BONES = ('Pelvis', 'Thigh_L', 'Foot_L', 'Hand_R', 'ik_hand_gun', 'Hand_L', 'ik_hand_l')
RTS = unreal.RelativeTransformSpace.RTS_COMPONENT
S = {'h': None, 't0': time.time(), 'last': -1.0, 'done': set()}


def w(line):
    with open(OUT, 'a', encoding='utf-8') as f:
        f.write(line + '\n')


def prop(o, n, d='n/a'):
    try:
        return o.get_editor_property(n)
    except Exception:
        return d


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

    if 2.0 < tw < 2.3 and 'slide1' not in S['done']:
        S['done'].add('slide1')
        try:
            mov.set_editor_property('velocity', fwd * 750.0)
            w('%.1f DRIVE slide1 can_slide=%s' % (t, mov.can_slide()))
            mov.start_slide()
        except Exception as e:
            w('%.1f DRIVE slide1 ERR %s' % (t, e))
    if 11.0 < tw < 11.3 and 'stop' not in S['done']:
        S['done'].add('stop')
        mov.set_editor_property('velocity', unreal.Vector(0, 0, 0))
        w('%.1f DRIVE stop' % t)
    if 13.0 < tw < 13.3 and 'crouchwalk' not in S['done']:
        S['done'].add('crouchwalk')
        try:
            pawn.crouch()
        except Exception as e:
            w('%.1f DRIVE crouch ERR %s' % (t, e))
        mov.set_editor_property('velocity', fwd * 300.0)
        w('%.1f DRIVE crouch walk 300' % t)
    if 17.0 < tw < 17.3 and 'slide2' not in S['done']:
        S['done'].add('slide2')
        try:
            mov.set_editor_property('velocity', fwd * 750.0)
            w('%.1f DRIVE slide2 can_slide=%s crouched=%s' % (t, mov.can_slide(), prop(pawn, 'is_crouched')))
            mov.start_slide()
        except Exception as e:
            w('%.1f DRIVE slide2 ERR %s' % (t, e))

    if t - S['last'] < 0.1:
        return
    S['last'] = t
    ai = mesh.get_anim_instance()
    try:
        mz = mesh.get_world_transform().translation.z
    except Exception:
        mz = -1.0
    row = ['%.2f' % t, 'spd=%4.0f' % pawn.get_velocity().length(),
           'z=%.1f' % mz,
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
        except Exception as e:
            tr[b] = None
            row.append('%s=ERR' % b)
    for b in BONES:
        if tr.get(b):
            row.append('%s=(%.1f,%.1f,%.1f)' % (b, tr[b].translation.x, tr[b].translation.y, tr[b].translation.z))
    if tr.get('ik_hand_gun') and tr.get('Hand_R'):
        row.append('gun-HandR=%.1f' % (tr['ik_hand_gun'].translation - tr['Hand_R'].translation).length())
    if tr.get('ik_hand_l') and tr.get('Hand_L'):
        row.append('ikhandL-HandL=%.1f' % (tr['ik_hand_l'].translation - tr['Hand_L'].translation).length())
    w(' '.join(str(x) for x in row))


open(OUT, 'w', encoding='utf-8').write('')
try:
    r = unreal.ToolsetRegistry.execute_tool(
        'EditorToolset.EditorAppToolset', 'StartPIE',
        '{"options": {"bSimulate": false, "playMode": "PlayMode_InViewPort", "warmupSeconds": 3.0}}')
    print('StartPIE is_complete=%s error=%s' % (r.is_complete, r.error))
except Exception as e:
    print('StartPIE ERR %s' % e)
S['h'] = unreal.register_slate_post_tick_callback(_tick)
print('SLIDE PROBE ARMED -> %s' % OUT)
