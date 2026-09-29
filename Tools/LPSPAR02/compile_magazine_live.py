import unreal

assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), 'PIE must be stopped'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world, 'Editor world missing'
unreal.SystemLibrary.execute_console_command(world, 'LiveCoding.Compile')
print('LIVE_CODING_REQUESTED')
