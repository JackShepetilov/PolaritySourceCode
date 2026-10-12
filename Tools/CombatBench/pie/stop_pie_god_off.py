import unreal
es = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
w = es.get_game_world() or es.get_editor_world()
unreal.SystemLibrary.execute_console_command(w, "polarity.player.god 0")
print("GOD OFF")
unreal.ToolsetRegistry.execute_tool("EditorToolset.EditorAppToolset", "StopPIE", "{}")
print("STOP requested")
