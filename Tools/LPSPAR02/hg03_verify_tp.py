import json
import unreal

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert world, 'PIE required'
subject = next(p for p in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.ShooterCharacter)
               if p.is_locally_controlled())
target_class = unreal.load_class(None,
    '/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation/Pistols/BP_M1911.BP_M1911_C')
weapon = subject.get_current_weapon()
if not weapon or 'M1911' not in weapon.get_name():
    subject.set_editor_property('starting_weapon_class', target_class)
    subject.equip_starting_weapon_animated()
weapon = subject.get_current_weapon()
assert weapon and 'M1911' in weapon.get_name()
controller = subject.get_controller()
controller.set_ignore_move_input(True)
controller.set_ignore_look_input(True)
subject.get_apex_movement().stop_movement_immediately()
subject.get_editor_property('FirstPersonMesh').set_visibility(False, True)
weapon.get_first_person_mesh().set_visibility(False, True)
subject.get_editor_property('mesh').set_owner_no_see(False)
weapon.get_third_person_mesh().set_owner_no_see(False)

cameras = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CameraActor)
if not cameras:
    unreal.SystemLibrary.execute_console_command(world, 'summon CameraActor')
    cameras = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CameraActor)
assert cameras
camera = cameras[-1]
target = subject.get_actor_location() + unreal.Vector(0, 0, 65)
place = target + subject.get_actor_forward_vector() * 260 + subject.get_actor_right_vector() * 105
camera.set_actor_location(place, False, True)
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(place, target), False)
controller.set_view_target_with_blend(camera)
unreal.SystemLibrary.execute_console_command(world, 'r.SetRes 1920x1080w')

OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/hg03_verify_tp.jsonl'
open(OUT, 'w').close()
state = {'t': 0.0, 'done': set(), 'handle': None, 'last': -1.0}


def asset_name(value):
    return value.get_name() if value else None


def report(tag):
    body = subject.get_editor_property('mesh').get_anim_instance()
    gun = weapon.get_third_person_mesh().get_anim_instance()
    row = {
        't': round(state['t'], 3), 'tag': tag,
        'weapon': weapon.get_name(),
        'actor_weapon': asset_name(body.get_editor_property('Actor Weapon')),
        'poses': asset_name(body.get_editor_property('Data Table Animation Poses')),
        'sequences': asset_name(body.get_editor_property('Data Table Sequences')),
        'jog': asset_name(body.get_editor_property('Sequence Loop Weapon Jog')),
        'look': asset_name(body.get_editor_property('Look Offset')),
        'shot_count': body.get_editor_property('Shot Count'),
        'body_montage': asset_name(body.get_current_active_montage()),
        'gun_montage': asset_name(gun.get_current_active_montage()) if gun else None,
        'reloading': weapon.is_reloading(),
    }
    with open(OUT, 'a', encoding='utf-8') as handle:
        handle.write(json.dumps(row, default=str) + '\n')


def once(key, when, action):
    if state['t'] >= when and key not in state['done']:
        state['done'].add(key)
        action()


def shot(label):
    unreal.SystemLibrary.execute_console_command(world,
        'HighResShot 1920x1080 filename=hg03_profile_' + label + '.png')


def tick(dt):
    try:
        if not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
            unreal.unregister_slate_post_tick_callback(state['handle'])
            return
        state['t'] += dt
        once('idle', 1.2, lambda: (report('idle'), shot('idle')))
        once('fire', 2.4, subject.do_start_firing)
        once('fire_capture', 2.48, lambda: (report('fire'), shot('fire')))
        once('fire_stop', 2.75, subject.do_stop_firing)
        once('reload', 4.0, lambda: (weapon.start_reload(), report('reload_start')))
        once('reload_capture', 4.65, lambda: (report('reload'), shot('reload')))
        once('end', 7.0, lambda: report('end'))
        if state['t'] - state['last'] >= 0.25:
            state['last'] = state['t']
            report('tick')
        if state['t'] >= 7.5:
            unreal.unregister_slate_post_tick_callback(state['handle'])
            print('TP VERIFY COMPLETE', OUT)
    except Exception as exc:
        with open(OUT, 'a', encoding='utf-8') as handle:
            handle.write(json.dumps({'error': str(exc), 't': state['t']}) + '\n')
        unreal.unregister_slate_post_tick_callback(state['handle'])


state['handle'] = unreal.register_slate_post_tick_callback(tick)
print('TP VERIFY ARMED', subject.get_name(), weapon.get_name())
