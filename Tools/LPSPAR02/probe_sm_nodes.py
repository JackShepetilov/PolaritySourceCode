import unreal

BS = unreal.BlueprintService
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'

for term in ('State Machine', 'State', 'Transition', 'Sequence Player', 'Layered', 'Cached Pose', 'Blend Poses by bool', 'Two Way'):
    try:
        ks = BS.discover_nodes(AB, term, '', 10)
        print('--- %r: %d' % (term, len(ks)))
        for k in ks[:8]:
            print('    ', k.spawner_key, '|', k.display_name)
    except Exception as e:
        print('---', term, 'ERR', e)

# get_node_pins on an existing state machine node to understand structure
print('\n=== pins of AnimGraphNode_StateMachine_3 (SMachine States Stance) ===')
try:
    for p in BS.get_node_pins(AB, 'AnimGraph', 'AnimGraphNode_StateMachine_3'):
        print('   ', p)
except Exception as e:
    print('ERR', e)
