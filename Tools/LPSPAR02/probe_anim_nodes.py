import unreal

BS = unreal.BlueprintService
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'

print('=== discover_nodes: all keys in AnimGraph ===')
try:
    keys = BS.discover_nodes(AB, 'AnimGraph')
    print('total:', len(keys))
    for k in keys:
        print('  ', k.spawner_key, '|', k.display_name, '|', k.node_class)
except Exception as e:
    print('ERR:', e)

# Try filtering by keyword if the service supports it
for kw in ('Sequence', 'Layered', 'StateMachine', 'Blend', 'Slot', 'Cache'):
    try:
        ks = BS.discover_nodes(AB, 'AnimGraph', kw) if False else None
    except Exception:
        pass

# Check signature
print('\ndiscover_nodes doc:', BS.discover_nodes.__doc__)
print('\nBS methods with node/graph:', [m for m in dir(BS) if 'node' in m.lower() or 'graph' in m.lower() or 'anim' in m.lower()])
