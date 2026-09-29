import unreal

# Stop PIE (needed before any write to the AnimBP: graph services and saves are dead while PIE runs).

try:
    r = unreal.ToolsetRegistry.execute_tool('EditorToolset.EditorAppToolset', 'StopPIE', '{"options": {}}')
    print('StopPIE:', r.is_complete, r.error)
except Exception as e:
    print('StopPIE ERR', e)

es = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
print('game world now:', es.get_game_world())
