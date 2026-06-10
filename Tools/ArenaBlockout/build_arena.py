# Arena blockout builder for Polarity.
#
# Builds (or rebuilds) an arena sublevel from a JSON spec. Idempotent: every actor it
# creates is tagged BLOCKOUT_<ArenaName>; rerunning deletes those and respawns from JSON.
#
# Usage:
#   In-editor console:  py "<...>/Source/Tools/ArenaBlockout/build_arena.py" A2_Courtyard
#   Headless (full editor; -run=pythonscript commandlet CRASHES on new_level — do not use):
#     UnrealEditor-Cmd.exe "<...>/Polarity.uproject" /Engine/Maps/Entry
#       -ExecCmds="py <bootstrap>.py" -EnablePlugins=PythonScriptPlugin
#       -nullrhi -nosound -unattended -nosplash -noLiveCoding
#     where the bootstrap sets sys.argv = [script, "A2_Courtyard", "--quit"] and exec()s
#     this file. --quit closes the editor when the build finishes (or fails).
#
# Spec files live in Arenas/<name>.json next to this script. After a successful build the
# script writes Build/<name>_dump.json with every spawned actor (class, location, bounds)
# for diff/verification, and saves the level + generated material assets.
#
# Log filter tag: [ARENA_BLOCKOUT]

import json
import math
import os
import sys

import unreal

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
MAT_DIR = "/Game/ArenaBlockout/Materials"

CUBE_MESH = "/Engine/BasicShapes/Cube.Cube"
CYL_MESH = "/Engine/BasicShapes/Cylinder.Cylinder"

# Blockout palette (R11 in LevelDesign.md): one neutral ramp + accents.
MATERIALS = {
    "floor":   (0.30, 0.30, 0.32),
    "wall":    (0.10, 0.10, 0.12),
    "deco":    (0.50, 0.48, 0.42),
    "wallrun": (0.05, 0.30, 0.85),
    "emf":     (0.95, 0.45, 0.05),
    "blocker": (0.75, 0.08, 0.08),
}


def log(msg):
    unreal.log("[ARENA_BLOCKOUT] {}".format(msg))


def warn(msg):
    unreal.log_warning("[ARENA_BLOCKOUT] {}".format(msg))


def vec(xyz):
    return unreal.Vector(float(xyz[0]), float(xyz[1]), float(xyz[2]))


def rot(spec):
    return unreal.Rotator(
        roll=float(spec.get("roll", 0.0)),
        pitch=float(spec.get("pitch", 0.0)),
        yaw=float(spec.get("yaw", 0.0)),
    )


def get_subsystems():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    return les, eas


