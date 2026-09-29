import unreal, time

# Per-frame sampler for the slide check: anim variable SlideAlpha, movement state, and the leg bones
# that the new masked layer is supposed to drive. Writes Saved/LPSP_AR02/slide_samples.txt.
DURATION = 20.0
OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/slide_samples.txt'
NPC = '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterNPC_AR02_Test.BP_ShooterNPC_AR02_Test_C'
S = {'h': None, 't0': time.time(), 'lines': [], 'last': -1.0}


def r3(r):
    return 'p%.0f y%.0f r%.0f' % (r.pitch, r.yaw, r.roll)


def prop(obj, name, default='n/a'):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return default


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
    cls = unreal.load_class(None, NPC)
    npcs = unreal.GameplayStatics.get_all_actors_of_class(w, cls)
    if not npcs:
        return
    n = npcs[0]
    m = n.get_editor_property('mesh')
    ai = m.get_anim_instance()
    mov = n.get_editor_property('character_movement')
    slide_alpha = prop(ai, 'SlideAlpha')
    try:
        slide_alpha = mov.get_slide_alpha()
    except Exception:
        pass
    is_sliding = 'alpha>0.5'
    dur = 'n/a'
    try:
        dur = mov.get_slide_duration()
    except Exception:
        pass
    # trigger one slide as soon as the movement component allows it
    if t > 1.5 and not S.get('started'):
        try:
            if mov.can_slide():
                S['started'] = True
                S['lines'].append('TRIGGER start_slide at t=%.2f speed=%.0f' % (t, n.get_velocity().length()))
                mov.start_slide()
        except Exception as e:
            S['lines'].append('TRIGGER error %s' % e)
    thigh = m.get_socket_transform('thigh_l', unreal.RelativeTransformSpace.RTS_COMPONENT).rotation.rotator()
    foot = m.get_socket_transform('foot_l', unreal.RelativeTransformSpace.RTS_COMPONENT).translation
    pelvis = m.get_socket_transform('pelvis', unreal.RelativeTransformSpace.RTS_COMPONENT).translation
    d = foot - pelvis
    S['lines'].append('%.2f alpha=%s sliding=%s dur=%s spd=%.0f thighL=%s foot-pelvis=(%.0f,%.0f,%.0f)' % (
        t, slide_alpha, is_sliding, dur, n.get_velocity().length(), r3(thigh), d.x, d.y, d.z))


S['h'] = unreal.register_slate_post_tick_callback(_tick)
print('SLIDE SAMPLER armed', DURATION, '->', OUT)
