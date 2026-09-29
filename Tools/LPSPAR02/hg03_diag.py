import json

import unreal

# Why the pistol grip and reload look wrong: compare what the working rifle carries against what the
# pistol carries, and read the timings that scale the third person montages.

OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/hg03_diag.jsonl'
RIFLE = '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_AR02_Integration_Test'
PISTOL = '/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation/Pistols/BP_M1911'
PISTOL_BACKUP = '/Game/Variant_Shooter/Tests/LPSP_Handgun_03/BP_M1911_BeforeTPKit'
FIELDS = ('reload_time', 'reload_montage', 'secondary_reload_montage', 'ReloadMontageTP',
          'SecondaryReloadMontageTP', 'WeaponMeshReloadAnimationTP', 'WeaponMeshSecondaryReloadAnimationTP',
          'OptionalGripSocketName', 'bWeaponPoseFromAnimation', 'bThirdPersonWeaponPoseFromAnimation')

open(OUT, 'w').close()


def write(payload):
    with open(OUT, 'a', encoding='utf-8') as handle:
        handle.write(json.dumps(payload, default=str) + '\n')


def safe(action):
    try:
        return action()
    except Exception as exc:
        return 'ERR %s' % exc


for label, path in (('RIFLE', RIFLE), ('PISTOL', PISTOL), ('PISTOL_BEFORE', PISTOL_BACKUP)):
    cls = unreal.EditorAssetLibrary.load_blueprint_class(path)
    cdo = unreal.get_default_object(cls)
    row = {'bp': label, 'path': path}
    for name in FIELDS:
        try:
            value = cdo.get_editor_property(name)
        except Exception as exc:
            row[name] = 'ABSENT'
            continue
        text = str(value)
        row[name] = text[-60:] if len(text) > 60 else text
    for component in cdo.get_components_by_class(unreal.SkeletalMeshComponent):
        mesh = safe(lambda: component.get_editor_property('skeletal_mesh_asset'))
        anim = safe(lambda: component.get_editor_property('anim_class'))
        row[component.get_name()] = {'mesh': str(mesh)[-70:], 'anim_class': str(anim)[-70:]}
    write(row)

for path in ('/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Characters/Animations/Handguns/AM_TP_CH_Handgun_Reload',
             '/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Weapons/Handguns/Animations/AM_TP_WEP_Handgun_03_Reload',
             '/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Weapons/Handguns/Animations/AM_TP_WEP_Handgun_03_Reload_Empty'):
    montage = unreal.EditorAssetLibrary.load_asset(path)
    write({'montage': path.split('/')[-1],
           'length': safe(lambda: montage.get_editor_property('sequence_length')) if montage else None,
           'slots': safe(lambda: [str(t.get_editor_property('slot_name'))
                                  for t in montage.get_editor_property('slot_anim_tracks')]) if montage else None})
for path in ('/Game/InfimaGames/LowPolyShooterPack/Core/Weapons/ABP_LPSP_WEP',
             '/Game/InfimaGames/AnimatedLowPolyWeapons/Core/ABP_ALPW_WEP',
             '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'):
    blueprint = unreal.EditorAssetLibrary.load_asset(path)
    if not blueprint:
        write({'abp': path, 'missing': True})
        continue
    package = blueprint.get_outermost()
    slots = []
    for node in unreal.ObjectIterator(unreal.EdGraphNode):
        if node.get_outermost() != package or 'Slot' not in node.get_class().get_name():
            continue
        slots.append({'node': node.get_name(),
                      'slot': safe(lambda: str(node.get_editor_property('slot_name')))})
    write({'abp': path.split('/')[-1], 'slot_nodes': slots})

print('DIAG DONE', OUT)
