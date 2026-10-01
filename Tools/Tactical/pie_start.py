import unreal, json
r = unreal.ToolsetRegistry.execute_tool("EditorToolset.EditorAppToolset", "StartPIE",
    json.dumps({"options": {"bSimulate": False, "playMode": "PlayMode_InViewPort", "warmupSeconds": 4.0}}))
print("START", r.is_complete, r.error)
