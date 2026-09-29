import unreal

# Place the player near the test NPC, then try to start a slide on the NPC so the sampler has
# something to record. Reports which entry point worked.

NPC = '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterNPC_AR02_Test.BP_ShooterNPC_AR02_Test_C'
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
print('game world:', w)
if not w:
    print('NO PIE')
else:
    n = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.load_class(None, NPC))[0]
    loc = n.get_actor_location()
    dest = loc + n.get_actor_forward_vector() * 3200.0
    dest.z = loc.z + 100.0
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    if pawn:
        pawn.set_actor_location(dest, False, True)
    unreal.SystemLibrary.execute_console_command(w, 'god')
    mov = n.get_editor_property('character_movement')
    print('movement class:', mov.get_class().get_name())
    print('slide methods:', [m for m in dir(mov) if 'slide' in m.lower()])


    def prop(obj, name, default='n/a'):
        try:
            return obj.get_editor_property(name)
        except Exception:
            return default


    print('bIsSliding:', prop(mov, 'bIsSliding'))
    print('SlideAlpha:', prop(mov, 'SlideAlpha'))
    print('SlideDuration:', prop(mov, 'SlideDuration'))
    print('speed now: %.0f' % n.get_velocity().length())

    p = 'C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/LPSPAR02/sample_slide_logic.py'
    g = {'__name__': '__main__'}
    exec(compile(open(p, encoding='utf-8').read(), p, 'exec'), g)
    print('SAMPLER ARMED')
