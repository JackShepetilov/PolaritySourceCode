import collections

import unreal

# Structural comparison of the pack's per-weapon AnimBPs against our AR_02 copy.
# Answers one question: do the per-family graphs differ structurally, or only in data.
# Read-only.

ABPS = [
    '/Game/InfimaGames/LowPolyShooterPack/Usable/Animation/ABP_LPSP_WEP_AR_02',
    '/Game/InfimaGames/LowPolyShooterPack/Usable/Animation/ABP_LPSP_WEP_Handgun_03',
    '/Game/InfimaGames/LowPolyShooterPack/Usable/Animation/ABP_LPSP_WEP_Shotgun_01',
    '/Game/InfimaGames/LowPolyShooterPack/Usable/Animation/ABP_LPSP_WEP_Sniper_02',
    '/Game/InfimaGames/LowPolyShooterPack/Usable/Animation/ABP_LPSP_WEP_RL_01',
    '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test',
]

for path in ABPS:
    bp = unreal.EditorAssetLibrary.load_asset(path)
    print('===', path, bool(bp))
    if not bp:
        continue
    package = bp.get_outermost()
    histogram = collections.Counter()
    graphs, machines, states, slots = set(), set(), set(), set()
    for node in unreal.ObjectIterator(unreal.EdGraphNode):
        if node.get_outermost() != package:
            continue
        graphs.add(node.get_outer().get_name())
        cls = node.get_class().get_name()
        histogram[cls] += 1
        if 'Machine' in cls:
            machines.add(node.get_name())
        elif 'AnimStateNode' in cls:
            try:
                states.add(str(node.get_editor_property('state_name')))
            except Exception:
                states.add(node.get_name())
        if 'Slot' in cls:
            try:
                slots.add(str(node.get_editor_property('slot_name')))
            except Exception:
                pass
    print('  graphs', sorted(graphs))
    print('  machines', sorted(machines))
    print('  state_count', len(states), sorted(states))
    print('  slots', sorted(slots))
    print('  classes', histogram.most_common(10))
print('DONE')
