import unreal, time

# Start PIE, place the player near the test NPC, and try to make the NPC slide so the new graph
# can be checked. Prints which slide entry point exists.

es = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
print('game world before:', es.get_game_world())

if not es.get_game_world():
    r = unreal.ToolsetRegistry.execute_tool('EditorToolset.EditorAppToolset', 'StartPIE',
                                            '{"options": {"bSimulate": false, "playMode": "PlayMode_InViewPort", "warmupSeconds": 3.0}}')
    print('StartPIE:', r.is_complete, r.error)

pkg_cls = unreal.load_class(None, '/Script/Polarity.ApexMovementComponent')
print('Apex methods with slide:', [m for m in dir(pkg_cls) if 'slide' in m.lower()])
