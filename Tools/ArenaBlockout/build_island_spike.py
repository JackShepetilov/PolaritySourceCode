# Island biome TECH SPIKE - MALDIVE MODEL (author 2026-06-11): each arena gets
# its OWN micro-island barely bigger than the arena, arena DEAD-CENTER on a flat
# top, open water between islands is mandatory. No big landmass here.
#
# Validates before the real island is built:
#   (a) heightmap import pipeline (make_island_heightmap.py -> import_heightmap)
#   (b) arena yaw rotation on its own maldive (A2 at 45 deg)
#   (c) Dynamic navmesh on an assembled map with TWO arena sublevels
#   (d) WaterBodyOcean + strait between islands + bridge stub (valve placeholder)
#
# AUTHOR-OWNED on this map (NEVER touch): RunLaunchPoint (he places and tunes it
# himself), the BP_ShooterCharacter actor, the ArenaLightingDebug3 sublevel, and
# the WaterBodyOcean/WaterZone once untagged (his ocean tweaks must survive).
#
# Usage (in-editor): py "<...>/build_island_spike.py"
#   Terrain comes from Island/spike_heightmap_1009.png (idempotent absolute
#   import - regenerate via: python make_island_heightmap.py).
# Idempotent: BLOCKOUT_IslandSpike actors are cleared and respawned; arenas are
# re-attached only when their transform changed. Log tag: [ISLAND_SPIKE]

import math
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
HEIGHTMAP = os.path.join(TOOLS_DIR, "Island", "spike_heightmap_1009.png")

SEA_FLOOR_Z = -2000.0     # landscape base plane Z (heightmap value 32768)
WATER_Z = 0.0

# (name, package, world pos, yaw) - keep in sync with make_island_heightmap.py:
# A1 maldive top 450 (deck 500 = pier on stilts), A2 maldive top 1200 (floors
# flush). Arena centered on its island; A2 rotated 45 = the 8a.3 yaw test.
ARENAS = [
    ("Lvl_A1_Pier", "/Game/Variant_Shooter/Arenas/Biome1/A1_Pier/Lvl_A1_Pier",
     (0.0, -24000.0, 500.0), 0.0),
    ("Lvl_A2_Courtyard", "/Game/Variant_Shooter/Arenas/Biome1/A2_Courtyard/Lvl_A2_Courtyard",
     (0.0, -7600.0, 1200.0), 45.0),
]

# valve placeholder across the strait (real island uses valve sublevels):
# A1 north beach (~-18150, H~50) -> A2 south beach (~-15300, H~60), deck above water
BRIDGE = {"id": "strait_bridge", "shape": "box", "mat": "deco",
          "pos": [0.0, -16725.0, 100.0], "size": [700.0, 3100.0, 60.0],
          "group": "Bridges"}

# (label, x, y, want) - terrain probes after heightmap import; catches both a
# broken import mapping (normalization) and a flipped row order
PROBES = (("A1 top", 0.0, -24000.0, 450.0),
          ("A2 top", 0.0, -7600.0, 1200.0),
          ("strait mid", 0.0, -16700.0, -539.0),
          ("open sea", 25000.0, -30000.0, -2000.0))


def log(msg):
    unreal.log("[ISLAND_SPIKE] {}".format(msg))


def warn(msg):
    unreal.log_warning("[ISLAND_SPIKE] {}".format(msg))


def preserve_water(eas):
    """Strip our tag from water actors: the ocean is a keeper the author tunes,
    clear_tagged must never cycle it again."""
    for a in eas.get_all_level_actors():
        if a is None:
            continue
        if a.get_class().get_name() in ("WaterBodyOcean", "WaterZone"):
            tags = [t for t in a.tags if str(t) != TAG]
            if len(tags) != len(a.tags):
                a.set_editor_property("tags", tags)
                log("Untagged water keeper '{}'".format(a.get_actor_label()))


