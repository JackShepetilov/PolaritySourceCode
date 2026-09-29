import json

import unreal

# PIE check of the pistol kit: does the weapon really carry the pack mesh, do the weapon-mesh montages
# and the body montage play on fire and on both reload stages.
# Numbers first, two screenshots for the eye.

WEAPON = '/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation/Pistols/BP_M1911.BP_M1911_C'
OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/hg03_pie.jsonl'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert world, 'PIE required'
subject = next(p for p in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.ShooterCharacter)
               if p.is_locally_controlled())
weapon_now = subject.get_current_weapon()
if not weapon_now or 'M1911' not in weapon_now.get_name():
    subject.set_editor_property('starting_weapon_class', unreal.load_class(None, WEAPON))
    subject.equip_starting_weapon_animated()

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
    place = target + subject.get_actor_forward_vector() * 230 + subject.get_actor_right_vector() * 150 + unreal.Vector(0, 0, 10)
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


def safe(action):
    try:
        return action()
    except Exception as exc:
        return 'ERR %s' % exc


def report(tag, t):
    weapon = subject.get_current_weapon()
    body = subject.get_editor_property('mesh').get_anim_instance()
    weapon_anim = weapon.get_third_person_mesh().get_anim_instance()
    write({'t': round(t, 3), 'tag': tag, 'weapon': weapon.get_name(),
           'tp_mesh': safe(lambda: str(weapon.get_third_person_mesh().get_editor_property('skeletal_mesh_asset'))),
           'attached_to': safe(lambda: str(weapon.get_third_person_mesh().get_attach_parent().get_name())),
           'socket': safe(lambda: str(weapon.get_third_person_mesh().get_attach_socket_name())),
           'body_montage': safe(lambda: str(body.get_current_active_montage())) if body else None,
           'weapon_montage': safe(lambda: str(weapon_anim.get_current_active_montage())) if weapon_anim else None,
           'reloading': safe(lambda: weapon.is_reloading())})


def once(key, at, action):
    if state['t'] >= at and key not in state['done']:
        state['done'].add(key)
        try:
            action()
        except Exception as exc:
            write({'t': round(state['t'], 3), 'action_error': key, 'error': str(exc)})


def shot():
    unreal.SystemLibrary.execute_console_command(
        world, 'HighResShot 1920x1080 filename=hg03_%s.png' % ('idle' if state['t'] < 3 else 'reload'))


def tick(dt):
    try:
        if not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
            unreal.unregister_slate_post_tick_callback(state['handle'])
            return
        state['t'] += dt
        t = state['t']
        once('shot_idle', 1.5, shot)
        once('report_idle', 2.0, lambda: report('idle', state['t']))
        once('fire', 3.0, subject.do_start_firing)
        once('stop_fire', 3.6, subject.do_stop_firing)
        once('reload', 4.0, lambda: write({'t': round(state['t'], 3), 'event': 'reload',
                                           'ok': subject.get_current_weapon().start_reload()}))
        once('report_reload', 5.0, lambda: report('reload', state['t']))
        once('shot_reload', 5.2, shot)
        once('report_after', 9.0, lambda: report('after', state['t']))
        if t - state['last'] >= 0.3:
            state['last'] = t
            report('tick', t)
        if t >= 11:
            unreal.unregister_slate_post_tick_callback(state['handle'])
            print('HG03 PIE COMPLETE', OUT)
    except Exception as exc:
        write({'error': str(exc)})
        unreal.unregister_slate_post_tick_callback(state['handle'])


state['handle'] = unreal.register_slate_post_tick_callback(tick)
print('HG03 PIE ARMED', subject.get_name())
