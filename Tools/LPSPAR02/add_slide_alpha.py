import unreal, json

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
ref_path = AB + '.' + AB.split('/')[-1]

# Discover add_variable schema / signature
schemas = json.loads(unreal.ToolsetRegistry.get_all_toolset_json_schemas())
bp_tools = [s for s in schemas if 'BlueprintTools' in s.get('name', '')]
if bp_tools:
    print('Found BlueprintTools schemas:', len(bp_tools))
    for t in bp_tools[0].get('tools', []):
        if t.get('name') in ('add_variable', 'compile_blueprint'):
            print('Tool:', t.get('name'), t.get('input_schema'))

# Add SlideAlpha variable
args = {
    'blueprint': {'refPath': ref_path},
    'name': 'SlideAlpha',
    'type_name': 'float'
}
res = unreal.ToolsetRegistry.execute_tool(
    'editor_toolset.toolsets.blueprint.BlueprintTools',
    'add_variable',
    json.dumps(args)
)
print('add_variable result:', res.is_complete, res.error, res.get_value_as_json_string() if res.is_complete else '')

# Compile
comp = unreal.ToolsetRegistry.execute_tool(
    'editor_toolset.toolsets.blueprint.BlueprintTools',
    'compile_blueprint',
    json.dumps({'blueprint': {'refPath': ref_path}})
)
print('compile result:', comp.is_complete, comp.error)
unreal.EditorAssetLibrary.save_asset(AB, False)

# Verify
info = unreal.BlueprintService.get_blueprint_info(AB)
var_names = [v.variable_name for v in (info.variables or [])]
print('SlideAlpha verified:', 'SlideAlpha' in var_names)
