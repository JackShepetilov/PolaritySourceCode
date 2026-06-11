# Builds the REAL Biome1 island map (Lvl_Biome1_Island) from the layout source
# of truth (Island/biome1_island_layout.json) + the authored heightmap
# (Island/biome1_heightmap_2017.png, made by make_biome1_heightmap.py).
#
# Maldive model (author 2026-06-11): M1-M4 each on their own arena-sized
# micro-island, arena DEAD-CENTER, entrance yaw faces back along the incoming
# leg (the author's arrows). Guards G1-G3 + citadel(=A6_Villa) on the big
# island; G3 sits in a carved bowl (A5 terraces go to -900).
#
# AUTHOR-OWNED (never touched/spawned here): RunLaunchPoint, debug character,
# his sublevels. Lighting comes from ArenaLightingDebug3 (re-attached if absent).
#
# Usage (in-editor): py "<...>/build_biome_island.py"
# Idempotent: heightmap import is absolute; arenas re-attach only when their
# slot transform changed; BLOCKOUT_Biome1Island actors (bridges) are rebuilt.
# Log tag: [BIOME_ISLAND]

import json
import math
import os
import sys

import unreal

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.append(TOOLS_DIR)
import build_arena as ba  # noqa: E402

LAYOUT = os.path.join(TOOLS_DIR, "Island", "biome1_island_layout.json")
HEIGHTMAP = os.path.join(TOOLS_DIR, "Island", "biome1_heightmap_2017.png")
LEVEL_PATH = "/Game/Variant_Shooter/Arenas/Biome1/Island/Lvl_Biome1_Island"
TAG = "BLOCKOUT_Biome1Island"
ARENA = "Biome1Island"
LS_LABEL = "Biome1Island"
ARENA_ROOT = "/Game/Variant_Shooter/Arenas/Biome1"
SEA_FLOOR_Z = -2000.0
WATER_Z = 0.0
PAD_RIM = 900.0
SHORE_RUN = 1.4           # keep in sync with make_biome1_heightmap.py
BRIDGE_OVERLAP = 800.0    # each end buried into the beach
AUTHOR_SUBLEVELS = ["/Game/Variant_Shooter/Arenas/ArenaDebug/ArenaLightingDebug3"]


def log(msg):
    unreal.log("[BIOME_ISLAND] {}".format(msg))


def warn(msg):
    unreal.log_warning("[BIOME_ISLAND] {}".format(msg))


def load_layout():
    with open(LAYOUT, encoding="utf-8") as f:
        layout = json.load(f)
    return layout, {s["id"]: s for s in layout["slots"]}


def entrance_yaw(slot, slots):
    """World yaw so the arena's LOCAL -Y entrance faces its enter_from node
    (= the author's entrance-axis arrows along the route)."""
    tgt = slots[slot["enter_from"]]["pos"]
    dx, dy = tgt[0] - slot["pos"][0], tgt[1] - slot["pos"][1]
    n = math.hypot(dx, dy)
    return math.degrees(math.atan2(dx / n, -dy / n))


def ensure_landscape(slots):
    if not hasattr(unreal, "LandscapeService"):
        raise RuntimeError("unreal.LandscapeService missing - is the VibeUE plugin loaded?")
    svc = unreal.LandscapeService
    if not svc.landscape_exists(LS_LABEL):
        half = (2017 - 1) * 100 * 0.5
        res = svc.create_landscape(
            location=unreal.Vector(-half, -half, SEA_FLOOR_Z),
            rotation=unreal.Rotator(0.0, 0.0, 0.0),
            scale=unreal.Vector(100.0, 100.0, 100.0),
            sections_per_component=2,
            quads_per_section=63,
            component_count_x=16,
            component_count_y=16,
            landscape_label=LS_LABEL)
        if not res.success:
            raise RuntimeError("create_landscape failed: " + str(res.error_message))
        log("Created landscape {} (2017x2017 @ 100 = ~2x2 km, base Z {})".format(
            LS_LABEL, SEA_FLOOR_Z))
    if not os.path.isfile(HEIGHTMAP):
        raise RuntimeError("Heightmap missing: {} - run make_biome1_heightmap.py"
                           .format(HEIGHTMAP))
    imp = svc.import_heightmap(LS_LABEL, HEIGHTMAP)
    if not getattr(imp, "success", False):
        raise RuntimeError("import_heightmap failed: "
                           + str(getattr(imp, "error_message", "?")))
    log("Heightmap imported: {} ({})".format(
        os.path.basename(HEIGHTMAP), getattr(imp, "resolution", "?")))
    probes = [(sid, slots[sid]["pos"][0], slots[sid]["pos"][1], slots[sid]["pos"][2])
              for sid in ("M1", "M2", "M3", "M4", "shoulder", "G1", "G2", "Citadel")]
    probes.append(("G3 bowl ctr", slots["G3"]["pos"][0], slots["G3"]["pos"][1], 8375.0))
    probes.append(("open sea", 70000.0, -70000.0, -2000.0))
    bad = 0
    for label, x, y, want in probes:
        s = svc.get_height_at_location(LS_LABEL, float(x), float(y))
        if not s.valid:
            warn("Probe {}: INVALID".format(label))
            bad += 1
            continue
        ok = abs(s.height - want) <= max(90.0, abs(want) * 0.05)
        if not ok:
            bad += 1
        log("Probe {}: {:.0f} (want ~{:.0f}) {}".format(
            label, s.height, want, "OK" if ok else "MISMATCH"))
    if bad:
        warn("{} probe(s) off - terrain mapping needs attention".format(bad))


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


