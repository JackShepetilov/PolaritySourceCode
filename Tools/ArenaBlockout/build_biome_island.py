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
ARENA_LIFT = 10.0          # arenas float 10 uu above their pads (no z-fighting)
SHORE_RUN = 1.4           # keep in sync with make_biome1_heightmap.py
BRIDGE_OVERLAP = 800.0    # each end buried into the beach
AUTHOR_SUBLEVELS = ["/Game/Variant_Shooter/Arenas/ArenaDebug/ArenaLightingDebug3",
                    "/Game/Variant_Shooter/Arenas/Biome1/RunLogic/Lvl_RunLogic"]


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
    probes.append(("G3 bowl ctr", slots["G3"]["pos"][0], slots["G3"]["pos"][1],
                   slots["G3"]["pos"][2] - 625.0))
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
                abs(loc.z - (pos[2] + ARENA_LIFT)) < 1.0 and
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
        # explicit yaw override (guard ring etc.) beats the enter_from formula
        yaw = float(s["yaw"]) if "yaw" in s else round(entrance_yaw(s, slots), 1)
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
            location=unreal.Vector(pos[0], pos[1], pos[2] + ARENA_LIFT),
            rotation=unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
        sl2 = unreal.EditorLevelUtils.add_level_to_world_with_transform(
            world, pkg, unreal.LevelStreamingAlwaysLoaded, xform)
        if sl2:
            log("Attached {} [{}] at {} yaw={}".format(name, s["id"], pos, yaw))
        else:
            warn("failed to attach sublevel {}".format(pkg))


def march_to_rim(svc, from_xy, to_xy, drop_below):
    """March from from_xy toward to_xy (400 uu steps); return the LAST point
    (x, y, h) before the terrain drops below `drop_below` - i.e. the elevated
    rim edge. Bridges must connect heights, not lowlands (author 2026-06-11)."""
    ax, ay = from_xy
    bx, by = to_xy
    dist = math.hypot(bx - ax, by - ay)
    ux, uy = (bx - ax) / dist, (by - ay) / dist
    t = 0.0
    last = None
    while t < dist:
        x, y = ax + ux * t, ay + uy * t
        s = svc.get_height_at_location(LS_LABEL, x, y)
        h = s.height if s.valid else -99999.0
        if h < drop_below and last is not None:
            return last
        if h >= drop_below:
            last = (x, y, h)
        t += 400.0
    return last if last is not None else (bx, by, drop_below)


def build_bridges(eas, mats, slots, layout_island_center):
    """Straight plank stubs across the water hops of the maldive chain
    (real valves become their own sublevels later). Each end is found by
    sampling the actual terrain down to the waterline, then buried slightly
    into the beach."""
    svc = unreal.LandscapeService
    hops = [("M1", "M2"), ("M2", "M3"), ("M3", "M4"), ("M4", "shoulder")]
    for a_id, b_id in hops:
        a, b = slots[a_id], slots[b_id]
        a_xy = (a["pos"][0], a["pos"][1])
        b_xy = (b["pos"][0], b["pos"][1])
        if b["kind"] == "arena":
            x0, y0, h0 = march_to_rim(svc, a_xy, b_xy, a["pos"][2] - 140.0)
            x1, y1, h1 = march_to_rim(svc, b_xy, a_xy, b["pos"][2] - 140.0)
        else:
            # final hop: bridge to the NEAREST island shore (march toward the
            # island center), the player walks the beach onward - not a giant
            # span to the distant shoulder point
            icx, icy = layout_island_center
            x0, y0, h0 = march_to_rim(svc, a_xy, (icx, icy), a["pos"][2] - 140.0)
            x1, y1, h1 = march_to_rim(svc, (icx, icy), a_xy, 330.0)
        span = math.hypot(x1 - x0, y1 - y0)
        if span < 500.0:
            log("Valve {}->{}: islets touch - no bridge needed".format(a_id, b_id))
            continue
        piece = {"id": "valve_{}_{}".format(a_id, b_id), "shape": "ramp",
                 "mat": "deco", "group": "Valves",
                 "from": [x0, y0, h0 + 20.0], "to": [x1, y1, h1 + 20.0],
                 "width": 700.0, "thick": 80.0}
        ba.spawn_shape(eas, mats, piece, TAG, ARENA)
        log("Valve ramp {}->{}: {:.0f} uu, rim h {:.0f} -> {:.0f}".format(
            a_id, b_id, span, h0, h1))


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
        # NORMALIZE the zone every run: a collapsed/dragged zone (seen 1x1 at a
        # random location 2026-06-11) desyncs the rendered surface from the
        # underwater post-process
        try:
            zone.set_actor_location(unreal.Vector(0.0, 0.0, 0.0), False, False)
            zone.set_editor_property("zone_extent", unreal.Vector2D(220000.0, 220000.0))
            log("WaterZone normalized: loc (0,0,0), extent 220000x220000")
        except Exception as e:
            warn("WaterZone normalize failed: {}".format(e))


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


