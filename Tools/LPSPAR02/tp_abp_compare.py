import collections

import unreal

# Which of the pack's AnimBPs actually holds the TP machinery, and is any of it weapon-family specific.
# Read-only.

ABPS = [
    '/Game/InfimaGames/LowPolyShooterPack/Core/Characters/ABP_LPSP_TP_PCH',
    '/Game/InfimaGames/AnimatedLowPolyWeapons/Core/ABP_ALPW_TP_PCH',
    '/Game/InfimaGames/AnimatedLowPolyWeapons/Useable/Animation/ABP_ALPW_TP_PCH_01',
    '/Game/InfimaGames/AnimatedLowPolyWeapons/Useable/Animation/ABP_ALPW_TP_PCH_02',
    '/Game/InfimaGames/LowPolyShooterPack/Core/Other/ABP_LPSP_CH',
    '/Game/InfimaGames/LowPolyShooterPack/Core/Weapons/ABP_LPSP_WEP',
]

for path in ABPS:
    bp = unreal.EditorAssetLibrary.load_asset(path)
    print('===', path, bool(bp))
    if not bp:
        continue
    package = bp.get_outermost()
    histogram = collections.Counter()
    slots, machines, states, layers, linked = set(), 0, 0, set(), 0
    for node in unreal.ObjectIterator(unreal.EdGraphNode):
        if node.get_outermost() != package:
            continue
        cls = node.get_class().get_name()
        histogram[cls] += 1
        if 'StateMachine' in cls:
            machines += 1
        elif 'AnimStateNode' in cls:
            states += 1
        elif 'Slot' in cls:
            try:
                slots.add(str(node.get_editor_property('slot_name')))
            except Exception:
                slots.add('ERR')
        elif 'LinkedAnimLayer' in cls:
            linked += 1
            try:
                layers.add(str(node.get_editor_property('layer')))
            except Exception:
                pass
    print('  state_machines', machines, 'states', states, 'linked_layers', linked)
    print('  slots', sorted(slots))
    print('  layers', sorted(layers))
    print('  classes', histogram.most_common(8))
    try:
        print('  variables', [str(v.variable_name) for v in unreal.BlueprintService.list_variables(path)][:40])
    except Exception as exc:
        print('  variables ERR', exc)
print('DONE')
