import unreal, time
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
print("weapon", pawn.get_current_weapon())
pawn.do_start_firing()
t0 = time.time()
def _stop(dt):
    global _h
    if time.time() - t0 > 0.6:
        pawn.do_stop_firing()
        unreal.unregister_slate_post_tick_callback(_h)
_h = unreal.register_slate_post_tick_callback(_stop)
