import unreal

BS = unreal.BlueprintService
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'

for term in ('Layered blend', 'Try Get Pawn Owner', 'Get Apex Movement', 'Is Sliding',
             'Get Slide Duration', 'Less than', 'Sequence Player', 'Blend Poses by bool', 'Get Movement'):
    ks = BS.discover_nodes(AB, term, '', 8)
    print('--- %r (%d)' % (term, len(ks)))
    for k in ks:
        print('    ', k.spawner_key, '|', k.display_name, '|', k.node_class)
