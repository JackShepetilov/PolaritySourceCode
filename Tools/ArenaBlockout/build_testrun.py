# Arena gauntlet test level: all 8 arenas as streaming sublevels in difficulty
# order, linked by a south boulevard with a bridge to each arena's approach.
# NOT the island macro level - purely for feel-testing arenas in sequence.
#
# Usage (in-editor): py "<...>/build_testrun.py"
# Idempotent: BLOCKOUT_TestRun geometry is cleared and rebuilt; already-added
# sublevels are kept (transform re-applied).
# Log tag: [TESTRUN]

import os
import sys

import unreal

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.append(TOOLS_DIR)
import build_arena as ba  # noqa: E402  (reuse materials/shape/level helpers)

LEVEL_PATH = "/Game/Variant_Shooter/Arenas/Biome1/TestRun/Lvl_ArenaTestRun"
ARENA_ROOT = "/Game/Variant_Shooter/Arenas/Biome1"
TAG = "BLOCKOUT_TestRun"
ARENA = "TestRun"

# (name, south approach edge local y, sublevel z offset)
ARENAS = [
    ("A1_Pier",         -3800, 0),
    ("A8_Lighthouse",   -3250, 0),
    ("A2_Courtyard",    -3800, 0),
    ("A4_Hangar",       -4300, 0),
    ("A3_Dome",         -3050, 0),
    ("A5_Amphitheater", -4100, 0),
    ("A6_Villa",        -4700, 0),
    ("A7_Gallery",      -4500, 500),
]
SPACING = 10500
BLVD_Y = -6400
BLVD_HALF = 750


def log(msg):
    unreal.log("[TESTRUN] {}".format(msg))


def main():
    les, eas = ba.get_subsystems()
    mats = ba.ensure_materials(force=False)
    # Regenerate from scratch every run: arenas attach as LevelInstance actors
    # (the same mechanism the island will use), so a stale persistent level with
    # origin-stacked streaming sublevels from older runs must not survive.
    eal = unreal.EditorAssetLibrary
    if eal.does_asset_exist(LEVEL_PATH):
        les.new_level("/Game/__transient_testrun_switch")
        eal.delete_asset(LEVEL_PATH)
        log("Deleted old " + LEVEL_PATH)
    ba.open_or_create_level(les, LEVEL_PATH)
    ba.clear_tagged(eas, TAG, LEVEL_PATH)

    last_x = (len(ARENAS) - 1) * SPACING
    blvd_len = last_x + 8000
    pieces = [
        {"id": "boulevard", "shape": "box", "mat": "floor",
         "pos": [last_x * 0.5, BLVD_Y, -50], "size": [blvd_len, BLVD_HALF * 2, 100],
         "group": "Boulevard"},
    ]
    for i, (name, edge, _zoff) in enumerate(ARENAS):
        x = i * SPACING
        y_from = BLVD_Y + BLVD_HALF          # boulevard north edge
        y_to = edge + 200                     # overlap into the arena approach
        length = y_to - y_from
        pieces.append({"id": "bridge_{}".format(name), "shape": "box", "mat": "deco",
                       "pos": [x, (y_from + y_to) * 0.5, -50],
                       "size": [600, abs(length), 100], "group": "Bridges"})
    for piece in pieces:
        ba.spawn_shape(eas, mats, piece, TAG, ARENA)
    log("Spawned boulevard + {} bridges".format(len(ARENAS)))

    start = eas.spawn_actor_from_class(unreal.PlayerStart,
                                       unreal.Vector(-3000, BLVD_Y, 100),
                                       unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    ba.finish_actor(start, TAG, "BLK_TestRun_PlayerStart", "TestRun/Logic")

    # --- attach arenas as LevelInstance actors (transform-capable) ---
    attached = 0
    for i, (name, _edge, zoff) in enumerate(ARENAS):
        pkg = "{}/{}/Lvl_{}".format(ARENA_ROOT, name, name)
        world_asset = unreal.EditorAssetLibrary.load_asset(pkg)
        if world_asset is None:
            unreal.log_warning("[TESTRUN] arena level missing: {}".format(pkg))
            continue
        inst = eas.spawn_actor_from_class(
            unreal.LevelInstance, unreal.Vector(i * SPACING, 0.0, float(zoff)))
        if inst is None:
            unreal.log_warning("[TESTRUN] LevelInstance spawn failed for {}".format(name))
            continue
        try:
            inst.set_editor_property("world_asset", world_asset)
        except Exception as e:
            unreal.log_warning("[TESTRUN] world_asset for {}: {}".format(name, e))
        ba.finish_actor(inst, TAG, "BLK_TestRun_LI_{}".format(name), "TestRun/Arenas")
        attached += 1
    log("LevelInstance arenas attached: {}".format(attached))

    # Navigation: arena navmesh tiles never transform with instances, and the
    # editor's async rebuild finishes AFTER our save (project default is Static
    # runtime generation -> the game would load empty tiles and NPCs freeze).
    # Make THIS map's navmesh Dynamic: it rebuilds itself on game start.
    nav_actor = None
    for a in eas.get_all_level_actors():
        if a and a.get_class().get_name() == "RecastNavMesh":
            nav_actor = a
            break
    if nav_actor is None:
        # NavigationSystem creates its navdata asynchronously AFTER this script
        # (and after our save) - spawn it explicitly so the flag lands in the save.
        nav_actor = eas.spawn_actor_from_class(unreal.RecastNavMesh,
                                               unreal.Vector(0.0, 0.0, 0.0))
    if nav_actor:
        nav_actor.set_editor_property("runtime_generation",
                                      unreal.RuntimeGenerationType.DYNAMIC)
        log("RecastNavMesh '{}' runtime_generation -> Dynamic".format(
            nav_actor.get_actor_label()))
    else:
        unreal.log_warning("[TESTRUN] could not ensure RecastNavMesh - set "
                           "RuntimeGeneration=Dynamic manually on the map's navmesh")

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("RESULT: SUCCESS - {} (gauntlet order: {})".format(
        LEVEL_PATH, " -> ".join(n for n, _e, _z in ARENAS)))


main()