def ensure_landscape():
    """Create the landscape once, then (re)import the authored heightmap -
    import is ABSOLUTE, so reruns are idempotent (unlike additive sculpting)."""
    if not hasattr(unreal, "LandscapeService"):
        raise RuntimeError("unreal.LandscapeService missing - is the VibeUE plugin loaded?")
    svc = unreal.LandscapeService
    if not svc.landscape_exists(LS_LABEL):
        half = (1009 - 1) * 100 * 0.5
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
        log("Created landscape {} (1009x1009 @ 100, base Z {})".format(
            LS_LABEL, SEA_FLOOR_Z))
    if not os.path.isfile(HEIGHTMAP):
        raise RuntimeError("Heightmap missing: {} - run make_island_heightmap.py"
                           .format(HEIGHTMAP))
    imp = svc.import_heightmap(LS_LABEL, HEIGHTMAP)
    if not getattr(imp, "success", False):
        raise RuntimeError("import_heightmap failed: "
                           + str(getattr(imp, "error_message", "?")))
    log("Heightmap imported: {} ({})".format(
        os.path.basename(HEIGHTMAP), getattr(imp, "resolution", "?")))
    bad = 0
    for label, x, y, want in PROBES:
        s = svc.get_height_at_location(LS_LABEL, x, y)
        if not s.valid:
            warn("Probe {}: INVALID".format(label))
            bad += 1
            continue
        ok = abs(s.height - want) <= max(80.0, abs(want) * 0.05)
        if not ok:
            bad += 1
        log("Probe {}: {:.0f} (want ~{:.0f}) {}".format(
            label, s.height, want, "OK" if ok else "MISMATCH"))
    if bad:
        warn("{} probe(s) off - import mapping/orientation needs attention, "
             "do not trust the terrain yet".format(bad))


def _transform_matches(sl, pos, yaw):
    try:
        t = sl.get_editor_property("level_transform")
        loc = t.translation
        cur_yaw = t.rotation.rotator().yaw
        return (abs(loc.x - pos[0]) < 1.0 and abs(loc.y - pos[1]) < 1.0 and
                abs(loc.z - pos[2]) < 1.0 and
                abs(((cur_yaw - yaw + 180.0) % 360.0) - 180.0) < 0.5)
    except Exception as e:
        warn("Cannot read level_transform ({}) - keeping as is".format(e))
        return True


