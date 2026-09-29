import unreal

BS = unreal.BlueprintService
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
BASE = '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/'

IDS = {
    'SlideIn': ('DAF3EBD547FEBEA094DF93891C4037FB', BASE + 'M_Neutral_Slide_FootOut_Into_Lfoot', 'false'),
    'SlideLoop': ('0EE1A0064CC9B8343C0F7983847FC210', BASE + 'M_Neutral_Slide_FootOut_Loop', 'true'),
    'SlideOut': ('5273C6024439008CF39695975C6131E2', BASE + 'M_Neutral_Slide_FootOut_Out_Moving_Run', 'false'),
}

print('=== pins of SlideIn ===')
for p in BS.get_node_pins(AB, GRAPH, IDS['SlideIn'][0]):
    print('   ', p)

# Try pin values first
for name, (nid, asset, looping) in IDS.items():
    r1 = BS.set_node_pin_value(AB, GRAPH, nid, 'Sequence', asset)
    r2 = BS.set_node_pin_value(AB, GRAPH, nid, 'bLooping', looping)
    print(name, 'set Sequence=', r1, 'bLooping=', r2)

# Read back
for name, (nid, asset, looping) in IDS.items():
    print('---', name)
    for p in BS.get_node_pins(AB, GRAPH, nid):
        print('   ', p)
