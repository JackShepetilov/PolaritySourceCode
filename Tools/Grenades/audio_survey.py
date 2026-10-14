import unreal


def main():
    w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    print("pie:", w is not None)
    print("dirty maps:", [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()])
    for n in ("Overall", "SFX", "UI", "Music", "Voice", "Application_Focused", "RenderedCinematics"):
        c = unreal.load_asset("/Game/Audio/Classes/" + n)
        if not c:
            print(n, "missing"); continue
        print(n, "parent:", c.get_editor_property("parent_class"), "children:", [x.get_name() if x else None for x in c.get_editor_property("child_classes")])
    for a in ("/Game/Audio/MetaSounds/sfx_Weapon_GrenadeExplosion_nl_meta", "/Game/Audio/Sounds/Grenade/grenade_explode_whack_01"):
        x = unreal.load_asset(a)
        try:
            print(a, "class:", x.get_editor_property("sound_class_object"))
        except Exception as e:
            print(a, "ERR", e)
