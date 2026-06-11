# Island biome TECH SPIKE: landscape + ocean + ONE yaw-rotated arena + dynamic nav
# + run sea-launch. Validates the unknowns from LevelDesign.md §8a before the real
# island is built: (3) arena yaw rotation, (5) Dynamic navmesh on an assembled map,
# landscape-as-ground under an arena, WaterBodyOcean spawned from python.
#
# NOT the final island map - a disposable polygon (Lvl_IslandSpike).
#
# Usage (in-editor): py "<...>/build_island_spike.py" [--resculpt]
#   --resculpt: re-flatten the landscape to the sea floor and sculpt again
#               (sculpt ops are additive deltas - never re-run them blindly).
# Idempotent: BLOCKOUT_IslandSpike actors are cleared and respawned; the landscape
# and the arena sublevel are created once and kept. Log tag: [ISLAND_SPIKE]

import os
import sys

import unreal

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.append(TOOLS_DIR)
import build_arena as ba  # noqa: E402  (level/actor/material helpers)

LEVEL_PATH = "/Game/Variant_Shooter/Arenas/Biome1/Island/Lvl_IslandSpike"
TAG = "BLOCKOUT_IslandSpike"
ARENA = "IslandSpike"
LS_LABEL = "IslandSpike"

ARENA_PKG = "/Game/Variant_Shooter/Arenas/Biome1/A2_Courtyard/Lvl_A2_Courtyard"
ARENA_NAME = "Lvl_A2_Courtyard"

# --- layout (1009x1009 @ scale 100 = ~1x1 km, centered on origin) ---
SEA_FLOOR_Z = -2000.0     # landscape base plane (flat = ocean floor)
WATER_Z = 0.0             # WaterBodyOcean surface
PEAK_H = 9500.0           # mountain delta above sea floor -> peak ~ +7500 world
ISLAND_R = 32000.0        # mountain base radius; shoreline (Z=0) lands at ~21000
PLATEAU = (0.0, -12000.0, 3500.0)   # arena slot: cut into the south slope (~35 m)
PLATEAU_R = 6200.0        # A2 bounding circle at any yaw is ~5050
BEACH = (0.0, -19000.0, 250.0)      # landing strip between shoreline and slope
ARENA_YAW = 45.0          # deliberately diagonal: tests non-axis-aligned nav/collision
LAUNCH = (0.0, -25000.0, 150.0)     # over water; toss arc lands on the beach
LAUNCH_ROT = (0.0, 35.0, 90.0)      # roll, pitch (up), yaw (+Y = toward island)
LAUNCH_SPEED = 3200.0


def log(msg):
    unreal.log("[ISLAND_SPIKE] {}".format(msg))


def warn(msg):
    unreal.log_warning("[ISLAND_SPIKE] {}".format(msg))


def ensure_landscape():
    """Create the 1009x1009 landscape once; sculpt only on creation or --resculpt."""
    if not hasattr(unreal, "LandscapeService"):
        raise RuntimeError("unreal.LandscapeService missing - is the VibeUE plugin loaded?")
    svc = unreal.LandscapeService
    fresh = not svc.landscape_exists(LS_LABEL)
    if fresh:
        half = (1009 - 1) * 100 * 0.5  # 50400: center the landscape on the origin
        res = svc.create_landscape(
            location=unreal.Vector(-half, -half, SEA_FLOOR_Z),
            rotation=unreal.Rotator(0.0, 0.0, 0.0),
            scale=unreal.Vector(100.0, 100.0, 100.0),
            sections_per_component=2,
            quads_per_section=63,
            component_count_x=8,
            component_count_y=8,
            landscape_label=LS_LABEL)
        if not res.success:
            raise RuntimeError("create_landscape failed: " + str(res.error_message))
        log("Created landscape {} (1009x1009 @ 100 = ~1x1 km, base Z {})".format(
            LS_LABEL, SEA_FLOOR_Z))
    if not (fresh or "--resculpt" in sys.argv):
        log("Landscape exists - sculpt skipped (pass --resculpt to reshape)")
        return
    if not fresh:
        # additive sculpt ops would stack: reset the whole map to the base plane first
        svc.flatten_at_location(LS_LABEL, 0.0, 0.0, 80000.0, SEA_FLOOR_Z, 1.0)
        log("Reset terrain to sea floor for resculpt")
    # v3 signature (probed): (label, cx, cy, radius, height, sharpness, add_noise, seed)
    svc.create_mountain(LS_LABEL, center_x=0.0, center_y=0.0, radius=ISLAND_R,
                        height=PEAK_H, sharpness=1.2, add_noise=True, seed=7)
    svc.flatten_at_location(LS_LABEL, PLATEAU[0], PLATEAU[1], PLATEAU_R, PLATEAU[2], 1.0)
    svc.flatten_at_location(LS_LABEL, BEACH[0], BEACH[1], 3000.0, BEACH[2], 1.0)
    # soften the beach->slope and plateau->slope seams
    svc.smooth_at_location(LS_LABEL, 0.0, -16200.0, 3200.0, 0.5)
    a = svc.analyze_terrain(LS_LABEL, 0.0, 0.0, 0.0)
    log("Sculpted island: height {:.0f}..{:.0f}, max slope {:.1f} deg".format(
        a.min_height, a.max_height, a.max_slope_degrees))
    for label, (x, y, want) in (("plateau", PLATEAU), ("beach", BEACH)):
        s = svc.get_height_at_location(LS_LABEL, x, y)
        if s.valid:
            log("Height check {}: {:.0f} (want ~{:.0f})".format(label, s.height, want))


