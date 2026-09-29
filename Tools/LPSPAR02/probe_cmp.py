import unreal

BS = unreal.BlueprintService
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'

for term in ('Less_DoubleDouble', 'LessFloat', 'Less_FloatFloat', 'LessEqual_DoubleDouble',
             'Not_PreBool', 'Boolean NOT', 'Conv_FloatToDouble', 'DoubleToFloat', 'Conv_DoubleToFloat'):
    ks = BS.discover_nodes(AB, term, '', 5)
    print('--- %r' % term)
    for k in ks:
        print('    ', k.spawner_key, '|', k.display_name)
