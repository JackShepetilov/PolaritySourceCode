import json

import unreal

# Where the AR leftovers come from: the donor's Settings Animation, the AnimBP defaults, and the live
# anim instance in PIE, side by side.

OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/tp_vars_probe.jsonl'
ABP = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
DONORS = ('/Game/InfimaGames/LowPolyShooterPack/Usable/Weapons/BP_LPSP_WEP_Handgun_03',
          '/Game/InfimaGames/LowPolyShooterPack/Usable/Weapons/BP_LPSP_WEP_AR_02')
NAMES = ('Settings Animation', 'Data Table Animation Poses', 'Data Table Sequences',
         'Sequence Loop Weapon Jog', 'Look Offset', 'Recoil State Weapon', 'Actor Weapon')

open(OUT, 'w').close()


def write(payload):
    with open(OUT, 'a', encoding='utf-8') as handle:
        handle.write(json.dumps(payload, default=str) + '\n')


def dump(label, obj):
    row = {'where': label}
    for name in NAMES:
        try:
            value = obj.get_editor_property(name)
        except Exception:
            row[name] = None
            continue
        if name == 'Settings Animation':
            try:
                row[name] = value.export_text()[:900]
            except Exception as exc:
                row[name] = 'ERR %s' % exc
        else:
            row[name] = value.get_name() if hasattr(value, 'get_name') else str(value)[:80]
    write(row)


for donor_path in DONORS:
    cls = unreal.EditorAssetLibrary.load_blueprint_class(donor_path)
    if cls:
        dump('DONOR ' + donor_path.split('/')[-1], unreal.get_default_object(cls))

anim_cls = unreal.EditorAssetLibrary.load_blueprint_class(ABP)
if anim_cls:
    dump('ANIMBP_DEFAULTS', unreal.get_default_object(anim_cls))

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
if world:
    subject = next(p for p in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.ShooterCharacter)
                   if p.is_locally_controlled())
    dump('LIVE_BODY', subject.get_editor_property('mesh').get_anim_instance())
print('PROBE DONE', OUT)
