import unreal
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pc = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.PlayerController)[0]
pc.get_controlled_pawn().set_actor_location(unreal.Vector(5533.0, 13750.0, 100.0), False, True)
print("MOVED to x 5533 at", round(unreal.GameplayStatics.get_time_seconds(w), 1))
