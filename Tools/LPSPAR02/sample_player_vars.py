import unreal, time

# Waits for PIE (up to WAIT seconds), then logs the LOCAL player's anim variables every 0.1 s:
# what the graph thinks about slide, jump, crouch, aiming and look, next to the movement truth.
# Output: Saved/LPSP_AR02/player_vars.txt
WAIT = 600.0
RUNTIME = 240.0
OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/player_vars.txt'
VARS = ['SlideAlpha', 'CrouchAlpha', 'Crouching', 'Jumping', 'Aiming', 'Look Offset', 'Pitch',
        'Is Moving', 'Running', 'Tactical Sprinting', 'Movement Forward', 'Movement Right']
S = {'h': None, 't0': time.time(), 'lines': [], 'last': -1.0, 'seen': None, 'armed': time.time()}


def prop(obj, name):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return 'n/a'


def _tick(dt):
    t = time.time() - S['t0']
    w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if t > WAIT + RUNTIME:
        unreal.unregister_slate_post_tick_callback(S['h'])
        open(OUT, 'w', encoding='utf-8').write('\n'.join(S['lines']))
        return
    if not w:
        if t - S['last'] > 5.0:
            S['last'] = t
            S['lines'].append('%.1f waiting for PIE' % t)
        return
    if t - S['last'] < 0.1:
        return
    S['last'] = t
    pc = unreal.GameplayStatics.get_player_controller(w, 0)
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    if not pawn:
        return
    if S['seen'] != pawn.get_name():
        S['seen'] = pawn.get_name()
        S['lines'].append('pawn: %s class %s' % (pawn.get_name(), pawn.get_class().get_name()))
    mesh = prop(pawn, 'mesh')
    ai = mesh.get_anim_instance() if mesh and mesh != 'n/a' else None
    mov = prop(pawn, 'character_movement')
    row = ['%.1f' % t, 'cls=%s' % pawn.get_class().get_name()[-24:],
           'anim=%s' % (ai.get_class().get_name() if ai else None)]
    if mov != 'n/a':
        row.append('spd=%.0f' % pawn.get_velocity().length())
        try:
            row.append('sliding=%s' % mov.get_slide_alpha())
        except Exception:
            row.append('sliding=?')
    if ai:
        for v in VARS:
            row.append('%s=%s' % (v, prop(ai, v)))
    S['lines'].append(' '.join(str(x) for x in row))


S['h'] = unreal.register_slate_post_tick_callback(_tick)
print('PLAYER VARS SAMPLER armed ->', OUT)
