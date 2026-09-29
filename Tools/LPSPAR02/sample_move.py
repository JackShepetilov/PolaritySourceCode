import unreal, time

# Per-frame movement sampler (slate post-tick) for the AR_02 test NPC in PIE. Every 0.1 s writes:
# actor yaw, velocity yaw, speed, Movement Angle, Movement F/R/B/L, SL transition flags, crouch.
# Output Saved/LPSP_AR02/move_samples.txt after DURATION s.

DURATION = 15.0
OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/move_samples.txt'
S = {'h': None, 't0': time.time(), 'last': -1.0, 'lines': []}
FLAGS = ['SL Forward -> Forward Left', 'SL Forward -> Backward', 'SL Backward -> Backward Left', 'SL Backward -> Forward',
         'SL Forward Left -> Backward Left', 'SL Forward Left -> Forward', 'SL Backward Left -> Forward Left',
         'SL Backward Left -> Backward']


def _tick(dt):
    t = time.time() - S['t0']
    w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if t > DURATION or not w:
        unreal.unregister_slate_post_tick_callback(S['h'])
        open(OUT, 'w', encoding='utf-8').write('\n'.join(S['lines']))
        return
    if t - S['last'] < 0.1:
        return
    S['last'] = t
    cls = unreal.load_class(None, '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterNPC_AR02_Test.BP_ShooterNPC_AR02_Test_C')
    npcs = unreal.GameplayStatics.get_all_actors_of_class(w, cls)
    if not npcs:
        return
    n = npcs[0]
    ai = n.get_editor_property('mesh').get_anim_instance()
    v = n.get_velocity()
    vy = unreal.MathLibrary.make_rot_from_x(v).yaw if v.length() > 1 else 0.0
    g = ai.get_editor_property
    fl = ''.join('1' if g(f) else '0' for f in FLAGS)
    S['lines'].append('%.1f yaw=%.0f vyaw=%.0f spd=%.0f ang=%.0f F=%.2f R=%.2f B=%.2f L=%.2f flags=%s crouch=%s/%s' % (
        t, n.get_actor_rotation().yaw, vy, v.length(), g('Movement Angle'), g('Movement Forward'), g('Movement Right'),
        g('Movement Backward'), g('Movement Left'), fl, n.get_editor_property('is_crouched') if hasattr(n, 'is_crouched') else '?',
        g('Crouching')))


S['h'] = unreal.register_slate_post_tick_callback(_tick)
print('MOVE SAMPLER armed', DURATION)
