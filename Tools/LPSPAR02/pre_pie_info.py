import unreal

# Pre-PIE facts about the AR_02 copy: variable list (name/type), and the recoil struct value kept on
# the AnimBP CDO (the previous session filled it from the AR_02 reference settings).
# Run: Tools/mcp.sh py Source/Tools/LPSPAR02/run.py

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'

info = unreal.BlueprintService.get_blueprint_info(AB)
print('status:', getattr(info, 'status', None))
vs = getattr(info, 'variables', None)
print('variables (%d):' % (len(vs) if vs else 0))
for v in (vs or []):
    try:
        print('   ', v.variable_name, v.variable_type, 'instanceditable=' + str(v.instance_editable) if hasattr(v, 'instance_editable') else '')
    except Exception as e:
        print('   raw', v, e)

cls = unreal.load_class(None, AB + '.ABP_AR02_LPSP_Test_C')
print('class:', cls)
cdo = unreal.get_default_object(cls)
print('CDO class:', cdo.get_class().get_name() if cdo else None)
for name in ['Shot Count', 'Recoil State Weapon', 'Aiming', 'Lean Alpha', 'Third Person', 'Actor Weapon']:
    try:
        v = cdo.get_editor_property(name)
        try:
            txt = v.export_text() if hasattr(v, 'export_text') else None
        except Exception:
            txt = None
        print('CDO %s = %s' % (name, (txt or v)))
    except Exception as e:
        print('CDO %s ERR %s' % (name, e))
