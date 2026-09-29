import unreal

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
if world:
    result = unreal.ToolsetRegistry.execute_tool('EditorToolset.EditorAppToolset', 'StopPIE', '{}')
    print('STOP PIE', result.is_complete, result.error)
else:
    print('PIE ALREADY OFF')
