import json

import unreal

# Third person render of the pistol: the earlier shots came out first person, so here the view target
# is set explicitly and a camera is spawned when the level has none.

OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/hg03_tp.jsonl'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert world, 'PIE required'
subject = next(p for p in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.ShooterCharacter)
               if p.is_locally_controlled())
controller = subject.get_controller()
cameras = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CameraActor)
print('CAMERAS', len(cameras))
if cameras:
    camera = cameras[-1]
else:
    camera = unreal.GameplayStatics.spawn_actor_from_class(world, unreal.CameraActor,
                                                           subject.get_actor_location(), unreal.Rotator())
    print('CAMERA_SPAWNED', camera)

forward = subject.get_actor_forward_vector()
right = subject.get_actor_right_vector()
target = subject.get_actor_location() + unreal.Vector(0, 0, 30)
place = target + forward * 210 + right * 130 + unreal.Vector(0, 0, 10)
camera.set_actor_location(place, False, True)
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(place, target), False)
subject.get_editor_property('mesh').set_owner_no_see(False)
subject.get_current_weapon().get_third_person_mesh().set_owner_no_see(False)
controller.set_view_target(camera)
controller.set_view_target_with_blend(camera)
unreal.SystemLibrary.execute_console_command(world, 'r.SetRes 1920x1080w')

open(OUT, 'w').close()
state = {'t': 0.0, 'done': set(), 'handle': None}


def write(payload):
    with open(OUT, 'a', encoding='utf-8') as handle:
        handle.write(json.dumps(payload) + '\n')


def shot(name):
    unreal.SystemLibrary.execute_console_command(world, 'HighResShot 1920x1080 filename=%s.png' % name)
    write({'t': round(state['t'], 3), 'shot': name,
           'view_target': str(controller.get_view_target()) if hasattr(controller, 'get_view_target') else 'n/a'})


def once(key, at, action):
    if state['t'] >= at and key not in state['done']:
        state['done'].add(key)
        try:
            action()
        except Exception as exc:
            write({'t': round(state['t'], 3), 'action_error': key, 'error': str(exc)})


def tick(dt):
    try:
        state['t'] += dt
        once('idle', 0.8, lambda: shot('hg03_tp_idle'))
        once('reload', 1.6, lambda: subject.get_current_weapon().start_reload())
        once('reload_shot', 2.3, lambda: shot('hg03_tp_reload'))
        if state['t'] >= 3.5:
            unreal.unregister_slate_post_tick_callback(state['handle'])
            print('HG03 TP DONE', OUT)
    except Exception as exc:
        write({'error': str(exc)})
        unreal.unregister_slate_post_tick_callback(state['handle'])


state['handle'] = unreal.register_slate_post_tick_callback(tick)
print('HG03 TP ARMED')
