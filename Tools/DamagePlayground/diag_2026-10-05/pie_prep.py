import unreal
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
if not w:
    print("NO GAME WORLD")
else:
    for c in ["polarity.player.god 1", "polarity.debug.infiniteammo 1", "polarity.projectile.debug 1"]:
        unreal.SystemLibrary.execute_console_command(w, c)
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    print("pawn", pawn, pawn.get_actor_location(), pawn.get_actor_rotation())
    wpn = pawn.get_current_weapon()
    print("weapon before", wpn)
    if not wpn:
        pawn.equip_starting_weapon_animated()
