import unreal

# Read the NPC's slide speed gates and re-arm the logic sampler in one go.

NPC = '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterNPC_AR02_Test.BP_ShooterNPC_AR02_Test_C'
SET = '/Game/Variant_Shooter/Blueprints/MovementSettings/MovementSettings_ShooterNPC'

st = unreal.load_asset(SET)
print('settings asset:', st, st.get_class().get_name() if st else None)
if st:
    for prop in ('SlideMinStartSpeed', 'SlideMinSpeed', 'SlideMinSpeedBurst', 'SlideMaxSpeedBurst',
                 'SlideFatigueScale', 'SprintSpeed', 'MaxWalkSpeed', 'MaxSprintSpeed'):
        try:
            print('   %-22s = %s' % (prop, st.get_editor_property(prop)))
        except Exception:
            print('   %-22s : no such property' % prop)

w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
print('game world:', w is not None)
if w:
    n = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.load_class(None, NPC))[0]
    loc = n.get_actor_location()
    dest = loc + n.get_actor_forward_vector() * 3400.0
    dest.z = loc.z + 100.0
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    if pawn:
        pawn.set_actor_location(dest, False, True)
    unreal.SystemLibrary.execute_console_command(w, 'god')
    mov = n.get_editor_property('character_movement')
    print('speed now: %.0f  can_slide: %s' % (n.get_velocity().length(), mov.can_slide()))

    p = 'C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/LPSPAR02/sample_slide_logic.py'
    g = {'__name__': '__main__'}
    exec(compile(open(p, encoding='utf-8').read(), p, 'exec'), g)
    print('SAMPLER ARMED')