def ensure_materials():
    """Create the flat-color blockout materials once; return name -> loaded asset."""
    eal = unreal.EditorAssetLibrary
    if not eal.does_directory_exist(MAT_DIR):
        eal.make_directory(MAT_DIR)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mel = unreal.MaterialEditingLibrary
    out = {}
    for name, rgb in MATERIALS.items():
        path = "{}/M_BLK_{}".format(MAT_DIR, name)
        if eal.does_asset_exist(path):
            out[name] = eal.load_asset(path)
            continue
        mat = tools.create_asset("M_BLK_{}".format(name), MAT_DIR, unreal.Material,
                                 unreal.MaterialFactoryNew())
        color = mel.create_material_expression(
            mat, unreal.MaterialExpressionConstant3Vector, -300, 0)
        color.set_editor_property(
            "constant", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
        mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
        rough = mel.create_material_expression(
            mat, unreal.MaterialExpressionConstant, -300, 220)
        rough.set_editor_property("r", 0.9)
        mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
        mel.recompile_material(mat)
        eal.save_asset(path)
        out[name] = mat
        log("Created material {}".format(path))
    return out


def resolve_class(path):
    """Load a class from '/Script/Module.Class' or a blueprint asset path."""
    if path.startswith("/Script/"):
        cls = unreal.load_class(None, path)
    else:
        cls = unreal.EditorAssetLibrary.load_blueprint_class(path)
    if cls is None:
        warn("Class not found: {}".format(path))
    return cls


def open_or_create_level(les, level_path):
    if unreal.EditorAssetLibrary.does_asset_exist(level_path):
        if not les.load_level(level_path):
            raise RuntimeError("Failed to load level " + level_path)
        log("Loaded existing level {}".format(level_path))
    else:
        if not les.new_level(level_path):
            raise RuntimeError("Failed to create level " + level_path)
        log("Created new level {}".format(level_path))


def clear_tagged(eas, tag):
    removed = 0
    for actor in list(eas.get_all_level_actors()):
        try:
            if tag in [str(t) for t in actor.tags]:
                eas.destroy_actor(actor)
                removed += 1
        except Exception:
            pass
    log("Removed {} previously generated actors".format(removed))


def finish_actor(actor, tag, label, folder):
    actor.set_actor_label(label)
    actor.set_folder_path(folder)
    tags = list(actor.tags)
    tags.append(unreal.Name(tag))
    actor.set_editor_property("tags", tags)
    return actor


def spawn_shape(eas, mats, piece, tag, arena):
    shape = piece.get("shape", "box")
    mesh_path = CYL_MESH if shape == "cylinder" else CUBE_MESH
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    actor = eas.spawn_actor_from_object(mesh, vec(piece["pos"]), rot(piece))
    if actor is None:
        warn("Failed to spawn piece {}".format(piece.get("id", "?")))
        return None
    size = piece["size"]
    actor.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    mat_name = piece.get("mat", "floor")
    smc = actor.static_mesh_component
    smc.set_material(0, mats.get(mat_name, mats["floor"]))
    group = piece.get("group", "Geo")
    return finish_actor(actor, tag, "BLK_{}_{}".format(arena, piece.get("id", "piece")),
                        "{}/{}".format(arena, group))


def spawn_lights(eas, tag, arena, bounds_z=2000.0):
    sun = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, bounds_z),
                                     unreal.Rotator(roll=0.0, pitch=-55.0, yaw=35.0))
    finish_actor(sun, tag, "BLK_{}_Sun".format(arena), "{}/Lighting".format(arena))
    sky = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, bounds_z))
    try:
        comp = sky.get_component_by_class(unreal.SkyLightComponent)
        comp.set_editor_property("real_time_capture", True)
    except Exception:
        warn("SkyLight real_time_capture not set (API mismatch) — set manually")
    finish_actor(sky, tag, "BLK_{}_SkyLight".format(arena), "{}/Lighting".format(arena))
    atm = eas.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0))
    finish_actor(atm, tag, "BLK_{}_SkyAtmosphere".format(arena), "{}/Lighting".format(arena))
    log("Spawned blockout lighting")


def spawn_navmesh_bounds(eas, tag, arena, spec):
    nav_spec = spec.get("navmesh_bounds")
    if not nav_spec:
        return
    actor = eas.spawn_actor_from_class(unreal.NavMeshBoundsVolume, vec(nav_spec["pos"]))
    if actor is None:
        warn("NavMeshBoundsVolume failed to spawn — add manually in editor")
        return
    size = nav_spec["size"]
    # Default brush is a 200uu cube; scale to requested size.
    actor.set_actor_scale3d(unreal.Vector(size[0] / 200.0, size[1] / 200.0, size[2] / 200.0))
    finish_actor(actor, tag, "BLK_{}_NavBounds".format(arena), "{}/Nav".format(arena))
    log("Spawned NavMeshBoundsVolume {}x{}x{} (verify navmesh builds after opening level)"
        .format(size[0], size[1], size[2]))


def spawn_trigger(eas, tag, arena, marker, idx):
    actor = eas.spawn_actor_from_class(unreal.TriggerBox, vec(marker["pos"]), rot(marker))
    comps = actor.get_components_by_class(unreal.BoxComponent)
    if comps:
        ext = marker.get("extent", [300, 100, 150])
        comps[0].set_box_extent(vec(ext))
    return finish_actor(actor, tag, "BLK_{}_EntryTrigger_{}".format(arena, idx),
                        "{}/Logic".format(arena))


