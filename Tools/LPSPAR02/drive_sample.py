import unreal, time

# Writes every sample straight to disk (so it can be read while PIE runs), and drives the local pawn
# itself: sprint-speed velocity + slide, then a jump, then another slide. 300 s window.
OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/player_vars.txt'
DUR = 300.0
VARS = ['SlideAlpha', 'CrouchAlpha', 'Crouching', 'Jumping', 'Aiming', 'Look Offset', 'Pitch',
        'Is Moving', 'Running', 'Tactical Sprinting']
S = {'h': None, 't0': time.time(), 'last': -1.0, 'done': set()}


def prop(o, n):
    try:
        return o.get_editor_property(n)
    except Exception:
        return 'n/a'


def w(line):
    with open(OUT, 'a', encoding='utf-8') as f:
        f.write(line + '\n')


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
    if mov == 'n/a':
        return

    # --- drive: slide at +2 s, jump at +6 s, slide again at +10 s (relative to the PIE start) ---
    if 2.0 < tw < 2.3 and 'slide1' not in S['done']:
        S['done'].add('slide1')
        try:
            pawn.set_actor_location(pawn.get_actor_location() + unreal.Vector(0, 0, 60), False, True)
            mov.set_editor_property('velocity', unreal.Vector(750.0, 0.0, 0.0))
            w('%.1f DRIVE set velocity 750, can_slide=%s' % (t, mov.can_slide()))
            mov.start_slide()
            w('%.1f DRIVE start_slide called' % t)
        except Exception as e:
            w('%.1f DRIVE slide err %s' % (t, e))
    if 6.0 < tw < 6.3 and 'jump' not in S['done']:
        S['done'].add('jump')
        try:
            w('%.1f DRIVE jump, can_slide=%s' % (t, mov.can_slide()))
            pawn.jump()
        except Exception as e:
            w('%.1f DRIVE jump err %s' % (t, e))
    if 10.0 < tw < 10.3 and 'slide2' not in S['done']:
        S['done'].add('slide2')
        try:
            mov.set_editor_property('velocity', unreal.Vector(750.0, 0.0, 0.0))
            w('%.1f DRIVE slide2 can_slide=%s' % (t, mov.can_slide()))
            mov.start_slide()
        except Exception as e:
            w('%.1f DRIVE slide2 err %s' % (t, e))

    if t - S['last'] < 0.1:
        return
    S['last'] = t
    mesh = prop(pawn, 'mesh')
    ai = mesh.get_anim_instance() if mesh != 'n/a' else None
    row = ['%.2f' % t, 'spd=%4.0f' % pawn.get_velocity().length(),
           'alpha=%.2f' % mov.get_slide_alpha(), 'dur=%.2f' % mov.get_slide_duration(),
           'anim=%s' % (ai.get_class().get_name() if ai else None)]
    if ai:
        for v in VARS:
            val = prop(ai, v)
            if isinstance(val, float):
                val = '%.2f' % val
            row.append('%s=%s' % (v, val))
    # leg pose: this is what tells a working slide layer from a crouch-walk pose
    try:
        mesh_c = mesh
        thigh = mesh_c.get_socket_transform('thigh_l', unreal.RelativeTransformSpace.RTS_COMPONENT).rotation.rotator()
        foot = mesh_c.get_socket_transform('foot_l', unreal.RelativeTransformSpace.RTS_COMPONENT).translation
        pel = mesh_c.get_socket_transform('pelvis', unreal.RelativeTransformSpace.RTS_COMPONENT).translation
        d = foot - pel
        row.append('thighL=(%.0f,%.0f,%.0f)' % (thigh.pitch, thigh.yaw, thigh.roll))
        row.append('foot-pel=(%.0f,%.0f,%.0f)' % (d.x, d.y, d.z))
        row.append('pelZ=%.0f' % pel.z)
    except Exception as e:
        row.append('pose err %s' % e)
    w(' '.join(str(x) for x in row))


open(OUT, 'w', encoding='utf-8').write('')
S['h'] = unreal.register_slate_post_tick_callback(_tick)
print('DRIVER+SAMPLER armed ->', OUT)
