"""Explicit stages for a reproducible standalone tier run in a 1920x1080 PIE window."""
import json
import unreal

UES = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)


def start():
    if UES.get_game_world():
        raise RuntimeError("PIE already running")
    cls = unreal.load_class(None, "/Script/UnrealEd.LevelEditorPlaySettings")
    settings = unreal.get_default_object(cls)
    settings.set_editor_property("NewWindowWidth", 1920)
    settings.set_editor_property("NewWindowHeight", 1080)
    configure_performance()
    for cmd in ("polarity.ai.IgnorePlayers 0", "polarity.player.god 0", "polarity.ai.bench 1"):
        unreal.SystemLibrary.execute_console_command(UES.get_editor_world(), cmd)
    result = unreal.ToolsetRegistry.execute_tool("EditorToolset.EditorAppToolset", "StartPIE",
        json.dumps({"options": {"bSimulate": False, "playMode": "PlayMode_InEditorFloating", "warmupSeconds": 2.0}}))
    print("[TIER_RUN] start", result.error or "sent")


def configure_performance():
    settings_class = unreal.load_class(None, "/Script/UnrealEd.EditorPerformanceSettings")
    settings = unreal.get_default_object(settings_class)
    settings.set_editor_property("bThrottleCPUWhenNotForeground", False)
    unreal.SystemLibrary.execute_console_command(UES.get_editor_world(), "t.IdleWhenNotForeground 0")


def prepare():
    world = UES.get_game_world()
    if not world:
        raise RuntimeError("PIE not ready")
    players = [p.get_controlled_pawn() for p in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PlayerController)
               if p.get_controlled_pawn()]
    if len(players) != 1:
        raise RuntimeError("The comparison requires exactly one player")
    players[0].equip_starting_weapon_animated()
    weapon_class = unreal.load_class(None,
        "/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation/MG/BP_MGX5.BP_MGX5_C")
    assert weapon_class, "Benchmark automatic weapon is missing"
    players[0].add_weapon_class_animated(weapon_class)
    print("[TIER_RUN] equipped", players[0])


def run(rounds=5, timeout=60):
    world = UES.get_game_world()
    player = next(p.get_controlled_pawn() for p in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PlayerController)
                  if p.get_controlled_pawn())
    assert player.get_current_weapon().get_class().get_name() == "BP_MGX5_C", "Wait for weapon draw before running"
    root = "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_ShooterNPC_Tier"
    classes = ",".join(root + tier for tier in ("White", "Blue", "Purple"))
    unreal.SystemLibrary.execute_console_command(world, "polarity.bench.tier %s %d %d" % (classes, rounds, timeout))
    print("[TIER_RUN] dispatched", rounds, timeout)


def stop():
    world = UES.get_game_world()
    if world:
        unreal.SystemLibrary.execute_console_command(world, "polarity.bench.tier stop")
        unreal.ToolsetRegistry.execute_tool("EditorToolset.EditorAppToolset", "StopPIE", "{}")
    for cmd in ("polarity.ai.bench 0", "polarity.debug.infiniteammo 0"):
        unreal.SystemLibrary.execute_console_command(UES.get_editor_world(), cmd)
    print("[TIER_RUN] stopped")