def ensure_water(eas):
    """Spawn WaterBodyOcean (+WaterZone if the ocean doesn't auto-create one)."""
    if not hasattr(unreal, "WaterBodyOcean"):
        warn("unreal.WaterBodyOcean missing - Water plugin not loaded? Skipping ocean")
        return
    actors = {a.get_class().get_name(): a for a in eas.get_all_level_actors() if a}
    if "WaterBodyOcean" not in actors:
        ocean = eas.spawn_actor_from_class(
            unreal.WaterBodyOcean, unreal.Vector(0.0, 0.0, WATER_Z),
            unreal.Rotator(0.0, 0.0, 0.0))
        if ocean:
            ba.finish_actor(ocean, TAG, "BLK_IslandSpike_Ocean", "IslandSpike/Water")
            log("Spawned WaterBodyOcean at Z={}".format(WATER_Z))
        else:
            warn("WaterBodyOcean failed to spawn")
    else:
        log("WaterBodyOcean already present")
    # ocean registration may auto-create a zone; re-scan before deciding
    zone = None
    for a in eas.get_all_level_actors():
        if a and a.get_class().get_name() == "WaterZone":
            zone = a
            break
    if zone is None and hasattr(unreal, "WaterZone"):
        zone = eas.spawn_actor_from_class(unreal.WaterZone, unreal.Vector(0.0, 0.0, 0.0))
        if zone:
            ba.finish_actor(zone, TAG, "BLK_IslandSpike_WaterZone", "IslandSpike/Water")
            log("Spawned WaterZone (ocean did not auto-create one)")
    if zone is not None:
        try:
            zone.set_editor_property("zone_extent", unreal.Vector2D(110000.0, 110000.0))
            log("WaterZone extent -> 110000x110000")
        except Exception as e:
            props = [p for p in dir(zone) if "extent" in p.lower() or "zone" in p.lower()]
            warn("zone_extent not settable ({}); candidates: {}".format(e, props))


def ensure_arena(world):
    """Attach A2_Courtyard as a classic streaming sublevel, ROTATED (the §8a.3 test)."""
    if unreal.GameplayStatics.get_streaming_level(world, ARENA_PKG) or \
       unreal.GameplayStatics.get_streaming_level(world, ARENA_NAME):
        log("Arena sublevel already attached - transform kept")
        return
    xform = unreal.Transform(
        location=unreal.Vector(PLATEAU[0], PLATEAU[1], PLATEAU[2]),
        rotation=unreal.Rotator(roll=0.0, pitch=0.0, yaw=ARENA_YAW))
    sl = unreal.EditorLevelUtils.add_level_to_world_with_transform(
        world, ARENA_PKG, unreal.LevelStreamingAlwaysLoaded, xform)
    if sl:
        log("Attached {} at {} yaw={}".format(ARENA_NAME, PLATEAU, ARENA_YAW))
    else:
        warn("failed to attach sublevel {}".format(ARENA_PKG))


def ensure_dynamic_nav(eas):
    """Assembled-map navmesh MUST be runtime Dynamic (LevelDesign.md §8a.5)."""
    nav = None
    for a in eas.get_all_level_actors():
        if a and a.get_class().get_name() == "RecastNavMesh":
            nav = a
            break
    if nav is None:
        nav = eas.spawn_actor_from_class(unreal.RecastNavMesh, unreal.Vector(0.0, 0.0, 0.0))
    if nav:
        nav.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)
        log("RecastNavMesh '{}' runtime_generation -> Dynamic".format(nav.get_actor_label()))
    else:
        warn("could not ensure RecastNavMesh - set RuntimeGeneration=Dynamic manually")


def ensure_launch(eas):
    cls = ba.resolve_class("/Script/Polarity.RunLaunchPoint")
    if cls is None:
        warn("RunLaunchPoint class not found - skipping")
        return
    pt = eas.spawn_actor_from_class(
        cls, unreal.Vector(*LAUNCH),
        unreal.Rotator(roll=LAUNCH_ROT[0], pitch=LAUNCH_ROT[1], yaw=LAUNCH_ROT[2]))
    if pt is None:
        warn("RunLaunchPoint failed to spawn")
        return
    pt.set_editor_property("arena_index", 0)
    pt.set_editor_property("b_launch_from_sea", True)
    pt.set_editor_property("launch_speed", LAUNCH_SPEED)
    ba.finish_actor(pt, TAG, "BLK_IslandSpike_RunLaunch", "IslandSpike/Logic")
    log("RunLaunchPoint at {} pitch {} speed {} (sea toss onto the beach)".format(
        LAUNCH, LAUNCH_ROT[1], LAUNCH_SPEED))


def main():
    les, eas = ba.get_subsystems()
    ba.open_or_create_level(les, LEVEL_PATH)
    ba.backup_level(LEVEL_PATH, ARENA)
    ba.clear_tagged(eas, TAG, LEVEL_PATH)

    ensure_landscape()
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    ensure_water(eas)
    ensure_arena(world)
    ensure_dynamic_nav(eas)
    ensure_launch(eas)

    try:
        unreal.SystemLibrary.execute_console_command(world, "RebuildNavigation")
        log("RebuildNavigation issued")
    except Exception as e:
        warn("RebuildNavigation failed: {}".format(e))
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("RESULT: SUCCESS - {} (arena {} yaw={} on plateau Z={})".format(
        LEVEL_PATH, ARENA_NAME, ARENA_YAW, PLATEAU[2]))


main()
