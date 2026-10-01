import unreal
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
print("PAWN", pawn.get_actor_location(), "aim", pawn.get_base_aim_rotation(), "apexAim", pawn.character_movement.is_aiming())
tc = pawn.get_component_by_class(unreal.TacticalDeviceComponent)
print("TAC", tc.get_charge(), tc.is_device_running(), tc.get_device())
for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.ShooterNPC):
    m = a.mesh
    print(a.get_name(), a.get_actor_location(), "mesh", m.get_world_location(), "head", m.get_socket_location("head"),
          "bone?", m.get_bone_index("head"), "dazzled", a.is_dazzled(), "dead", a.is_dead())
