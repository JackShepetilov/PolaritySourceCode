import unreal
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
unreal.GameplayStatics.get_player_pawn(w, 0).do_stop_ads()
print("UNAIM")