def ensure_arenas(world, layout, slots):
    """Attach every arena slot as a classic streaming sublevel (8a.1 canon),
    entrance yaw per the author's arrows. Detach+re-add when a slot moved."""
    for s in layout["slots"]:
        if s["kind"] != "arena":
            continue
        arena_name = s["default"]
        name = "Lvl_" + arena_name
        pkg = "{}/{}/{}".format(ARENA_ROOT, arena_name, name)
        pos = s["pos"]
        yaw = round(entrance_yaw(s, slots), 1)
        sl = unreal.GameplayStatics.get_streaming_level(world, pkg) or \
            unreal.GameplayStatics.get_streaming_level(world, name)
        if sl and _transform_matches(sl, pos, yaw):
            log("{} [{}] already attached with the right transform".format(
                name, s["id"]))
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
            log("Attached {} [{}] at {} yaw={}".format(name, s["id"], pos, yaw))
        else:
            warn("failed to attach sublevel {}".format(pkg))


def shore_dist(slot):
    """Approx distance from slot center to its island waterline."""
    if slot["kind"] != "arena":
        return 1200.0  # waypoint on the big island - bridge end lands on the beach
    return (slot["r"] + PAD_RIM) + slot["pos"][2] * SHORE_RUN


def build_bridges(eas, mats, slots):
    """Straight plank stubs across the water hops of the maldive chain
    (real valves become their own sublevels later)."""
    hops = [("M1", "M2"), ("M2", "M3"), ("M3", "M4"), ("M4", "shoulder")]
    for a_id, b_id in hops:
        a, b = slots[a_id], slots[b_id]
        ax, ay = a["pos"][0], a["pos"][1]
        bx, by = b["pos"][0], b["pos"][1]
        dx, dy = bx - ax, by - ay
        dist = math.hypot(dx, dy)
        ux, uy = dx / dist, dy / dist
        sa, sb = shore_dist(a), shore_dist(b)
        x0, y0 = ax + ux * (sa - BRIDGE_OVERLAP), ay + uy * (sa - BRIDGE_OVERLAP)
        x1, y1 = bx - ux * (sb - BRIDGE_OVERLAP), by - uy * (sb - BRIDGE_OVERLAP)
        length = math.hypot(x1 - x0, y1 - y0)
        piece = {"id": "valve_{}_{}".format(a_id, b_id), "shape": "box",
                 "mat": "deco", "group": "Valves",
                 "pos": [(x0 + x1) * 0.5, (y0 + y1) * 0.5, 120.0],
                 "size": [length, 700.0, 60.0],
                 "yaw": math.degrees(math.atan2(dy, dx))}
        ba.spawn_shape(eas, mats, piece, TAG, ARENA)
        log("Valve stub {}->{}: {:.0f} uu".format(a_id, b_id, length))


def ensure_water(eas):
    if not hasattr(unreal, "WaterBodyOcean"):
        warn("Water plugin classes missing - skipping ocean")
        return
    classes = {a.get_class().get_name() for a in eas.get_all_level_actors() if a}
    if "WaterBodyOcean" not in classes:
        ocean = eas.spawn_actor_from_class(
            unreal.WaterBodyOcean, unreal.Vector(0.0, 0.0, WATER_Z),
            unreal.Rotator(0.0, 0.0, 0.0))
        if ocean:
            ocean.set_actor_label("Biome1_Ocean")
            ocean.set_folder_path("Biome1Island/Water")
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
            zone.set_actor_label("Biome1_WaterZone")
            zone.set_folder_path("Biome1Island/Water")
            log("Spawned WaterZone")
    if zone is not None:
        try:
            zone.set_editor_property("zone_extent", unreal.Vector2D(220000.0, 220000.0))
        except Exception as e:
            warn("zone_extent not settable: {}".format(e))


def ensure_author_extras(world):
    for pkg in AUTHOR_SUBLEVELS:
        short = pkg.rsplit("/", 1)[-1]
        if unreal.GameplayStatics.get_streaming_level(world, pkg) or \
           unreal.GameplayStatics.get_streaming_level(world, short):
            log("Author sublevel {} in place".format(short))
            continue
        sl = unreal.EditorLevelUtils.add_level_to_world(
            world, pkg, unreal.LevelStreamingAlwaysLoaded)
        if sl:
            log("Attached author lighting sublevel {}".format(short))
        else:
            warn("Could not attach {} - add it in the Levels panel".format(pkg))
    log("Reminder: RunLaunchPoint / debug character are author-placed (script "
        "never spawns or touches them)")


def ensure_dynamic_nav(eas):
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
        ba.ensure_safe_to_build()
    layout, slots = load_layout()
    les, eas = ba.get_subsystems()
    ba.open_or_create_level(les, LEVEL_PATH)  # refuses over unsaved maps
    ba.backup_level(LEVEL_PATH, ARENA)
    ba.clear_tagged(eas, TAG, LEVEL_PATH)

    ensure_landscape(slots)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    ensure_water(eas)
    ensure_arenas(world, layout, slots)
    ensure_author_extras(world)
    mats = ba.ensure_materials(force=False)
    build_bridges(eas, mats, slots)
    ensure_dynamic_nav(eas)
    try:
        unreal.SystemLibrary.execute_console_command(world, "RebuildNavigation")
        log("RebuildNavigation issued")
    except Exception as e:
        warn("RebuildNavigation failed: {}".format(e))
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("RESULT: SUCCESS - {} (8 arenas on layout v{} slots, maldive chain "
        "bridged, citadel=A6 on the cliff)".format(
            LEVEL_PATH, layout.get("version", "?")))


main()