def build_waves(spec, classes):
    waves = []
    for wave_spec in spec.get("waves", []):
        entries = []
        for npc_key, count in wave_spec.get("entries", []):
            cls = classes.get(npc_key)
            if cls is None:
                warn("Wave entry skipped, unknown npc key '{}'".format(npc_key))
                continue
            entry = unreal.ArenaSpawnEntry()
            entry.set_editor_property("npc_class", cls)
            entry.set_editor_property("count", int(count))
            entries.append(entry)
        wave = unreal.ArenaWave()
        wave.set_editor_property("entries", entries)
        wave.set_editor_property("delay_before_wave", float(wave_spec.get("delay", 0.0)))
        waves.append(wave)
    return waves


def dump_actors(eas, tag, arena):
    rows = []
    for actor in eas.get_all_level_actors():
        if tag not in [str(t) for t in actor.tags]:
            continue
        origin, extent = actor.get_actor_bounds(False)
        loc = actor.get_actor_location()
        rows.append({
            "label": actor.get_actor_label(),
            "class": actor.get_class().get_name(),
            "loc": [round(loc.x, 1), round(loc.y, 1), round(loc.z, 1)],
            "bounds_origin": [round(origin.x, 1), round(origin.y, 1), round(origin.z, 1)],
            "bounds_extent": [round(extent.x, 1), round(extent.y, 1), round(extent.z, 1)],
        })
    out_dir = os.path.join(TOOLS_DIR, "Build")
    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)
    out_path = os.path.join(out_dir, "{}_dump.json".format(arena))
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(rows, f, indent=1, ensure_ascii=False)
    log("Dumped {} actors to {}".format(len(rows), out_path))


