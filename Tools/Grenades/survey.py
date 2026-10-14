import unreal


def keys():
    for path in ("/Game/Variant_Shooter/Input/IMC_Weapons", "/Game/Input/IMC_Default"):
        imc = unreal.load_asset(path)
        data = imc.get_editor_property("default_key_mappings").get_editor_property("mappings")
        print(path, sorted("%s=%s" % (m.get_editor_property("key").get_editor_property("key_name"), m.get_editor_property("action").get_name()) for m in data))


def player():
    gm = unreal.load_asset("/Game/Variant_Shooter/Blueprints/BP_ShooterGameMode")
    if gm:
        cdo = unreal.get_default_object(gm.generated_class())
        print("pawn:", cdo.get_editor_property("default_pawn_class"))
    smoke = unreal.load_asset("/Game/Variant_Shooter/Blueprints/Abilities/DA_Ability_SmokeScreen")
    for p in ("aim_start_montage", "aim_loop_montage", "throw_montage", "throw_release_time", "quick_start_play_rate",
              "throw_play_rate", "montage_overlap", "aim_preview_delay", "throw_origin_offset", "aim_converge_distance",
              "throw_pitch_offset", "throw_sound", "cast_start_sound"):
        try:
            print(p, smoke.get_editor_property(p))
        except Exception as e:
            print(p, "ERR", e)
