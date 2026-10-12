import unreal
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pc = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.PlayerController)[0]
pawn = pc.get_controlled_pawn()
unreal.SystemLibrary.execute_console_command(w, "polarity.player.god 1", pc)
for e in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.BenchEncounter):
    if e.get_editor_property("encounter_name") == "SNIPER x1":
        pawn.set_actor_location(unreal.Vector(3733.0, 13750.0, 100.0), False, True)
        pc.set_control_rotation(unreal.Rotator(roll=0.0, pitch=8.0, yaw=0.0))
        e.start_encounter()
        print("STARTED", e.get_alive_count())