def build(arena_name):
    spec_path = os.path.join(TOOLS_DIR, "Arenas", arena_name + ".json")
    if not os.path.isfile(spec_path):
        raise RuntimeError("Spec not found: " + spec_path)
    with open(spec_path, "r", encoding="utf-8") as f:
        spec = json.load(f)

    arena = spec["name"]
    tag = "BLOCKOUT_" + arena
    level_path = spec["level_path"]

    les, eas = get_subsystems()
    mats = ensure_materials()
    open_or_create_level(les, level_path)
    clear_tagged(eas, tag)

    classes = {key: resolve_class(path) for key, path in spec.get("classes", {}).items()}

    # --- geometry ---
    blockers = {}
    count_geo = 0
    for piece in spec.get("pieces", []):
        actor = spawn_shape(eas, mats, piece, tag, arena)
        if actor and piece.get("mat") == "blocker":
            blockers[piece.get("id")] = actor
        if actor:
            count_geo += 1
    log("Spawned {} geometry pieces".format(count_geo))

    spawn_lights(eas, tag, arena)
    spawn_navmesh_bounds(eas, tag, arena, spec)

    # --- markers / gameplay actors ---
    triggers = []
    spawn_point_count = 0
    antenna_actor = None
    button_actor = None
    respawn_actor = None
    manager_marker = None

    for idx, marker in enumerate(spec.get("markers", [])):
        mtype = marker["type"]
        if mtype == "manager":
            manager_marker = marker
            continue
        if mtype == "entry_trigger":
            triggers.append(spawn_trigger(eas, tag, arena, marker, len(triggers)))
            continue
        if mtype == "player_start":
            actor = eas.spawn_actor_from_class(unreal.PlayerStart, vec(marker["pos"]), rot(marker))
            finish_actor(actor, tag, "BLK_{}_PlayerStart".format(arena), "{}/Logic".format(arena))
            continue
        if mtype == "respawn_point":
            respawn_actor = eas.spawn_actor_from_class(unreal.TargetPoint, vec(marker["pos"]), rot(marker))
            finish_actor(respawn_actor, tag, "BLK_{}_Respawn".format(arena), "{}/Logic".format(arena))
            continue
        if mtype == "spawn":
            cls = classes.get("spawn_point")
            actor = eas.spawn_actor_from_class(cls, vec(marker["pos"]), rot(marker))
            if marker.get("air", False):
                actor.set_editor_property("air_spawn", True)
                if "air_height" in marker:
                    actor.set_editor_property("air_spawn_height", float(marker["air_height"]))
            label = marker.get("id", "Spawn{}".format(idx))
            finish_actor(actor, tag, "BLK_{}_{}".format(arena, label), "{}/Spawns".format(arena))
            spawn_point_count += 1
            continue
        if mtype == "prop_cluster":
            cls = classes.get("prop")
            n = int(marker.get("count", 4))
            radius = float(marker.get("radius", 180))
            cx, cy, cz = marker["pos"]
            for i in range(n):
                ang = (2.0 * math.pi / n) * i
                pos = [cx + radius * math.cos(ang), cy + radius * math.sin(ang), cz]
                actor = eas.spawn_actor_from_class(cls, vec(pos))
                if actor:
                    finish_actor(actor, tag, "BLK_{}_Prop_{}_{}".format(arena, marker.get("id", idx), i),
                                 "{}/Props".format(arena))
            continue
        if mtype in ("antenna", "button", "coordinator", "plate"):
            cls = classes.get(mtype)
            actor = eas.spawn_actor_from_class(cls, vec(marker["pos"]), rot(marker))
            if actor is None:
                warn("Failed to spawn marker '{}'".format(mtype))
                continue
            finish_actor(actor, tag, "BLK_{}_{}".format(arena, marker.get("id", mtype)),
                         "{}/Logic".format(arena))
            if mtype == "antenna":
                antenna_actor = actor
            elif mtype == "button":
                button_actor = actor
            continue
        warn("Unknown marker type '{}'".format(mtype))

    log("Spawned {} spawn points, {} entry triggers".format(spawn_point_count, len(triggers)))

    # --- antenna <-> button pairing ---
    if antenna_actor and button_actor:
        try:
            antenna_actor.set_editor_property("interaction_button", button_actor)
            log("Paired antenna with button")
        except Exception as e:
            warn("Antenna/button pairing failed: {}".format(e))

    # --- arena manager ---
    if manager_marker:
        cls = classes.get("arena_manager")
        manager = eas.spawn_actor_from_class(cls, vec(manager_marker["pos"]))
        if manager:
            finish_actor(manager, tag, "BLK_{}_ArenaManager".format(arena), "{}/Logic".format(arena))
            try:
                manager.set_editor_property("waves", build_waves(spec, classes))
                manager.set_editor_property("time_between_waves",
                                            float(spec.get("time_between_waves", 3.0)))
                if triggers:
                    manager.set_editor_property("entry_triggers", triggers)
                blocker_actors = [blockers[bid] for bid in spec.get("exit_blocker_ids", [])
                                  if bid in blockers]
                if blocker_actors:
                    manager.set_editor_property("exit_blockers", blocker_actors)
                if respawn_actor:
                    manager.set_editor_property("player_respawn_point", respawn_actor)
                log("ArenaManager configured: {} waves, {} triggers, {} blockers".format(
                    len(spec.get("waves", [])), len(triggers), len(blocker_actors)))
            except Exception as e:
                warn("ArenaManager wiring failed: {} — configure in Details panel".format(e))
        else:
            warn("ArenaManager failed to spawn")
    else:
        warn("No manager marker in spec")

    # --- save + dump ---
    if not les.save_current_level():
        warn("save_current_level returned false")
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    dump_actors(eas, tag, arena)
    log("DONE: {} built into {}".format(arena, level_path))


def main():
    args = [a for a in sys.argv[1:] if a and not a.startswith("-")]
    quit_when_done = "--quit" in sys.argv[1:]
    ok = False
    try:
        if not args:
            raise RuntimeError("Usage: build_arena.py <ArenaSpecName> [--quit]")
        build(args[0])
        ok = True
    except Exception:
        import traceback
        for line in traceback.format_exc().splitlines():
            warn(line)
    finally:
        log("RESULT: {}".format("SUCCESS" if ok else "FAILED"))
        if quit_when_done:
            log("Quitting editor (--quit)")
            unreal.SystemLibrary.quit_editor()


main()
