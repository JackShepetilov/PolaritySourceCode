import unreal

NPC = '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterNPC_AR02_Test.BP_ShooterNPC_AR02_Test_C'
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
print('game world:', w)
if w:
    cls = unreal.load_class(None, NPC)
    test_npcs = unreal.GameplayStatics.get_all_actors_of_class(w, cls)
    print('test npcs:', len(test_npcs))
    base = unreal.load_class(None, '/Script/Polarity.ShooterNPC')
    alln = unreal.GameplayStatics.get_all_actors_of_class(w, base)
    print('all shooter npcs:', len(alln), [a.get_name() for a in alln][:6])
    if test_npcs:
        n = test_npcs[0]
        loc = n.get_actor_location()
        dest = loc + n.get_actor_forward_vector() * 2500.0
        dest.z = loc.z + 100.0
        pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
        if pawn:
            pawn.set_actor_location(dest, False, True)
        unreal.SystemLibrary.execute_console_command(w, 'god')
        mov = n.get_editor_property('character_movement')
        print('speed now: %.0f can_slide: %s' % (n.get_velocity().length(), mov.can_slide()))
        p = 'C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/LPSPAR02/sample_slide_logic.py'
        g = {'__name__': '__main__'}
        exec(compile(open(p, encoding='utf-8').read(), p, 'exec'), g)
        print('SAMPLER ARMED')
