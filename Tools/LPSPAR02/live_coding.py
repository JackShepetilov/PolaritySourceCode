import unreal

# Kick Live Coding from inside the editor (no key press needed). The result is in Saved/Logs/Polarity.log,
# category LogLiveCoding: "Starting Live Coding compile." / "Live coding succeeded" / compiler errors.
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
print('editor world: %s' % (world is not None))
print('game world (PIE must be off): %s' % (unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None))
unreal.SystemLibrary.execute_console_command(world, 'LiveCoding.Compile')
print('LiveCoding.Compile issued')
