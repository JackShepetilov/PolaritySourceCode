import json

import unreal

# 1) Does discovery see the interface event keys on the base weapon BP and on the AR02 test BP (which
#    already has such an event)?
# 2) Is there a tool for adding an interface to a Blueprint?

BS = unreal.BlueprintService
for label, path in (('BASE', '/Game/Variant_Shooter/Blueprints/Weapons/BP_ShooterWeaponBase'),
                    ('AR02', '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_AR02_Integration_Test')):
    for message in ('ALPW: Set Magazine Visibility', 'ALPW: Drop Magazine'):
        found = BS.discover_nodes(path, message, '', 20)
        print('DISCOVER', label, message, 'count', len(found))
        for item in found[:6]:
            print('   ', str(getattr(item, 'spawner_key', ''))[:90], '|',
                  str(getattr(item, 'display_name', ''))[:40], '|',
                  str(getattr(item, 'node_class', ''))[:40])

schemas = json.loads(unreal.ToolsetRegistry.get_all_toolset_json_schemas())
for toolset in schemas:
    name = toolset.get('name', '')
    tools = [tool.get('name', '') for tool in toolset.get('tools', [])]
    hits = [tool for tool in tools if 'interface' in tool.lower()]
    if hits:
        print('TOOLSET', name, hits)
print('PROBE DONE')
