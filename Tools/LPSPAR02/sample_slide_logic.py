import unreal, time

# Slide logic sampler for the AR_02 test NPC: catches StartSlide/EndSlide through the movement
# component's own delegates and logs per-frame speed, alpha, duration, fatigue and the leg pose.
# Writes Saved/LPSP_AR02/slide_logic_samples.txt
DURATION = 30.0
OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/slide_logic_samples.txt'
NPC = '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterNPC_AR02_Test.BP_ShooterNPC_AR02_Test_C'
S = {'h': None, 't0': time.time(), 'lines': [], 'last': -1.0, 'trigger': False, 'bound': None}


def stamp():
    return '%.2f' % (time.time() - S['t0'])


def on_started():
    S['lines'].append('%s EVENT slide STARTED' % stamp())


def on_ended():
    S['lines'].append('%s EVENT slide ENDED' % stamp())


def _tick(dt):
    t = time.time() - S['t0']
    w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if t > DURATION or not w:
        unreal.unregister_slate_post_tick_callback(S['h'])
        open(OUT, 'w', encoding='utf-8').write('\n'.join(S['lines']))
        return
    cls = unreal.load_class(None, NPC)
    npcs = unreal.GameplayStatics.get_all_actors_of_class(w, cls)
    if not npcs:
        return
    n = npcs[0]
    mov = n.get_editor_property('character_movement')

    if S['bound'] is None:
        got = []
        for name in ('on_slide_started', 'on_slide_ended'):
            try:
                d = mov.get_editor_property(name)
                d.add_callable(on_started if 'started' in name else on_ended)
                got.append(name)
            except Exception as e:
                got.append('%s: %s' % (name, e))
        S['bound'] = got
        S['lines'].append('%s BIND %s' % (stamp(), got))

    if t - S['last'] < 0.05:
        return
    S['last'] = t

    # fire a slide the moment the movement component allows it (speed above the minimum)
    if not S['trigger']:
        try:
            if mov.can_slide():
                S['trigger'] = True
                S['lines'].append('%s MANUAL can_slide=True speed=%.0f -> start_slide' % (stamp(), n.get_velocity().length()))
                mov.start_slide()
        except Exception as e:
            S['lines'].append('%s MANUAL error %s' % (stamp(), e))

    m = n.get_editor_property('mesh')
    alpha = mov.get_slide_alpha()
    dur = mov.get_slide_duration()
    fat = mov.get_slide_fatigue()
    try:
        can = mov.can_slide()
    except Exception:
        can = '?'
    thigh = m.get_socket_transform('thigh_l', unreal.RelativeTransformSpace.RTS_COMPONENT).rotation.rotator()
    foot = m.get_socket_transform('foot_l', unreal.RelativeTransformSpace.RTS_COMPONENT).translation
    pel = m.get_socket_transform('pelvis', unreal.RelativeTransformSpace.RTS_COMPONENT).translation
    d = foot - pel
    S['lines'].append('%s spd=%4.0f alpha=%.3f dur=%.3f can=%s fat=%d thighL=(%.0f,%.0f,%.0f) foot-pel=(%.0f,%.0f,%.0f)' % (
        stamp(), n.get_velocity().length(), alpha, dur, can, fat,
        thigh.pitch, thigh.yaw, thigh.roll, d.x, d.y, d.z))


S['h'] = unreal.register_slate_post_tick_callback(_tick)
print('SLIDE LOGIC SAMPLER armed', DURATION, '->', OUT)
