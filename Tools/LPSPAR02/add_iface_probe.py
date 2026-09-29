import json

import unreal

# Signature of the interface tools, then add the pack's interface to the base weapon Blueprint.

BS = unreal.BlueprintService
print('HAS_ADD', hasattr(BS, 'add_interface'), hasattr(BS, 'remove_interface'))

schemas = json.loads(unreal.ToolsetRegistry.get_all_toolset_json_schemas())
for toolset in schemas:
    if toolset.get('name') != 'VibeUE.BlueprintService':
        continue
    for tool in toolset.get('tools', []):
        if 'Interface' in tool.get('name', ''):
            print('TOOL', tool.get('name'))
            print('  params', json.dumps(tool.get('inputSchema', {}).get('properties', {}), default=str)[:600])
            print('  required', tool.get('inputSchema', {}).get('required'))
print('SCHEMA DONE')
