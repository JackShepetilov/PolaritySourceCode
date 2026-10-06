"""Mirror checks against real PIE colliders, followed by real pistol shots and VFX observations."""
import json
from pathlib import Path
import unreal

ROOT = Path(r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8")
OUT = ROOT / "Saved/Tank_Collision_Verification.json"
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
results = {"rays": [], "shots": []}


def save():
    OUT.write_text(json.dumps(results, indent=2), encoding="utf-8")


def context():
    world = ues.get_game_world()
    assert world
    controllers = [c for c in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PlayerController)
                   if c.get_controlled_pawn()]
    assert len(controllers) == 1
    pc = controllers[0]
    actor = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.TrackedTankNPC)
                 if a.actor_has_tag("TankVerify"))
    return world, pc, pc.get_controlled_pawn(), actor


def mirror_rays():
    world, pc, player, actor = context()
    colliders = {c.get_name(): c for c in actor.get_components_by_class(unreal.BoxComponent)
                 if c.get_name().startswith("HitZoneCollision_")}
    assert len(colliders) == 5, colliders
    for index in (0, 1):
        box = colliders["HitZoneCollision_" + str(index)]
        transform = box.get_world_transform()
        for direction in ("side", "front", "rear"):
            for along in (-130, 0, 130) if direction == "side" else (-10, 0, 10):
                for height in (-18, 0, 18):
                    # Aim at the visible face, not the centre behind the hull from above.
                    local = unreal.Vector(along, 0, height) if direction == "side" else unreal.Vector(178 if direction == "front" else -178, along, height)
                    point = unreal.MathLibrary.transform_location(transform, local)
                    offset = unreal.Vector(0, -600 if index == 0 else 600, 120) if direction == "side" else unreal.Vector(600 if direction == "front" else -600, 0, 120)
                    start = unreal.MathLibrary.transform_location(transform, local + offset)
                    hit = unreal.SystemLibrary.sphere_trace_single_for_objects(world, start, point, 4,
                        [unreal.ObjectTypeQuery.ECC_PAWN], False, [player], unreal.DrawDebugTrace.NONE, True)
                    component = hit.to_tuple()[10] if hit else None
                    actual = component.get_name() if component else None
                    item = {"part": index, "direction": direction, "along": along, "height": height,
                            "component": actual, "expected": box.get_name(), "pass": actual == box.get_name()}
                    results["rays"].append(item)
    save()
    print("MIRROR_RAYS", sum(r["pass"] for r in results["rays"]), "/", len(results["rays"]))
    print("FAILED", [r for r in results["rays"] if not r["pass"]])


def position_for_part(index):
    world, pc, player, actor = context()
    box = next(c for c in actor.get_components_by_class(unreal.BoxComponent)
               if c.get_name() == "HitZoneCollision_" + str(index))
    transform = box.get_world_transform()
    stand = unreal.MathLibrary.transform_location(transform, unreal.Vector(0, -550 if index == 0 else 550, 0))
    stand.z = 95
    player.set_actor_location(stand, False, True)
    pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(stand + unreal.Vector(0,0,64), box.get_world_location()))
    print("POSITION_FOR_PART", index)


def shoot_part(index):
    global shot_index, shot_clock, shot_handle, shot_sent
    world, pc, player, actor = context()
    assert player.get_current_weapon().get_class().get_name() == "BP_M1911_C"
    box = next(c for c in actor.get_components_by_class(unreal.BoxComponent)
               if c.get_name() == "HitZoneCollision_" + str(index))
    pc.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(pc.player_camera_manager.get_camera_location(), box.get_world_location()))
    shot_index, shot_clock, shot_sent = index, 0.0, False
    shot_handle = unreal.register_slate_post_tick_callback(shot_tick)


def shot_tick(delta):
    global shot_clock, shot_sent
    shot_clock += delta
    _, pc, player, actor = context()
    if shot_clock >= .15 and not shot_sent:
        player.do_start_firing()
        player.do_stop_firing()
        shot_sent = True
    if shot_clock > .23:
        zones = actor.get_component_by_class(unreal.HitZoneComponent)
        item = {"part": shot_index, "hp": actor.get_editor_property("current_hp"),
                "pools": {n: zones.get_zone_pool_fraction(n) for n in ("TrackLeft", "TrackRight", "Optics")},
                "effects": [{"system": c.get_asset().get_name() if c.get_asset() else None,
                             "position": [c.get_world_location().x, c.get_world_location().y, c.get_world_location().z]}
                            for c in unreal.ObjectIterator(unreal.NiagaraComponent)
                            if c.is_active() and c.get_asset() and c.get_asset().get_name() == "NS_Impact_TankTrack"
                            and "UEDPIE" in c.get_path_name()]}
        results["shots"].append(item)
        save()
        print("COLLISION_SHOT", item)
        unreal.unregister_slate_post_tick_callback(shot_handle)
