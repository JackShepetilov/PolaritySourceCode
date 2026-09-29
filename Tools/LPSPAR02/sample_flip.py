import unreal, time

# Per-frame sampler (slate post-tick) for the AR_02 test NPC. Answers two open questions in one run:
#  1) does the body/root follow the actor when the NPC turns (root yaw vs mesh yaw),
#  2) what moves during a burst: the ik_hand_gun bone or the weapon mesh itself.
# Also tests, once at t=12 s, whether ModifyBone_27's Rotation binding reads the AnimBP variable
# 'Root Yaw Moving Smooth' (rotator) - if the body turns on that write, the binding reads the variable.
# Writes Saved/LPSP_AR02/flip_samples.txt, stops itself after DURATION s or when PIE ends.

DURATION = 45.0
OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/flip_samples.txt'
NPC = '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterNPC_AR02_Test.BP_ShooterNPC_AR02_Test_C'
S = {'h': None, 't0': time.time(), 'lines': [], 'set': False}


def r3(r):
    return 'p%.0f y%.0f r%.0f' % (r.pitch, r.yaw, r.roll)


def v3(v):
    return '%.1f,%.1f,%.1f' % (v.x, v.y, v.z)


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
    m = n.get_editor_property('mesh')
    ai = m.get_anim_instance()
    body = m.get_world_rotation()
    root = m.get_socket_transform('root', unreal.RelativeTransformSpace.RTS_WORLD).rotation.rotator()
    ikc = m.get_socket_transform('ik_hand_gun', unreal.RelativeTransformSpace.RTS_COMPONENT).rotation.rotator()
    ikw = m.get_socket_transform('ik_hand_gun', unreal.RelativeTransformSpace.RTS_WORLD).rotation.rotator()
    wep = ai.get_editor_property('Actor Weapon')
    wyaw = None
    if wep:
        for c in wep.get_components_by_class(unreal.SkeletalMeshComponent):
            wyaw = r3(c.get_world_rotation())
            break
    if not S['set'] and t > 12.0:
        S['set'] = True
        try:
            ai.set_editor_property('Root Yaw Moving Smooth', unreal.Rotator(roll=0.0, pitch=0.0, yaw=90.0))
            S['lines'].append('SET Root Yaw Moving Smooth yaw=90 OK')
        except Exception as e:
            S['lines'].append('SET Root Yaw Moving Smooth ERR %s' % e)
    S['lines'].append('%.2f act_yaw=%.0f body=%s root=%s ikC=%s ikW=%s wep=%s shots=%d recl=%s ryms=%s aim=%s low=%s mov=%s' % (
        t, n.get_actor_rotation().yaw, r3(body), r3(root), r3(ikc), r3(ikw), wyaw,
        ai.get_editor_property('Shot Count'), v3(ai.get_editor_property('Current Recoil Rotation')),
        r3(ai.get_editor_property('Root Yaw Moving Smooth')), ai.get_editor_property('Aiming'),
        ai.get_editor_property('Lowered'), ai.get_editor_property('Is Moving')))


S['h'] = unreal.register_slate_post_tick_callback(_tick)
print('FLIP SAMPLER armed', DURATION, '->', OUT)
