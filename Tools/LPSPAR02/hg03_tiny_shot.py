import unreal

# One small shot (800x450) for review: the previous ones were too heavy to open.

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
unreal.SystemLibrary.execute_console_command(world, 'r.SetRes 800x450w')
unreal.SystemLibrary.execute_console_command(world, 'HighResShot 800x450 filename=hg03_tiny_idle.png')
print('TINY SHOT REQUESTED')
