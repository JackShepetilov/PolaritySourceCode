import unreal

# What the AR02 implementation actually consists of, field by field.
# Read-only. This is the reference manifest the other weapons must reproduce.

BP = '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_AR02_Integration_Test'
CH = '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterCharacter_AR02_Test'
ABP = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
KEYS = ('third', 'tp_', 'reload', 'fire', 'montage', 'animation', 'mesh', 'magazine', 'infima', 'aim')
SKIP = ('None', 'False', '0', '0.0', '', '()', '[]', 'True')


def interesting(name):
    lowered = name.lower()
    return any(key in lowered for key in KEYS)


for path in (BP, CH):
    cls = unreal.EditorAssetLibrary.load_blueprint_class(path)
    cdo = unreal.get_default_object(cls)
    print('=== ', path)
    for name in dir(cdo):
        if name.startswith('_') or not interesting(name):
            continue
        try:
            value = getattr(cdo, name)
        except Exception:
            continue
        if callable(value):
            continue
        text = str(value)[:220]
        if text in SKIP:
            continue
        print('  ', name, '=', text)

abp = unreal.EditorAssetLibrary.load_asset(ABP)
print('ABP', abp)
for prop in ('parent_class', 'implemented_interfaces', 'skeleton'):
    try:
        print('  ', prop, '=', abp.get_editor_property(prop) if abp else None)
    except Exception as exc:
        print('  ', prop, 'ERR', exc)
try:
    print('MAG_SOCKETS',
          unreal.BlueprintService.get_component_property(BP, 'InfimaTPMagazine', 'RelativeRotation'),
          unreal.BlueprintService.get_component_property(BP, 'InfimaTPMagazine', 'RelativeLocation'))
except Exception as exc:
    print('MAG_SOCKETS ERR', exc)
print('DONE')

