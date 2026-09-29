import unreal

# Which AnimBP drives the player's third person body, and does it know about SlideAlpha?
CANDIDATES = [
    '/Game/Variant_Shooter/Blueprints/BP_ShooterCharacter',
    '/Game/Variant_Shooter/Blueprints/BP_ShooterCharacterCheat',
    '/Game/Variant_Shooter/Blueprints/Characters/BP_ShooterCharacter',
]

found = None
for path in CANDIDATES:
    a = unreal.load_asset(path)
    if a:
        found = path
        print('found player BP:', path)
        break
if not found:
    for a in unreal.EditorAssetLibrary.list_assets('/Game/Variant_Shooter/Blueprints', recursive=True, include_folder=False):
        n = a.split('/')[-1].split('.')[0]
        if n.startswith('BP_ShooterCharacter'):
            print('candidate:', a)

if found:
    cls = unreal.EditorAssetLibrary.load_blueprint_class(found)
    cdo = unreal.get_default_object(cls)
    for comp_name in ('mesh', 'first_person_mesh', 'weapon_tp_mesh'):
        try:
            comp = cdo.get_editor_property(comp_name)
        except Exception as e:
            print('%-18s : cannot read (%s)' % (comp_name, e))
            continue
        if not comp:
            print('%-18s : none' % comp_name)
            continue
        sk = None
        try:
            sk = comp.get_skinned_asset()
        except Exception:
            pass
        anim_cls = None
        for prop in ('anim_class', 'anim_instance_class', 'animation_blueprint'):
            try:
                anim_cls = comp.get_editor_property(prop)
                break
            except Exception:
                continue
        print('%-18s mesh=%-28s anim_class=%s' % (comp_name, sk.get_name() if sk else None, anim_cls))
