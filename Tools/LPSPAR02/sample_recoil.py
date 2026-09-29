import unreal, time

# Per-frame sampler (slate post-tick) for the AR_02 test NPC during PIE: ik_hand_gun component rotation,
# Shot Count, Is Firing of the weapon. Writes Saved/LPSP_AR02/recoil_samples.txt, stops itself after
# DURATION seconds or when PIE ends. Needed because one mcp.sh call takes ~16 s and a burst is shorter.

DURATION = 20.0
OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/recoil_samples.txt'
_state = {'h': None, 't0': time.time(), 'lines': []}


def _tick(dt):
    w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    done = (time.time() - _state['t0']) > DURATION or not w
    if not done:
        cls = unreal.load_class(None, '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterNPC_AR02_Test.BP_ShooterNPC_AR02_Test_C')
        npcs = unreal.GameplayStatics.get_all_actors_of_class(w, cls)
        if npcs:
            m = npcs[0].get_editor_property('mesh')
            ai = m.get_anim_instance()
            r = m.get_socket_transform('ik_hand_gun', unreal.RelativeTransformSpace.RTS_COMPONENT).rotation.rotator()
            sc = ai.get_editor_property('Shot Count')
            wp = ai.get_editor_property('Actor Weapon')
            firing = wp.is_firing() if wp and hasattr(wp, 'is_firing') else None
            _state['lines'].append('%.2f shots=%d firing=%s p=%.1f y=%.1f r=%.1f' % (
                time.time() - _state['t0'], sc, firing, r.pitch, r.yaw, r.roll))
    if done:
        unreal.unregister_slate_post_tick_callback(_state['h'])
        open(OUT, 'w', encoding='utf-8').write('\n'.join(_state['lines']))


_state['h'] = unreal.register_slate_post_tick_callback(_tick)
print('SAMPLER armed for', DURATION, 's ->', OUT)
