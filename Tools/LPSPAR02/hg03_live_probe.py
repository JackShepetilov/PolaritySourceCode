import json
import unreal

OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/hg03_live_probe.json'
WEAPON = '/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation/Pistols/BP_M1911.BP_M1911_C'
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert world, 'PIE required'
subject = next(p for p in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.ShooterCharacter)
               if p.is_locally_controlled())
weapon = subject.get_current_weapon()
if not weapon or 'M1911' not in weapon.get_name():
    subject.set_editor_property('starting_weapon_class', unreal.load_class(None, WEAPON))
    subject.equip_starting_weapon_animated()
weapon = subject.get_current_weapon()
body = subject.get_editor_property('mesh')
anim = body.get_anim_instance()
names = ('Actor Weapon', 'Data Table Animation Poses', 'Data Table Sequences',
         'Settings Animation', 'Sequence Loop Weapon Jog', 'Look Offset',
         'Aiming', 'Shot Count', 'Third Person')
row = {'subject': subject.get_name(), 'weapon': weapon.get_name() if weapon else None,
       'anim_class': anim.get_class().get_name() if anim else None, 'fields': {}}
for name in names:
    try:
        value = anim.get_editor_property(name)
        if hasattr(value, 'get_path_name'):
            row['fields'][name] = value.get_path_name()
        elif hasattr(value, 'export_text'):
            raw = value.export_text()
            row['fields'][name] = raw[:250]
        else:
            row['fields'][name] = str(value)
    except Exception as exc:
        row['fields'][name] = 'ERR ' + str(exc)
with open(OUT, 'w', encoding='utf-8') as handle:
    json.dump(row, handle, indent=2)
print('LIVE PROBE', OUT, row['subject'], row['weapon'], row['anim_class'])
