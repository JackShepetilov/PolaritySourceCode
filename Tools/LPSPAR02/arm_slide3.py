import unreal, json

# Temporary: lower the NPC's slide start gate so the AI can actually slide in this test scenario
# (it holds 300-500 cm/s, the gate is 700). Original value is stored for rollback.
SET = '/Game/Variant_Shooter/Blueprints/MovementSettings/MovementSettings_ShooterNPC'
BACKUP = unreal.Paths.project_saved_dir() + 'LPSP_AR02/slide_settings_backup.json'
NPC = '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterNPC_AR02_Test.BP_ShooterNPC_AR02_Test_C'

st = unreal.load_asset(SET)
orig = {}
for prop in ('SlideMinStartSpeed', 'SlideMinSpeed'):
    orig[prop] = st.get_editor_property(prop)
json.dump({'asset': SET, 'values': orig}, open(BACKUP, 'w', encoding='utf-8'), indent=2)
print('original:', orig, '-> saved to', BACKUP)

st.set_editor_property('SlideMinStartSpeed', 250.0)
print('saved:', unreal.EditorAssetLibrary.save_asset(SET, only_if_is_dirty=False))
print('SlideMinStartSpeed now:', unreal.load_asset(SET).get_editor_property('SlideMinStartSpeed'))

w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
print('game world:', w is not None)
if w:
    n = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.load_class(None, NPC))[0]
    loc = n.get_actor_location()
    dest = loc + n.get_actor_forward_vector() * 2500.0
    dest.z = loc.z + 100.0
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    if pawn:
        pawn.set_actor_location(dest, False, True)
    unreal.SystemLibrary.execute_console_command(w, 'god')
    print('speed now: %.0f' % n.get_velocity().length())
    p = 'C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/LPSPAR02/sample_slide_logic.py'
    g = {'__name__': '__main__'}
    exec(compile(open(p, encoding='utf-8').read(), p, 'exec'), g)
    print('SAMPLER ARMED')
