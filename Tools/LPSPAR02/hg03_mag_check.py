import json

import unreal

# Does the code-built magazine appear on the third person mesh, where is it attached, and what does it
# hold. Also what happens to it during a reload before the interface events are wired in the base BP.

OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/hg03_mag_code.jsonl'
WEAPON = '/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation/Pistols/BP_M1911.BP_M1911_C'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert world, 'PIE required'
subject = next(p for p in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.ShooterCharacter)
               if p.is_locally_controlled())
if 'M1911' not in str(subject.get_current_weapon()):
    subject.set_editor_property('starting_weapon_class', unreal.load_class(None, WEAPON))
    subject.equip_starting_weapon_animated()
weapon = subject.get_current_weapon()
print('WEAPON', weapon.get_name())

cameras = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CameraActor)
if not cameras:
    unreal.SystemLibrary.execute_console_command(world, 'summon CameraActor')
    cameras = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CameraActor)
camera = cameras[-1]
forward = subject.get_actor_forward_vector()
right = subject.get_actor_right_vector()
target = subject.get_actor_location() + unreal.Vector(0, 0, 40)
place = target + forward * 200 + right * 120 + unreal.Vector(0, 0, 10)
camera.set_actor_location(place, False, True)
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(place, target), False)
subject.get_controller().set_view_target_with_blend(camera)
subject.get_editor_property('mesh').set_owner_no_see(False)
weapon.get_third_person_mesh().set_owner_no_see(False)
unreal.SystemLibrary.execute_console_command(world, 'r.SetRes 800x450w')

open(OUT, 'w').close()
state = {'t': 0.0, 'handle': None, 'done': set()}


def write(payload):
    with open(OUT, 'a', encoding='utf-8') as handle:
        handle.write(json.dumps(payload, default=str) + '\n')


def safe(action):
    try:
        return action()
    except Exception as exc:
        return 'ERR %s' % exc


def report(tag):
    mesh = weapon.get_third_person_mesh()
    children = []
    for child in mesh.get_children_components(False):
        asset = None
        if isinstance(child, unreal.StaticMeshComponent):
            asset = safe(lambda: str(child.get_editor_property('static_mesh')))
        children.append({'name': child.get_name(), 'class': child.get_class().get_name(),
                         'mesh': asset, 'visible': safe(lambda: child.is_visible())})
    write({'t': round(state['t'], 3), 'tag': tag, 'weapon': weapon.get_name(),
           'socket': safe(lambda: str(mesh.get_attach_socket_name())),
           'children': children,
           'reloading': weapon.is_reloading()})


def once(key, at, action):
    if state['t'] >= at and key not in state['done']:
        state['done'].add(key)
        try:
            action()
        except Exception as exc:
            write({'t': round(state['t'], 3), 'action_error': key, 'error': str(exc)})


def tick(dt):
    state['t'] += dt
    once('idle', 0.8, lambda: (report('idle'),
                               unreal.SystemLibrary.execute_console_command(
                                   world, 'HighResShot 800x450 filename=hg03_mag_idle.png')))
    once('reload', 1.6, lambda: weapon.start_reload())
    once('during', 2.4, lambda: (report('during_reload'),
                                 unreal.SystemLibrary.execute_console_command(
                                     world, 'HighResShot 800x450 filename=hg03_mag_reload.png')))
    once('after', 5.0, lambda: report('after_reload'))
    if state['t'] >= 5.5:
        unreal.unregister_slate_post_tick_callback(state['handle'])
        print('MAG CODE CHECK DONE', OUT)


state['handle'] = unreal.register_slate_post_tick_callback(tick)
print('MAG CODE CHECK ARMED')