CANONICAL_VALVES = {"BLK_Biome1Island_valve_M1_M2", "BLK_Biome1Island_valve_M2_M3",
                    "BLK_Biome1Island_valve_M3_M4", "BLK_Biome1Island_valve_M4_shoulder"}


def cleanup_foreign_orphans(eas):
    """Earlier builds spawned bridges/nav into whatever sublevel was CURRENT
    (attaching a sublevel makes it current) - delete those orphans from arena
    packages. The author's own copies (suffixed labels, e.g. valve_M4_shoulder2)
    are never touched."""
    removed = 0
    for a in list(eas.get_all_level_actors()):
        if a is None:
            continue
        try:
            pkg = a.get_package().get_name()
        except Exception:
            continue
        if pkg == LEVEL_PATH or not pkg.startswith(ARENA_ROOT + "/"):
            continue
        lbl = a.get_actor_label()
        cls = a.get_class().get_name()
        if lbl.startswith("BLK_IslandSpike_"):
            # spike-era junk that leaked into arena packages via the
            # current-level spawn bug (launch marker, strait bridge, ...)
            eas.destroy_actor(a)
            removed += 1
            log("Removed spike stray '{}' from {}".format(lbl, pkg))
            continue
        if lbl in CANONICAL_VALVES:
            eas.destroy_actor(a)
            removed += 1
            log("Removed orphaned bridge '{}' from {}".format(lbl, pkg))
        elif cls == "RecastNavMesh":
            try:
                if a.get_editor_property("runtime_generation") ==                         unreal.RuntimeGenerationType.DYNAMIC:
                    eas.destroy_actor(a)
                    removed += 1
                    log("Removed stray DYNAMIC RecastNavMesh from {}".format(pkg))
            except Exception:
                pass
    if removed:
        log("Foreign-package cleanup: {} orphan(s) removed (their packages will "
            "be saved at the end)".format(removed))


def ensure_dynamic_nav(eas):
    nav = None
    for a in eas.get_all_level_actors():
        if a and a.get_class().get_name() == "RecastNavMesh" and                 ba.in_package(a, LEVEL_PATH):
            nav = a
            break
    if nav is None:
        nav = eas.spawn_actor_from_class(unreal.RecastNavMesh, unreal.Vector(0.0, 0.0, 0.0))
    if nav:
        nav.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)
        log("RecastNavMesh '{}' runtime_generation -> Dynamic".format(nav.get_actor_label()))
    else:
        warn("could not ensure RecastNavMesh - set RuntimeGeneration=Dynamic manually")


def presave_pipeline_maps():
    """Make the dirty-map guard pass without ever cementing author gestures:
    - the island map + the lighting sublevel are OURS to save (snap-light and
      attach side effects) - save just those;
    - dirty ARENA maps are NEVER saved here: if the author dragged arena actors
      to show a new slot (sync workflow), `--discard-arena-edits` reloads them
      from disk (the drag intent lives in the layout JSON by then); without the
      flag they stay dirty and the guard refuses loudly."""
    dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    if not dirty:
        return
    keep = [p for p in dirty
            if p.get_name() == LEVEL_PATH or p.get_name() in AUTHOR_SUBLEVELS]
    arena_dirty = [p for p in dirty if p not in keep
                   and p.get_name().startswith(ARENA_ROOT + "/")]
    if keep:
        unreal.EditorLoadingAndSavingUtils.save_packages(keep, True)
        log("Pre-saved pipeline maps: {}".format([p.get_name() for p in keep]))
    if arena_dirty:
        if "--discard-arena-edits" in sys.argv:
            reloaded, err = unreal.EditorLoadingAndSavingUtils.reload_packages(
                arena_dirty,
                interaction_mode=unreal.ReloadPackagesInteractionMode.ASSUME_POSITIVE)
            log("Discarded in-editor arena edits (slot gestures already synced): "
                "{} (reloaded={}, err='{}')".format(
                    [p.get_name() for p in arena_dirty], reloaded, err))
        else:
            log("Dirty ARENA maps present: {} - NOT saving them (run sync first, "
                "then rebuild with --discard-arena-edits)".format(
                    [p.get_name() for p in arena_dirty]))


def main():
    if hasattr(ba, "ensure_safe_to_build"):
        ba.ensure_safe_to_build()
    layout, slots = load_layout()
    les, eas = ba.get_subsystems()
    presave_pipeline_maps()
    ba.open_or_create_level(les, LEVEL_PATH)  # refuses over unsaved maps
    ba.backup_level(LEVEL_PATH, ARENA)
    ba.clear_tagged(eas, TAG, LEVEL_PATH)

    ensure_landscape(slots)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    cleanup_foreign_orphans(eas)
    # SPAWN EVERYTHING FIRST: attaching a sublevel makes it the CURRENT level
    # and editor spawns land in the current level (learned 2026-06-11: bridges
    # ended up inside Lvl_A6_Villa). Spawns go before any attach.
    ensure_water(eas)
    ensure_dynamic_nav(eas)
    mats = ba.ensure_materials(force=False)
    build_bridges(eas, mats, slots, layout["island"]["center"])
    ensure_arenas(world, layout, slots)
    ensure_author_extras(world)
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
