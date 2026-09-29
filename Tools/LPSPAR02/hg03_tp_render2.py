import json

import unreal

# Take the third person shot with a camera summoned into the PIE world (GameplayStatics has no
# spawn_actor_from_class in this build, and the test level carries no CameraActor).

OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/hg03_tp2.jsonl'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert world, 'PIE required'
subject = next(p for p in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.ShooterCharacter)
               if p.is_locally_controlled())
controller = subject.get_controller()

open(OUT, 'w').close()


def write(payload):
    with open(OUT, 'a', encoding='utf-8') as handle:
        handle.write(json.dumps(payload) + '\n')


before = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CameraActor)
write({'cameras_before': [c.get_name() for c in before]})
unreal.SystemLibrary.execute_console_command(world, 'summon CameraActor')
after = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CameraActor)
write({'cameras_after': [c.get_name() for c in after]})
print('CAMERAS_BEFORE', len(before), 'AFTER', len(after))

if after:
    camera = after[-1]
    forward = subject.get_actor_forward_vector()
    right = subject.get_actor_right_vector()
    target = subject.get_actor_location() + unreal.Vector(0, 0, 30)
    place = target + forward * 210 + right * 130 + unreal.Vector(0, 0, 10)
    camera.set_actor_location(place, False, True)
    camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(place, target), False)
    subject.get_editor_property('mesh').set_owner_no_see(False)
    subject.get_current_weapon().get_third_person_mesh().set_owner_no_see(False)
    controller.set_view_target_with_blend(camera)
    unreal.SystemLibrary.execute_console_command(world, 'r.SetRes 1920x1080w')
    write({'view_target_set': True})

state = {'t': 0.0, 'done': set(), 'handle': None}


def once(key, at, action):
    if state['t'] >= at and key not in state['done']:
        state['done'].add(key)
        try:
            action()
        except Exception as exc:
            write({'t': round(state['t'], 3), 'action_error': key, 'error': str(exc)})


def shot(name):
    unreal.SystemLibrary.execute_console_command(world, 'HighResShot 1920x1080 filename=%s.png' % name)
    write({'t': round(state['t'], 3), 'shot': name})


def tick(dt):
    try:
        state['t'] += dt
        once('idle', 0.8, lambda: shot('hg03_tp_idle2'))
        once('reload', 1.6, lambda: subject.get_current_weapon().start_reload())
        once('reload_shot', 2.3, lambda: shot('hg03_tp_reload2'))
        if state['t'] >= 3.5:
            unreal.unregister_slate_post_tick_callback(state['handle'])
            print('HG03 TP2 DONE')
    except Exception as exc:
        write({'error': str(exc)})
        unreal.unregister_slate_post_tick_callback(state['handle'])


state['handle'] = unreal.register_slate_post_tick_callback(tick)
print('HG03 TP2 ARMED')
