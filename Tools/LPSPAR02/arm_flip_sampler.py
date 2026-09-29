import unreal

# Put the player pawn in front of the AR_02 test NPC (the NPC only engages at a distance), give the
# player god so the burst does not end the session, then arm sample_flip.py (per-frame slate sampler).

NPC = '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterNPC_AR02_Test.BP_ShooterNPC_AR02_Test_C'
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
print('game world', w)
cls = unreal.load_class(None, NPC)
npcs = unreal.GameplayStatics.get_all_actors_of_class(w, cls)
print('npcs', len(npcs))
n = npcs[0]
loc = n.get_actor_location()
fwd = n.get_actor_forward_vector()
dest = loc + fwd * 1800.0
dest.z = loc.z + 100.0

pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
print('player pawn', pawn)
pc = unreal.GameplayStatics.get_player_controller(w, 0)
if pawn:
    pawn.set_actor_location(dest, False, True)
    print('player moved to', pawn.get_actor_location(), 'dist to npc', (pawn.get_actor_location() - loc).length())
if pc:
    pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(pawn.get_actor_location() if pawn else dest, loc))
    print('control rotation set')
unreal.SystemLibrary.execute_console_command(w, 'god')
print('god on')

p = 'C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/LPSPAR02/sample_flip.py'
g = {'__name__': '__main__'}
exec(compile(open(p, encoding='utf-8').read(), p, 'exec'), g)
print('SAMPLER ARMED via exec')
