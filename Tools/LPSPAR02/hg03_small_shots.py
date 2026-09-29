import unreal

# Same third person view, smaller shots so they can be reviewed.

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert world, 'PIE required'
subject = next(p for p in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.ShooterCharacter)
               if p.is_locally_controlled())
camera = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.CameraActor)[-1]
forward = subject.get_actor_forward_vector()
right = subject.get_actor_right_vector()
target = subject.get_actor_location() + unreal.Vector(0, 0, 30)
place = target + forward * 190 + right * 120 + unreal.Vector(0, 0, 10)
camera.set_actor_location(place, False, True)
camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(place, target), False)
subject.get_controller().set_view_target_with_blend(camera)
subject.get_editor_property('mesh').set_owner_no_see(False)
subject.get_current_weapon().get_third_person_mesh().set_owner_no_see(False)
unreal.SystemLibrary.execute_console_command(world, 'r.SetRes 1280x720w')

state = {'t': 0.0, 'done': set(), 'handle': None}


def once(key, at, action):
    if state['t'] >= at and key not in state['done']:
        state['done'].add(key)
        action()


def tick(dt):
    state['t'] += dt
    once('idle', 0.6, lambda: unreal.SystemLibrary.execute_console_command(
        world, 'HighResShot 1280x720 filename=hg03_small_idle.png'))
    once('reload', 1.4, lambda: subject.get_current_weapon().start_reload())
    once('reload_shot', 2.0, lambda: unreal.SystemLibrary.execute_console_command(
        world, 'HighResShot 1280x720 filename=hg03_small_reload.png'))
    if state['t'] >= 3.2:
        unreal.unregister_slate_post_tick_callback(state['handle'])
        print('SMALL SHOTS DONE')


state['handle'] = unreal.register_slate_post_tick_callback(tick)
print('SMALL SHOTS ARMED')
