import unreal

# Read the two recoil CurveVector assets the AR_02 copy's SRecoilState points at (CDO of ABP_AR02_LPSP_Test).
# The recoil rotation is sampled at time = float(Shot Count), so the curve's values/magnitudes decide
# what the ik_hand_gun nodes add while firing. Read-only.

BASE = '/Game/InfimaGames/LowPolyShooterPack/Data/Curves/Recoil/'
for name in ['VC_WEP_Recoil_ARs_Rotation', 'VC_WEP_Recoil_ARs_Location']:
    a = unreal.load_asset(BASE + name)
    print('=== %s -> %s' % (name, a))
    if not a:
        continue
    try:
        curves = a.get_editor_property('float_curves')
        print('  float_curves:', type(curves), len(curves))
        for i, c in enumerate(curves):
            try:
                ks = c.get_editor_property('keys')
                print('  curve %d: %d keys' % (i, len(ks)))
                for k in ks:
                    print('     t=%.3f v=%.4f' % (k.time, k.value))
            except Exception as e:
                print('  curve %d keys err %s' % (i, e))
    except Exception as e:
        print('  float_curves err', e)
    for t in [0.0, 0.25, 0.5, 0.75, 1.0, 2.0, 3.0, 4.0, 6.0]:
        try:
            print('  GetVectorValue(%.2f) = %s' % (t, a.get_vector_value(t)))
        except Exception as e:
            print('  GetVectorValue err', e)
            break
