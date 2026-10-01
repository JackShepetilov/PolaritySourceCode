import unreal
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
print("PAWN", pawn.get_actor_location(), pawn.character_movement.movement_mode)
for start in (unreal.Vector(1585, -510, 0), unreal.Vector(1440, -119, 0)):
    hit = unreal.SystemLibrary.line_trace_single(w, start + unreal.Vector(0, 0, 2000), start - unreal.Vector(0, 0, 5000),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True)
    print("TRACE", start, hit)
for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.ShooterNPC)[:3]:
    print(a.get_name(), a.get_actor_location(), a.character_movement.movement_mode, a.capsule_component.get_collision_enabled())
