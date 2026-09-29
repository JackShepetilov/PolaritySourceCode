import unreal

# Same as arm_flip_sampler, but arms sample_weapon_bone.py (weapon mesh bones, both meshes).

NPC = '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterNPC_AR02_Test.BP_ShooterNPC_AR02_Test_C'
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
print('game world', w)
if not w:
    print('NO PIE - start PIE first')
else:
    cls = unreal.load_class(None, NPC)
    n = unreal.GameplayStatics.get_all_actors_of_class(w, cls)[0]
    loc = n.get_actor_location()
    dest = loc + n.get_actor_forward_vector() * 1800.0
    dest.z = loc.z + 100.0
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    pc = unreal.GameplayStatics.get_player_controller(w, 0)
    if pawn:
        pawn.set_actor_location(dest, False, True)
        print('player moved, dist', (pawn.get_actor_location() - loc).length())
    if pc:
        pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(dest, loc))
    unreal.SystemLibrary.execute_console_command(w, 'god')
    p = 'C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/LPSPAR02/sample_weapon_bone.py'
    g = {'__name__': '__main__'}
    exec(compile(open(p, encoding='utf-8').read(), p, 'exec'), g)
    print('WEAPON SAMPLER ARMED')
