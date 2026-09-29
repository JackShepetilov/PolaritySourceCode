import unreal

# Start PIE for the AR_02 test NPC measurements. Tries Epic's EditorAppToolset first, then the
# LevelEditorSubsystem. Checks the game world in a separate call (this one only requests PIE).

ok = None
try:
    args = '{"options": {"bSimulate": false, "playMode": "PlayMode_InViewPort", "warmupSeconds": 3.0}}'
    r = unreal.ToolsetRegistry.execute_tool('EditorToolset.EditorAppToolset', 'StartPIE', args)
    ok = 'toolset is_complete=%s error=%s' % (r.is_complete, r.error)
except Exception as e:
    ok = 'toolset ERR %s' % e
print('StartPIE:', ok)
