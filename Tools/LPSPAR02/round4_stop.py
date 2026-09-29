import unreal
r=unreal.ToolsetRegistry.execute_tool('EditorToolset.EditorAppToolset','StopPIE','{}')
print('StopPIE',r.is_complete,r.error)
