# Basketball combat arena builder for Polarity.
#
# Data source: Arenas/A11_Basketball.json
# Output: /Game/Variant_Shooter/Arenas/Biome1/A11_Basketball/Lvl_A11_Basketball
# Idempotent: only actors tagged BLOCKOUT_A11_Basketball are rebuilt.

import json
import math
import os
import sys

import unreal

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.append(TOOLS_DIR)
import build_arena as ba  # noqa: E402

SPEC_PATH = os.path.join(TOOLS_DIR, "Arenas", "A11_Basketball.json")
ARENA = "A11_Basketball"
TAG = "BLOCKOUT_" + ARENA


def log(message):
    unreal.log("[BASKETBALL_ARENA] {}".format(message))


def warn(message):
    unreal.log_warning("[BASKETBALL_ARENA] {}".format(message))


def load_spec():
    with open(SPEC_PATH, "r", encoding="utf-8") as handle:
        return json.load(handle)


def set_prop(actor, name, value):
    if not actor:
        return False
    try:
        actor.set_editor_property(name, value)
        return True
    except Exception as exc:
        warn("{}: could not set {} ({})".format(actor.get_actor_label(), name, exc))
        return False


def spawn_actor(eas, cls, marker, label, folder):
    if cls is None:
        return None
    pos = marker.get("pos", [0, 0, 0])
    yaw = float(marker.get("yaw", 0.0))
    actor = eas.spawn_actor_from_class(
        cls, ba.vec(pos), unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
    if actor:
        ba.finish_actor(actor, TAG, label, folder)
        log("CREATED: {} at {}".format(label, pos))
    return actor


def markers_of(spec, marker_type):
    return [m for m in spec.get("markers", []) if m.get("type") == marker_type]


def spawn_entry_trigger(eas, spec):
    marker = markers_of(spec, "entry_trigger")[0]
    actor = eas.spawn_actor_from_class(unreal.TriggerBox, ba.vec(marker["pos"]))
    if actor:
        boxes = actor.get_components_by_class(unreal.BoxComponent)
        if boxes:
            boxes[0].set_box_extent(ba.vec(marker["extent"]))
        ba.finish_actor(actor, TAG, "BLK_A11_Basketball_EntryTrigger", "A11_Basketball/Logic")
        log("CREATED: entry trigger")
    return actor


def spawn_spawn_points(eas, spec, classes):
    result = []
    for marker in markers_of(spec, "spawn"):
        sid = marker["id"]
        actor = spawn_actor(
            eas, classes.get("spawn_point"), marker,
            "BLK_A11_Basketball_{}".format(sid), "A11_Basketball/Spawns")
        if not actor:
            continue
        if marker.get("air"):
            set_prop(actor, "air_spawn", True)
            set_prop(actor, "air_spawn_height", float(marker.get("air_height", 340.0)))
        result.append(actor)
    log("Spawned {} ArenaSpawnPoint actors".format(len(result)))
    return result


def spawn_prop_clusters(eas, spec, classes):
    prop_class = classes.get("prop")
    count = 0
    for marker in markers_of(spec, "prop_cluster"):
        amount = int(marker.get("count", 3))
        radius = float(marker.get("radius", 170.0))
        cx, cy, cz = marker["pos"]
        for index in range(amount):
            angle = index * math.tau / max(amount, 1)
            child = {
                "pos": [cx + math.cos(angle) * radius, cy + math.sin(angle) * radius, cz],
                "yaw": math.degrees(angle),
            }
            actor = spawn_actor(
                eas, prop_class, child,
                "BLK_A11_Basketball_Prop_{}_{}".format(marker["id"], index),
                "A11_Basketball/Props")
            if actor:
                count += 1
    log("Spawned {} EMF props".format(count))


def spawn_nav_links(eas, spec):
    count = 0
    for marker in markers_of(spec, "nav_link"):
        actor = eas.spawn_actor_from_class(unreal.NavLinkProxy, ba.vec(marker["pos"]))
        if not actor:
            warn("{} NavLinkProxy failed".format(marker["id"]))
            continue

        left = ba.vec(marker["left"])
        right = ba.vec(marker["right"])
        point_link = unreal.NavigationLink()
        point_link.set_editor_property("left", left)
        point_link.set_editor_property("right", right)
        point_link.set_editor_property("direction", unreal.NavLinkDirection.BOTH_WAYS)
        point_link.set_editor_property("snap_radius", 180.0)
        actor.set_editor_property("point_links", [point_link])

        actor.set_editor_property("smart_link_is_relevant", True)
        smart = actor.get_editor_property("smart_link_comp")
        smart.set_editor_property("link_relative_start", left)
        smart.set_editor_property("link_relative_end", right)
        smart.set_editor_property("link_direction", unreal.NavLinkDirection.BOTH_WAYS)
        smart.set_editor_property("link_enabled", True)

        ba.finish_actor(
            actor, TAG, "BLK_A11_Basketball_{}".format(marker["id"]),
            "A11_Basketball/Nav")
        count += 1
    log("Spawned {} smart NavLinkProxy actors".format(count))


def build_sustain_pool(spec, classes):
    pool = []
    for item in spec["sustain"]["pool"]:
        npc_class = classes.get(item["class"])
        if not npc_class:
            warn("Sustain pool skips missing class {}".format(item["class"]))
            continue
        entry = unreal.SustainSpawnEntry()
        entry.set_editor_property("npc_class", npc_class)
        entry.set_editor_property("weight", float(item["weight"]))
        pool.append(entry)
    return pool


def spawn_manager(eas, spec, classes, trigger, blockers, respawn):
    marker = markers_of(spec, "manager")[0]
    manager = spawn_actor(
        eas, classes.get("arena_manager"), marker,
        "BLK_A11_Basketball_ArenaManager", "A11_Basketball/Logic")
    if not manager:
        return None

    set_prop(manager, "arena_mode", unreal.ArenaMode.SUSTAIN)
    set_prop(manager, "sustain_enemy_pool", build_sustain_pool(spec, classes))
    set_prop(manager, "max_sustain_enemies", int(spec["sustain"]["max_alive"]))
    set_prop(manager, "sustain_total_enemies", int(spec["sustain"]["total"]))
    set_prop(manager, "entry_triggers", [trigger] if trigger else [])
    set_prop(manager, "exit_blockers", blockers)
    if respawn:
        set_prop(manager, "player_respawn_point", respawn)
    log("MODIFIED: ArenaManager Sustain max_alive={} total={}".format(
        spec["sustain"]["max_alive"], spec["sustain"]["total"]))
    return manager


def spawn_basketball(eas, spec, classes):
    marker = markers_of(spec, "basketball")[0]
    ball = spawn_actor(
        eas, classes.get("basketball"), marker,
        "BLK_A11_Basketball_Ball", "A11_Basketball/Basketball")
    if not ball:
        return None
    settings = spec["basketball"]
    set_prop(ball, "required_combat_score", int(settings["required_score"]))
    set_prop(ball, "capture_range", float(settings["capture_range"]))
    set_prop(ball, "min_launch_speed", float(settings["min_launch_speed"]))
    set_prop(ball, "max_launch_speed", float(settings["max_launch_speed"]))
    set_prop(ball, "max_throw_charge_time", float(settings["max_charge_time"]))
    set_prop(ball, "award_combat_score_on_non_lethal_hit", False)
    log("MODIFIED: Basketball score requirement -> {}".format(settings["required_score"]))
    return ball


def spawn_hoop(eas, spec, classes):
    marker = markers_of(spec, "basketball_hoop")[0]
    hoop = spawn_actor(
        eas, classes.get("basketball_hoop"), marker,
        "BLK_A11_Basketball_Hoop", "A11_Basketball/Basketball")
    if not hoop:
        return None
    set_prop(hoop, "require_charged_ball", True)
    set_prop(hoop, "assist_requires_charged_ball", True)
    log("MODIFIED: Hoop requires charged ball")
    return hoop


def ensure_dynamic_nav(eas):
    nav_actor = None
    for actor in eas.get_all_level_actors():
        if actor and actor.get_class().get_name() == "RecastNavMesh":
            nav_actor = actor
            break
    if nav_actor is None:
        nav_actor = eas.spawn_actor_from_class(unreal.RecastNavMesh, unreal.Vector(0, 0, 0))
    if nav_actor:
        set_prop(nav_actor, "runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)
        log("MODIFIED: RecastNavMesh runtime_generation -> Dynamic")


def build():
    spec = load_spec()
    level_path = spec["level_path"]
    ba.ensure_safe_to_build()
    les, eas = ba.get_subsystems()
    materials = ba.ensure_materials(force=False)
    ba.open_or_create_level(les, level_path)
    ba.report_sublevels("After load")
    ba.clear_tagged(eas, TAG, level_path)

    classes = {key: ba.resolve_class(path) for key, path in spec["classes"].items()}

    blockers = []
    piece_count = 0
    blocker_ids = set(spec.get("exit_blocker_ids", []))
    for piece in spec["pieces"]:
        actor = ba.spawn_shape(eas, materials, piece, TAG, ARENA)
        if actor:
            piece_count += 1
            if piece.get("no_collision"):
                mesh_components = actor.get_components_by_class(unreal.StaticMeshComponent)
                for component in mesh_components:
                    component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            if piece["id"] in blocker_ids:
                blockers.append(actor)
            log("CREATED: geometry {}".format(piece["id"]))
    log("Spawned {} geometry pieces".format(piece_count))

    ba.spawn_navmesh_bounds(eas, TAG, ARENA, spec)

    player_marker = markers_of(spec, "player_start")[0]
    respawn_marker = markers_of(spec, "respawn_point")[0]
    spawn_actor(eas, unreal.PlayerStart, player_marker,
                "BLK_A11_Basketball_PlayerStart", "A11_Basketball/Logic")
    respawn = spawn_actor(eas, unreal.TargetPoint, respawn_marker,
                          "BLK_A11_Basketball_Respawn", "A11_Basketball/Logic")
    trigger = spawn_entry_trigger(eas, spec)

    spawn_spawn_points(eas, spec, classes)
    spawn_prop_clusters(eas, spec, classes)
    spawn_nav_links(eas, spec)

    coordinator_marker = markers_of(spec, "coordinator")[0]
    spawn_actor(eas, classes.get("coordinator"), coordinator_marker,
                "BLK_A11_Basketball_Coordinator", "A11_Basketball/Logic")

    antenna_marker = markers_of(spec, "antenna")[0]
    spawn_actor(eas, classes.get("antenna"), antenna_marker,
                "BLK_A11_Basketball_Antenna", "A11_Basketball/Logic")

    manager = spawn_manager(eas, spec, classes, trigger, blockers, respawn)
    spawn_basketball(eas, spec, classes)
    spawn_hoop(eas, spec, classes)

    ensure_dynamic_nav(eas)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    unreal.SystemLibrary.execute_console_command(world, "RebuildNavigation")
    log("RebuildNavigation issued")

    ba.backup_level(level_path, ARENA)
    ba.report_sublevels("Before save")
    if not les.save_current_level():
        warn("save_current_level returned false")
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    ba.dump_actors(eas, TAG, ARENA, level_path)
    ba.take_screenshots(eas, spec, ARENA)
    log("RESULT: SUCCESS - {} built into {}".format(ARENA, level_path))


def main():
    try:
        build()
    except Exception:
        import traceback
        for line in traceback.format_exc().splitlines():
            warn(line)
        log("RESULT: FAILED")
        raise


main()
