# pie_test.py
# Grenade checks in PIE, one step per call (Tools/ue_step.sh Grenades pie_test <step>).
# Docs/Grenades_Plan_2026-10-14.md
import json
import unreal

G = {}


def _world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def level():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    print("level:", les.get_current_level().get_outer().get_name())
    ew = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    npcs = [a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if isinstance(a, unreal.ShooterNPC)]
    print("npcs on level:", len(npcs))


def start():
    res = unreal.ToolsetRegistry.execute_tool("EditorToolset.EditorAppToolset", "StartPIE",
        json.dumps({"options": {"bSimulate": False, "playMode": "PlayMode_InEditorFloating", "warmupSeconds": 3.0}}))
    print("StartPIE", res.is_complete, res.error)


def setup():
    w = _world()
    if not w:
        print("no PIE world"); return
    unreal.SystemLibrary.execute_console_command(w, "polarity.ai.IgnorePlayers 1")
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    print("pawn:", pawn.get_class().get_name())
    gc = pawn.get_component_by_class(unreal.GrenadeComponent)
    bag = pawn.get_component_by_class(unreal.InventoryComponent)
    print("selected:", gc.get_selected_type(), "count:", gc.get_selected_count(), "all grenades:", bag.get_grenade_count(None))
    pawn.equip_starting_weapon_animated()


def tap():
    w = _world()
    pc = unreal.GameplayStatics.get_player_controller(w, 0)
    sub = unreal.SubsystemBlueprintLibrary.get_local_player_subsystem(pc, unreal.EnhancedInputLocalPlayerSubsystem)
    ia = unreal.load_asset("/Game/Variant_Shooter/Input/Actions/IA_Grenade")
    v = unreal.InputActionValue()
    sub.inject_input_for_action(ia, v, [], [])
    print("tap injected")


def state():
    w = _world()
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    gc = pawn.get_component_by_class(unreal.GrenadeComponent)
    print("throwing:", gc.is_throwing(), "selected:", gc.get_selected_type(), "count:", gc.get_selected_count())
    for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.GrenadeProjectile):
        print("grenade", a.get_name(), a.get_actor_location(), "hidden", a.is_hidden_ed())