def ensure_arenas(world):
    """Attach arenas as classic streaming sublevels. Transform is only applied
    AT ADD TIME (8a.1) - a moved slot means detach + re-add, never editing the
    transform of a loaded level."""
    for name, pkg, pos, yaw in ARENAS:
        sl = unreal.GameplayStatics.get_streaming_level(world, pkg) or \
            unreal.GameplayStatics.get_streaming_level(world, name)
        if sl and _transform_matches(sl, pos, yaw):
            log("{} already attached with the right transform".format(name))
            continue
        if sl:
            lvl = sl.get_loaded_level()
            if lvl is None or not hasattr(unreal.EditorLevelUtils,
                                          "remove_level_from_world"):
                warn("{} transform changed but cannot detach automatically - "
                     "remove it in the Levels panel and rerun".format(name))
                continue
            unreal.EditorLevelUtils.remove_level_from_world(lvl)
            log("Detached {} (slot moved)".format(name))
        xform = unreal.Transform(
            location=unreal.Vector(pos[0], pos[1], pos[2]),
            rotation=unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
        sl2 = unreal.EditorLevelUtils.add_level_to_world_with_transform(
            world, pkg, unreal.LevelStreamingAlwaysLoaded, xform)
        if sl2:
            log("Attached {} at {} yaw={}".format(name, pos, yaw))
        else:
            warn("failed to attach sublevel {}".format(pkg))


def ensure_water(eas):
    """Spawn WaterBodyOcean once, UNTAGGED (a keeper for the author to tune)."""
    if not hasattr(unreal, "WaterBodyOcean"):
        warn("unreal.WaterBodyOcean missing - Water plugin not loaded? Skipping ocean")
        return
    classes = {a.get_class().get_name() for a in eas.get_all_level_actors() if a}
    if "WaterBodyOcean" not in classes:
        ocean = eas.spawn_actor_from_class(
            unreal.WaterBodyOcean, unreal.Vector(0.0, 0.0, WATER_Z),
            unreal.Rotator(0.0, 0.0, 0.0))
        if ocean:
            ocean.set_actor_label("IslandSpike_Ocean")
            ocean.set_folder_path("IslandSpike/Water")
            log("Spawned WaterBodyOcean at Z={} (untagged keeper)".format(WATER_Z))
        else:
            warn("WaterBodyOcean failed to spawn")
    else:
        log("WaterBodyOcean already present - untouched")
    zone = None
    for a in eas.get_all_level_actors():
        if a and a.get_class().get_name() == "WaterZone":
            zone = a
            break
    if zone is None and hasattr(unreal, "WaterZone"):
        zone = eas.spawn_actor_from_class(unreal.WaterZone, unreal.Vector(0.0, 0.0, 0.0))
        if zone:
            zone.set_actor_label("IslandSpike_WaterZone")
            zone.set_folder_path("IslandSpike/Water")
            log("Spawned WaterZone (ocean did not auto-create one)")
    if zone is not None:
        try:
            zone.set_editor_property("zone_extent", unreal.Vector2D(110000.0, 110000.0))
        except Exception as e:
            warn("zone_extent not settable: {}".format(e))


AUTHOR_SUBLEVELS = ["/Game/Variant_Shooter/Arenas/ArenaDebug/ArenaLightingDebug3"]


def ensure_author_extras(world, eas):
    """The author's additions to this map are SACRED. The dirty-map guard in
    open_or_create_level now refuses to run over unsaved work; this is the
    second belt: re-attach his known sublevels if any past accident/crash left
    them missing, and loudly report his debug character state."""
    for pkg in AUTHOR_SUBLEVELS:
        short = pkg.rsplit("/", 1)[-1]
        if unreal.GameplayStatics.get_streaming_level(world, pkg) or \
           unreal.GameplayStatics.get_streaming_level(world, short):
            log("Author sublevel {} in place".format(short))
            continue
        sl = unreal.EditorLevelUtils.add_level_to_world(
            world, pkg, unreal.LevelStreamingAlwaysLoaded)
        if sl:
            log("Re-attached author sublevel {}".format(short))
        else:
            warn("Could not re-attach author sublevel {} - add it in the "
                 "Levels panel".format(pkg))
    has_char = any(a for a in eas.get_all_level_actors()
                   if a and "ShooterCharacter" in a.get_class().get_name())
    if not has_char:
        warn("BP_ShooterCharacter not found on the map (author places it "
             "manually - NOT spawning one)")


def ensure_dynamic_nav(eas):
    """Assembled-map navmesh MUST be runtime Dynamic (LevelDesign.md 8a.5)."""
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


def main():
    if hasattr(ba, "ensure_safe_to_build"):
        ba.ensure_safe_to_build()  # refuse during PIE (guide 1.0b)
    les, eas = ba.get_subsystems()
    ba.open_or_create_level(les, LEVEL_PATH)
    ba.backup_level(LEVEL_PATH, ARENA)
    preserve_water(eas)
    ba.clear_tagged(eas, TAG, LEVEL_PATH)

    ensure_landscape()
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    ensure_water(eas)
    ensure_arenas(world)
    ensure_author_extras(world, eas)

    mats = ba.ensure_materials(force=False)
    ba.spawn_shape(eas, mats, BRIDGE, TAG, ARENA)
    log("Strait bridge spawned (valve placeholder)")

    ensure_dynamic_nav(eas)
    try:
        unreal.SystemLibrary.execute_console_command(world, "RebuildNavigation")
        log("RebuildNavigation issued")
    except Exception as e:
        warn("RebuildNavigation failed: {}".format(e))
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("RESULT: SUCCESS - {} (maldive model: A1 centered @450-isle, "
        "A2 centered yaw=45 @1200-isle, strait + bridge)".format(LEVEL_PATH))


main()
