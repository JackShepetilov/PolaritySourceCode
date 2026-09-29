import json

import unreal

# PIE check of the TP magazine on BP_AR02_Integration_Test.
#
# Answers, with numbers instead of guesses:
#   1. does the TP magazine component exist under ThirdPersonMesh at runtime,
#   2. does it have a mesh (the Infima magazine row's mesh) and is it visible,
#   3. does its transform sit on SOCKET_Magazine of the TP weapon mesh,
#   4. what happens to it during a tactical and an empty reload.
#
# Also takes two HighResShot screenshots for the eye: idle and mid-reload.

WEAPON = '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_AR02_Integration_Test.BP_AR02_Integration_Test_C'
OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/mag_check.jsonl'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert world, 'PIE required'
subject = next(p for p in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.ShooterCharacter)
               if p.is_locally_controlled())
subject.set_editor_property('starting_weapon_class', unreal.load_class(None, WEAPON))
subject.equip_starting_weapon_animated()

# Third person camera, same recipe as round4: the test level already carries a CameraActor.
controller = subject.get_controller()
controller.set_ignore_move_input(True)
controller.set_ignore_look_input(True)
subject.un_crouch()
subject.get_apex_movement().stop_movement_immediately()
controller.set_control_rotation(unreal.Rotator(pitch=0, yaw=subject.get_actor_rotation().yaw))
cameras = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CameraActor)
if cameras:
    camera = cameras[-1]
    target = subject.get_actor_location() + unreal.Vector(0, 0, 25)
    place = target + subject.get_actor_forward_vector() * 250 + subject.get_actor_right_vector() * 170 + unreal.Vector(0, 0, 15)
    camera.set_actor_location(place, False, True)
    camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(place, target), False)
    controller.set_view_target_with_blend(camera)
subject.get_editor_property('mesh').set_owner_no_see(False)
subject.get_current_weapon().get_third_person_mesh().set_owner_no_see(False)
unreal.SystemLibrary.execute_console_command(world, 'r.SetRes 1920x1080w')

open(OUT, 'w').close()
state = {'t': 0.0, 'done': set(), 'last': -1.0, 'handle': None}


def write(payload):
    with open(OUT, 'a', encoding='utf-8') as handle:
        handle.write(json.dumps(payload) + '\n')


def magazine_report(tag, t):
    weapon = subject.get_current_weapon()
    if not weapon:
        return
    mesh = weapon.get_third_person_mesh()
    row = {'t': round(t, 3), 'tag': tag, 'weapon': weapon.get_name()}
    children = mesh.get_children_components(False)
    magazine = [c for c in children if c and 'Magazine' in c.get_name()]
    row['children'] = [c.get_name() for c in children]
    row['socket_exists'] = mesh.does_socket_exist('SOCKET_Magazine')
    if mesh.does_socket_exist('SOCKET_Magazine'):
        row['socket_world'] = str(mesh.get_socket_location('SOCKET_Magazine'))
    for comp in magazine:
        if not isinstance(comp, unreal.StaticMeshComponent):
            continue
        asset = comp.get_editor_property('static_mesh')
        row['mag_name'] = comp.get_name()
        row['mag_mesh'] = asset.get_path_name() if asset else None
        row['mag_visible'] = comp.is_visible()
        row['mag_world'] = str(comp.get_component_location())
        row['mag_parent'] = comp.get_attach_parent().get_name() if comp.get_attach_parent() else None
        row['mag_socket'] = str(comp.get_attach_socket_name()) if hasattr(comp, 'get_attach_socket_name') else None
    write(row)


def once(key, at, action):
    if state['t'] >= at and key not in state['done']:
        state['done'].add(key)
        try:
            action()
        except Exception as exc:
            write({'t': round(state['t'], 3), 'action_error': key, 'error': str(exc)})


def reload():
    weapon = subject.get_current_weapon()
    write({'t': round(state['t'], 3), 'event': 'reload', 'ok': weapon.start_reload()})


def tick(dt):
    try:
        if not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
            unreal.unregister_slate_post_tick_callback(state['handle'])
            return
        state['t'] += dt
        t = state['t']
        once('shot_idle', 1.5, lambda: unreal.SystemLibrary.execute_console_command(
            world, 'HighResShot 1920x1080 filename=mag_idle.png'))
        once('report_idle', 2.0, lambda: magazine_report('idle', state['t']))
        once('tactical', 3.0, reload)
        once('shot_reload', 4.2, lambda: unreal.SystemLibrary.execute_console_command(
            world, 'HighResShot 1920x1080 filename=mag_reload.png'))
        once('report_reload', 4.5, lambda: magazine_report('during_reload', state['t']))
        once('finish', 12.0, lambda: magazine_report('after_reload', state['t']))
        if t - state['last'] >= 0.25:
            state['last'] = t
            weapon = subject.get_current_weapon()
            write({'t': round(t, 3),
                   'bullets': weapon.get_editor_property('CurrentBullets') if weapon else None,
                   'reloading': weapon.is_reloading() if weapon else None})
        if t >= 14:
            unreal.unregister_slate_post_tick_callback(state['handle'])
            print('MAG CHECK COMPLETE', OUT)
    except Exception as exc:
        write({'error': str(exc)})
        unreal.unregister_slate_post_tick_callback(state['handle'])


state['handle'] = unreal.register_slate_post_tick_callback(tick)
print('MAG CHECK ARMED', subject.get_name())
