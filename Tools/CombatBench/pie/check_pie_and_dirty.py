import unreal
r = unreal.ToolsetRegistry.execute_tool("EditorToolset.EditorAppToolset", "IsPIERunning", "{}")
print("PIE", r.get_value_as_json_string(), "dirty maps", [str(p.get_name()) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()], "dirty content", [str(p.get_name()) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()])
