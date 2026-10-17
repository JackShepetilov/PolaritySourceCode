# modes.py: the flashlight and the laser get three modes on one key [author, 2026-10-15].
# Tools/ue_step.sh Tactical modes <step>. Steps: keys (what is taken), setup.
import unreal

EAL = unreal.EditorAssetLibrary
INPUT_DIR = "/Game/Variant_Shooter/Input/Actions"
IMC = "/Game/Variant_Shooter/Input/IMC_Weapons"
ACTION = "IA_TacticalMode"
TAC = "/Game/Variant_Shooter/Blueprints/Pickups/Attachments/Tactical"
PLAYER_BP = "/Game/Variant_Shooter/Blueprints/BP_ShooterCharacter"
CANDIDATES = ["T", "L", "B", "N", "Y", "U"]


def _taken():
    keys = {}
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    for a in ar.get_assets_by_class(unreal.TopLevelAssetPath("/Script/EnhancedInput", "InputMappingContext"), True):
        imc = a.get_asset()
        for m in imc.get_editor_property("default_key_mappings").get_editor_property("mappings"):
            k = str(m.get_editor_property("key").get_editor_property("key_name"))
            act = m.get_editor_property("action")
            keys.setdefault(k, []).append((a.asset_name, act.get_name() if act else None))
    return keys


def keys():
    taken = _taken()
    for k in CANDIDATES:
        print("KEY", k, taken.get(k, "free"))


def setup():
    path = INPUT_DIR + "/" + ACTION
    if not EAL.does_asset_exist(path):
        print("CREATED:", unreal.InputService.create_action(ACTION, INPUT_DIR, "Boolean"))
    taken = _taken()
    key = next((k for k in CANDIDATES if k not in taken), None)
    have = [k for k, v in taken.items() if any(n == ACTION for (_, n) in v)]
    if have:
        print("EXISTS: mapping", have, ACTION)
    elif key:
        print("ADDED: mapping", key, ACTION, unreal.InputService.add_key_mapping(IMC, path, key))
        EAL.save_asset(IMC, only_if_is_dirty=False)
    else:
        print("NO FREE KEY among", CANDIDATES)

    bp = unreal.load_asset(PLAYER_BP)
    cdo = unreal.get_default_object(bp.generated_class())
    comp = cdo.get_editor_property("tactical_device_component")
    comp.set_editor_property("mode_cycle_action", unreal.load_asset(path))
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    cdo = unreal.get_default_object(bp.generated_class())
    print("MODIFIED: player mode key action", cdo.get_editor_property("tactical_device_component").get_editor_property("mode_cycle_action"))
    print("SAVED:", EAL.save_asset(PLAYER_BP, only_if_is_dirty=False))

    laser = unreal.load_asset(TAC + "/DA_TacticalDevice_Laser")
    laser.set_editor_property("kind", unreal.TacticalLightKind.LASER)
    # The plain laser only starts the reaction over: no worse aim to speak of.
    laser.set_editor_property("normal_spread_multiplier", 1.05)
    print("MODIFIED: laser kind", laser.get_editor_property("kind"), "normal spread", laser.get_editor_property("normal_spread_multiplier"))
    print("SAVED:", EAL.save_asset(TAC + "/DA_TacticalDevice_Laser", only_if_is_dirty=False))
    flash = unreal.load_asset(TAC + "/DA_TacticalDevice_Flashlight")
    print("FLASHLIGHT kind", flash.get_editor_property("kind"))
