import unreal
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert w
unreal.SystemLibrary.execute_console_command(w,'HighResShot 1920x1080 filename=round4_infima_grip.png')
print('SCREENSHOT REQUESTED')
