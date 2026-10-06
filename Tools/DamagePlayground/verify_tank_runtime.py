"""Stages run in a live, single-player TierBench PIE; observations saved as JSON."""
import json
from pathlib import Path
import unreal

ROOT = Path(r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8")
RESULTS = ROOT / "Saved/Tank_Runtime_2026-10-04.json"
UES = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)


def context():
    world = UES.get_game_world()
    assert world, "PIE required"
    controllers = [p for p in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PlayerController)
                   if p.get_controlled_pawn()]
    assert len(controllers) == 1, "This isolated test requires one player"
    return world, controllers[0], controllers[0].get_controlled_pawn()


def record(name, value):
    data = json.loads(RESULTS.read_text()) if RESULTS.exists() else {}
    data[name] = value
    RESULTS.write_text(json.dumps(data, indent=2), encoding="utf-8")
    print("[TANK_VERIFY]", name, value)


def tank():
    world, _, _ = context()
    return next(a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TrackedTankNPC)
                if a.actor_has_tag("TankVerify"))


def summon():
    world, pc, player = context()
    player.equip_starting_weapon_animated()
    for command in ("polarity.player.god 1", "polarity.debug.infiniteammo 1", "polarity.ai.IgnorePlayers 0",
                    "polarity.ai.bench 0", "t.IdleWhenNotForeground 0", "DisableAllScreenMessages"):
        unreal.SystemLibrary.execute_console_command(world, command)
    path = "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_TrackedTank.BP_TrackedTank_C"
    unreal.load_class(None, path)
    before = set(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TrackedTankNPC))
    unreal.SystemLibrary.execute_console_command(world, "summon " + path)
    actor = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TrackedTankNPC) if a not in before)
    actor.set_editor_property("tags", [unreal.Name("TankVerify")])
    actor.set_actor_location_and_rotation(unreal.Vector(900, -900, 120), unreal.Rotator(yaw=180), False, True)
    player.set_actor_location(unreal.Vector(200, -900, 95), False, True)
    pc.set_control_rotation(unreal.Rotator(yaw=0))
    actor.set_editor_property("current_hp", 1000)
    if not actor.get_controller():
        actor.spawn_default_controller()
    record("spawn", {"name": actor.get_name(), "hp": actor.get_editor_property("current_hp"),
                     "one_track": actor.one_track_speed_multiplier, "ram_damage": actor.ram_damage,
                     "wreck_seconds": actor.wreck_seconds})


def center(actor, name):
    component = actor.get_components_by_class(unreal.HitZoneComponent)[0]
    zone = next(z for z in component.zones if str(z.name) == name)
    mesh = actor.get_components_by_class(unreal.SkeletalMeshComponent)[0]
    space = mesh.get_socket_transform(zone.bone, unreal.RelativeTransformSpace.RTS_WORLD) if str(zone.bone) else mesh.get_world_transform()
    return unreal.MathLibrary.transform_location(space, zone.center)


def point_hit(name, damage, heavy=False):
    world, pc, player = context()
    actor = tank()
    point = center(actor, name)
    yaw = actor.get_actor_rotation().yaw
    side = -90 if name == "TrackLeft" else 90 if name == "TrackRight" else 0
    direction = unreal.MathLibrary.get_forward_vector(unreal.Rotator(yaw=yaw + side))
    start, end = point + direction * 600, point - direction * 600
    hit = unreal.HitResult()
    def vec(v):
        return "(X=%f,Y=%f,Z=%f)" % (v.x, v.y, v.z)
    hit.import_text("(ImpactPoint=%s,TraceStart=%s,TraceEnd=%s)" % (vec(point), vec(start), vec(end)))
    kind = unreal.load_class(None, "/Script/Polarity.DamageType_Heavy") if heavy else unreal.DamageType
    applied = unreal.GameplayStatics.apply_point_damage(actor, damage, -direction, hit, pc, player, kind)
    zone = actor.get_components_by_class(unreal.HitZoneComponent)[0]
    move = actor.get_components_by_class(unreal.CharacterMovementComponent)[0]
    record("hit_" + name + "_" + str(damage), {"return_damage": applied,
        "pools": {n: zone.get_zone_pool_fraction(n) for n in ("TrackLeft", "TrackRight", "Optics")},
        "speed": move.max_walk_speed, "hp": actor.get_editor_property("current_hp"),
        "ramming": actor.is_ramming(), "dead": actor.is_dead(), "wrecked": actor.wrecked})


def view(name="Hull", distance=700):
    _, pc, player = context()
    actor = tank()
    point = center(actor, name)
    front = unreal.MathLibrary.get_forward_vector(unreal.Rotator(yaw=actor.get_actor_rotation().yaw))
    stand = actor.get_actor_location() + front * distance
    stand.z = 95
    player.set_actor_location(stand, False, True)
    pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(stand + unreal.Vector(0, 0, 64), point))


def fire():
    _, _, player = context()
    player.do_start_firing()


def cease():
    _, _, player = context()
    player.do_stop_firing()


def observe(name):
    _, _, player = context()
    actor = tank()
    record(name, {"tank_location": [actor.get_actor_location().x, actor.get_actor_location().y, actor.get_actor_location().z],
        "player_location": [player.get_actor_location().x, player.get_actor_location().y, player.get_actor_location().z],
        "ramming": actor.is_ramming(), "dead": actor.is_dead(), "wrecked": actor.wrecked,
        "fx": [{"name": c.get_name(), "active": c.is_active(), "system": str(c.get_asset())}
               for c in actor.get_components_by_class(unreal.NiagaraComponent)]})
