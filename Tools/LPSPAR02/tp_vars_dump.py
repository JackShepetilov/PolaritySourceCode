import json

import unreal

# Where the per-weapon jog and look values live: full donor settings dump and the AnimBP own defaults.

OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/tp_vars_dump.txt'
JSON_OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/tp_vars_dump.jsonl'
ABP = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
PACK_ABP = '/Game/InfimaGames/LowPolyShooterPack/Core/Characters/ABP_LPSP_TP_PCH'
DONORS = ('/Game/InfimaGames/LowPolyShooterPack/Usable/Weapons/BP_LPSP_WEP_Handgun_03',
          '/Game/InfimaGames/LowPolyShooterPack/Usable/Weapons/BP_LPSP_WEP_AR_02')
NAMES = ('Settings Animation', 'Data Table Animation Poses', 'Data Table Sequences',
         'Sequence Loop Weapon Jog', 'Look Offset')


def dump_to_file(label, obj, handle, json_handle):
    handle.write('===== %s (%s)\n' % (label, obj.get_class().get_name() if obj else 'None'))
    row = {'where': label}
    for name in NAMES:
        try:
            value = obj.get_editor_property(name)
        except Exception as exc:
            row[name] = 'ABSENT (%s)' % exc
            handle.write('  %s: ABSENT (%s)\n' % (name, exc))
            continue
        if name == 'Settings Animation':
            text = value.export_text()
            row[name] = 'len=%d' % len(text)
            handle.write('  Settings Animation (len=%d):\n%s\n' % (len(text), text))
        else:
            text = value.get_name() if hasattr(value, 'get_name') else str(value)
            row[name] = text
            handle.write('  %s = %s\n' % (name, text))
    json_handle.write(json.dumps(row, default=str) + '\n')


def load_class(asset_path):
    name = asset_path.split('/')[-1]
    return unreal.load_class(None, '%s.%s_C' % (asset_path, name))


with open(OUT, 'w', encoding='utf-8') as handle, open(JSON_OUT, 'w', encoding='utf-8') as json_handle:
    for donor_path in DONORS:
        cls = load_class(donor_path)
        handle.write('LOAD %s -> %s\n' % (donor_path, cls))
        if cls:
            dump_to_file('DONOR ' + donor_path.split('/')[-1], unreal.get_default_object(cls), handle, json_handle)
    for abp_path in (ABP, PACK_ABP):
        cls = load_class(abp_path)
        handle.write('LOAD %s -> %s\n' % (abp_path, cls))
        if cls:
            dump_to_file('ANIMBP ' + abp_path.split('/')[-1], unreal.get_default_object(cls), handle, json_handle)
print('DUMP DONE', OUT)
