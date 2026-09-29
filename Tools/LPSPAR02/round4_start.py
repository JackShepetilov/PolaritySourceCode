import unreal
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
r = unreal.ToolsetRegistry.execute_tool('EditorToolset.EditorAppToolset', 'StartPIE',
    '{"options":{"bSimulate":false,"playMode":"PlayMode_InEditorFloating","warmupSeconds":2.0}}')
print('StartPIE',r.is_complete,r.error)
