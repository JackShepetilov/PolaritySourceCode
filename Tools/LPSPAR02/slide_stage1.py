import unreal

# 1) Timing sanity: our retargeted slide sequences vs the UE5 originals they came from.
# 2) Can python create AnimGraph nodes? Probe discover_nodes keys for the AnimGraph.
PAIRS = [
    'M_Neutral_Slide_FootOut_Into_Lfoot',
    'M_Neutral_Slide_FootOut_Loop',
    'M_Relaxed_Slide_FootOut_Into_Sprint_Lfoot',
    'M_Neutral_Slide_FootOut_Out_Moving_Sprint' if False else 'M_Neutral_Slide_FootOut_Out_Moving_Run',
]
OURS = '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/'
UE5 = '/Game/Characters/Mannequins/Anims/Slide/'

print('=== TIMING: ours vs UE5 original ===')
for n in PAIRS:
    for base, tag in ((OURS, 'ours'), (UE5, 'ue5')):
        a = unreal.load_asset(base + n)
        if not a:
            print('  %s %s: MISSING' % (tag, n))
            continue
        print('  %s %-42s len=%.3f s' % (tag, n, a.get_editor_property('sequence_length')))

# Also our Out variants
for n in ['M_Neutral_Slide_FootOut_Out_Moving_Sprint', 'M_Relaxed_Slide_FootOut_Out_Moving_Sprint',
          'M_Neutral_Slide_FootOut_Out_Idle_Crouch']:
    a = unreal.load_asset(OURS + n)
    a5 = unreal.load_asset(UE5 + n)
    print('  out: %s ours=%s ue5=%s' % (n, round(a.get_editor_property('sequence_length'), 3) if a else None,
                                        round(a5.get_editor_property('sequence_length'), 3) if a5 else None))

print('\n=== discover_nodes keys (AnimGraph) ===')
BS = unreal.BlueprintService
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
try:
    keys = BS.discover_nodes(AB, 'AnimGraph')
    names = [getattr(k, 'key', lambda: k)() if callable(getattr(k, 'key', None)) else str(k) for k in keys]
    flat = [str(k) for k in keys]
    interesting = [k for k in flat if any(s in k for s in ('Layered', 'SequencePlayer', 'StateMachine', 'Slot', 'BlendList', 'TwoWayBlend', 'BlendPoses'))]
    print('total keys:', len(flat))
    for k in interesting[:40]:
        print('  ', k)
except Exception as e:
    print('discover_nodes ERR:', e)
