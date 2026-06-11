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


FLATCOL_PARENT = "/Game/LevelPrototyping/Materials/M_FlatCol"


def ensure_materials(force=False):
    """Create colored MaterialInstanceConstants of the template's M_FlatCol.

    Hand-building UMaterial graphs from python proved fragile (assets authored in a
    NullRHI commandlet session render as the checkerboard fallback forever). Material
    INSTANCES of an existing, known-good parent are deterministic instead."""
    eal = unreal.EditorAssetLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mel = unreal.MaterialEditingLibrary
    if force and eal.does_directory_exist(MAT_DIR):
        eal.delete_directory(MAT_DIR)
        log("Deleted old material dir {}".format(MAT_DIR))
    if not eal.does_directory_exist(MAT_DIR):
        eal.make_directory(MAT_DIR)
    parent = eal.load_asset(FLATCOL_PARENT)
    if parent is None:
        raise RuntimeError("Parent material not found: " + FLATCOL_PARENT)
    out = {}
    param_name = None
    for name, rgb in MATERIALS.items():
        path = "{}/MI_BLK_{}".format(MAT_DIR, name)
        if eal.does_asset_exist(path):
            mic = eal.load_asset(path)
        else:
            mic = tools.create_asset("MI_BLK_{}".format(name), MAT_DIR,
                                     unreal.MaterialInstanceConstant,
                                     unreal.MaterialInstanceConstantFactoryNew())
            if mic is None:
                raise RuntimeError("Failed to create material instance " + path)
            mel.set_material_instance_parent(mic, parent)
            log("Created material instance {}".format(path))
        if param_name is None:
            names = [str(n) for n in mel.get_vector_parameter_names(mic)]
            if not names:
                raise RuntimeError("M_FlatCol has no vector parameters to tint")
            preferred = [n for n in names if "color" in n.lower() or "tint" in n.lower()]
            param_name = preferred[0] if preferred else names[0]
            log("Using vector param '{}'".format(param_name))
        # Re-apply the tint EVERY run: parameter overrides set in a previous editor
        # session render as the parent's default grey otherwise (observed empirically).
        mel.set_material_instance_vector_parameter_value(
            mic, param_name, unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
        mel.update_material_instance(mic)
        eal.save_asset(path)
        out[name] = mic
    # Containment field: translucent glass for the "стакан" walls that keep drones
    # (and the player) inside during combat. Wired into ExitBlockers via the spec.
    glass = eal.load_asset("/Game/LevelPrototyping/PolygonPrototype/Materials/M_PolygonPrototype_Glass")
    out["field"] = glass if glass else out["wall"]
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


def level_disk_path(level_path):
    """/Game/Foo/Bar -> <ProjectDir>/Content/Foo/Bar.umap"""
    content = unreal.SystemLibrary.get_project_content_directory()
    return os.path.normpath(os.path.join(content, level_path.replace("/Game/", "", 1) + ".umap"))


def backup_level(level_path, arena):
    """Copy the .umap aside before any save — cheap insurance against data loss."""
    src = level_disk_path(level_path)
    if not os.path.isfile(src):
        return
    backup_dir = os.path.join(TOOLS_DIR, "Build", "Backups")
    if not os.path.isdir(backup_dir):
        os.makedirs(backup_dir)
    import shutil
    import time
    dst = os.path.join(backup_dir, "{}_{}.umap".format(arena, time.strftime("%Y%m%d_%H%M%S")))
    shutil.copy2(src, dst)
    log("Backup: {}".format(dst))


def report_sublevels(prefix):
    """Log the persistent level's streaming sublevels (author's debug/light levels)."""
    try:
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        subs = unreal.EditorLevelUtils.get_levels(world)
        names = []
        for lvl in subs:
            if lvl is None:
                continue
            pkg = lvl.get_package().get_name()
            names.append(pkg)
        log("{}: {} levels in world {}".format(prefix, len(names), names))
        return names
    except Exception as e:
        warn("Sublevel report failed: {}".format(e))
        return []


def open_or_create_level(les, level_path):
    # Asset registry scans asynchronously on editor boot — NEVER trust does_asset_exist
    # alone, or a slow scan would route an EXISTING map into new_level() and blank it.
    try:
        unreal.AssetRegistryHelpers.get_asset_registry().wait_for_completion()
    except Exception:
        pass
    exists_on_disk = os.path.isfile(level_disk_path(level_path))
    if exists_on_disk or unreal.EditorAssetLibrary.does_asset_exist(level_path):
        if not les.load_level(level_path):
            raise RuntimeError("Failed to load level " + level_path)
        log("Loaded existing level {}".format(level_path))
    else:
        if not les.new_level(level_path):
            raise RuntimeError("Failed to create level " + level_path)
        log("Created new level {}".format(level_path))


def in_package(actor, package_name):
    """True if the actor lives in the given level package.

    The author adds his own sublevels (debug character, lighting) to built arenas —
    we must NEVER touch actors outside the arena's persistent level package."""
    try:
        return actor.get_package().get_name() == package_name
    except Exception:
        return False


def clear_tagged(eas, tag, package_name):
    removed = 0
    skipped_foreign = 0
    for actor in list(eas.get_all_level_actors()):
        try:
            if tag in [str(t) for t in actor.tags]:
                if in_package(actor, package_name):
                    eas.destroy_actor(actor)
                    removed += 1
                else:
                    skipped_foreign += 1
        except Exception:
            pass
    log("Removed {} previously generated actors".format(removed))
    if skipped_foreign:
        warn("Skipped {} tagged actors living in OTHER levels (author sublevels untouched)"
             .format(skipped_foreign))


def finish_actor(actor, tag, label, folder):
    actor.set_actor_label(label)
    actor.set_folder_path(folder)
    tags = list(actor.tags)
    tags.append(unreal.Name(tag))
    actor.set_editor_property("tags", tags)
    return actor


FIELD_BP = "/Game/ArenaBlockout/BP_ContainmentField"
FIELD_MAT = "/Game/ArenaBlockout/Materials/M_ContainmentField"


def spawn_shape(eas, mats, piece, tag, arena):
    shape = piece.get("shape", "box")
    mat_name = piece.get("mat", "floor")
    # Containment fields: spawn the interactive BP (invisible hit-reveal shader,
    # feeds hit pos/time into its DMI; built by make_containment_field.py).
    # The BeginPlay CDMI uses slot-0 material, so the field material is assigned
    # on the spawned INSTANCE here. Falls back to a translucent box if absent.
    if mat_name == "field" and unreal.EditorAssetLibrary.does_asset_exist(FIELD_BP):
        bp_class = unreal.EditorAssetLibrary.load_blueprint_class(FIELD_BP)
        actor = eas.spawn_actor_from_class(bp_class, vec(piece["pos"]), rot(piece))
        if actor is None:
            warn("Failed to spawn field piece {}".format(piece.get("id", "?")))
            return None
        size = piece["size"]
        actor.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
        smc = actor.get_component_by_class(unreal.StaticMeshComponent)
        field_mat = unreal.EditorAssetLibrary.load_asset(FIELD_MAT)
        if smc and field_mat:
            smc.set_material(0, field_mat)
        else:
            warn("Field piece {}: mesh component or {} missing".format(
                piece.get("id", "?"), FIELD_MAT))
        group = piece.get("group", "Geo")
        ret = finish_actor(actor, tag, "BLK_{}_{}".format(arena, piece.get("id", "piece")),
                           "{}/{}".format(arena, group))
        # Gameplay markers: ApexMovement skips wallrun on NoWallRun; the elastic
        # no-slam-damage branches in ShooterNPC/ApexMovement key off ContainmentField.
        cur_tags = list(actor.tags)
        cur_tags.extend(["NoWallRun", "ContainmentField"])
        actor.set_editor_property("tags", cur_tags)
        return ret
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


# NO lighting is ever spawned: the author adds lighting as a separate sublevel.
# Verification screenshots are rendered via a transient SceneCapture2D with the
# Lighting show-flag disabled (= unlit, flat blockout colors). Synchronous, no
# viewport involved — works headless where editor viewports don't redraw.

def take_screenshots(eas, spec, arena):
    shots = spec.get("screenshots") or []
    if not shots:
        return
    out_dir = os.path.join(TOOLS_DIR, "Build", "Screenshots")
    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    rt = unreal.RenderingLibrary.create_render_target2d(
        world, 1600, 900, unreal.TextureRenderTargetFormat.RTF_RGBA8)
    cap = eas.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(0, 0, 0))
    comp = cap.capture_component2d
    comp.set_editor_property("texture_target", rt)
    comp.set_editor_property("capture_every_frame", False)
    # Fixed exposure: auto-exposure + camera flash blows tight white interiors to
    # pure white (observed on A4 hangar nave / A7 atrium shots).
    try:
        pps = comp.get_editor_property("post_process_settings")
        pps.set_editor_property("override_auto_exposure_min_brightness", True)
        pps.set_editor_property("override_auto_exposure_max_brightness", True)
        pps.set_editor_property("auto_exposure_min_brightness", 1.0)
        pps.set_editor_property("auto_exposure_max_brightness", 1.0)
        comp.set_editor_property("post_process_settings", pps)
    except Exception as e:
        warn("fixed exposure setup: {}".format(e))
    # Transient lights for the lit pass — destroyed right after, never saved (the level
    # stays light-free per author rule; we simply don't save after screenshots).
    sun = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 3000),
                                     unreal.Rotator(roll=0.0, pitch=-55.0, yaw=35.0))
    fill = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 3000),
                                      unreal.Rotator(roll=0.0, pitch=-30.0, yaw=215.0))
    try:
        fill.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property(
            "intensity", 3.0)
    except Exception as e:
        warn("fill light intensity: {}".format(e))
    # Camera-mounted "flash" so interiors (closed shells like the hangar) are visible —
    # the directional lights can't reach inside.
    flash = eas.spawn_actor_from_class(unreal.PointLight, unreal.Vector(0, 0, 0))
    try:
        flash_comp = flash.get_component_by_class(unreal.PointLightComponent)
        flash_comp.set_editor_property("intensity", 30000.0)
        flash_comp.set_editor_property("attenuation_radius", 12000.0)
        flash_comp.set_editor_property("cast_shadows", False)
    except Exception as e:
        warn("flash light setup: {}".format(e))
    for shot in shots:
        flash.set_actor_location(vec(shot["pos"]), False, False)
        cap.set_actor_location_and_rotation(vec(shot["pos"]), rot(shot), False, False)
        comp.set_editor_property("fov_angle", float(shot.get("fov", 90.0)))
        sid = shot.get("id", "shot")
        # Lit FinalColor with transient lights (captured twice so eye adaptation settles).
        comp.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
        comp.capture_scene()
        comp.capture_scene()
        name = "{}_{}.png".format(arena, sid)
        unreal.RenderingLibrary.export_render_target(world, rt, out_dir, name)
        log("Screenshot saved: {}".format(os.path.join(out_dir, name)))
    eas.destroy_actor(cap)
    eas.destroy_actor(sun)
    eas.destroy_actor(fill)
    eas.destroy_actor(flash)
    log("Screenshots done ({})".format(len(shots)))


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


def dump_actors(eas, tag, arena, package_name):
    rows = []
    for actor in eas.get_all_level_actors():
        if tag not in [str(t) for t in actor.tags]:
            continue
        if not in_package(actor, package_name):
            continue
        origin, extent = actor.get_actor_bounds(False)
        loc = actor.get_actor_location()
        row = {
            "label": actor.get_actor_label(),
            "class": actor.get_class().get_name(),
            "loc": [round(loc.x, 1), round(loc.y, 1), round(loc.z, 1)],
            "bounds_origin": [round(origin.x, 1), round(origin.y, 1), round(origin.z, 1)],
            "bounds_extent": [round(extent.x, 1), round(extent.y, 1), round(extent.z, 1)],
        }
        try:
            smc = getattr(actor, "static_mesh_component", None)
            if smc:
                mat = smc.get_material(0)
                row["mat"] = mat.get_path_name() if mat else "NULL"
        except Exception:
            pass
        rows.append(row)
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
    mats = ensure_materials(force=False)
    open_or_create_level(les, level_path)
    report_sublevels("After load")
    clear_tagged(eas, tag, level_path)

    classes = {key: resolve_class(path) for key, path in spec.get("classes", {}).items()}

    # --- geometry ---
    blockers = {}
    count_geo = 0
    for piece in spec.get("pieces", []):
        actor = spawn_shape(eas, mats, piece, tag, arena)
        # Any piece referenced by exit_blocker_ids must be wirable into the
        # ArenaManager (red gate blockers AND containment fields alike).
        if actor and piece.get("mat") in ("blocker", "field"):
            blockers[piece.get("id")] = actor
        if actor:
            count_geo += 1
    log("Spawned {} geometry pieces".format(count_geo))

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
        if mtype == "npc":
            cls = classes.get(marker.get("class_key", ""))
            if cls is None:
                warn("npc marker: unknown class_key '{}'".format(marker.get("class_key")))
                continue
            actor = eas.spawn_actor_from_class(cls, vec(marker["pos"]), rot(marker))
            if actor:
                label = marker.get("id", "NPC{}".format(idx))
                finish_actor(actor, tag, "BLK_{}_{}".format(arena, label), "{}/NPCs".format(arena))
            continue
        if mtype == "nav_link":
            actor = eas.spawn_actor_from_class(unreal.NavLinkProxy, vec(marker["pos"]), rot(marker))
            if actor is None:
                warn("NavLinkProxy failed to spawn")
                continue
            link = unreal.NavigationLink()
            link.set_editor_property("left", vec(marker["left"]))
            link.set_editor_property("right", vec(marker["right"]))
            try:
                link.set_editor_property("direction", unreal.NavLinkDirection.BOTH_WAYS)
                link.set_editor_property("snap_radius", float(marker.get("snap_radius", 150.0)))
            except Exception as e:
                warn("NavigationLink optional props: {}".format(e))
            actor.set_editor_property("point_links", [link])
            # PolarityPathFollowingComponent's jump traversal was built against SMART
            # links (CustomNavLinkId) — configure the smart link to the same endpoints.
            try:
                actor.set_editor_property("smart_link_is_relevant", True)
                smart = actor.get_editor_property("smart_link_comp")
                smart.set_editor_property("link_relative_start", vec(marker["left"]))
                smart.set_editor_property("link_relative_end", vec(marker["right"]))
                smart.set_editor_property("link_direction", unreal.NavLinkDirection.BOTH_WAYS)
                smart.set_editor_property("link_enabled", True)
            except Exception as e:
                warn("Smart link setup failed ({}) — falling back to point link only".format(e))
            label = marker.get("id", "NavLink{}".format(idx))
            finish_actor(actor, tag, "BLK_{}_{}".format(arena, label), "{}/Nav".format(arena))
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
                sustain = spec.get("sustain")
                if sustain:
                    # Continuous pressure mode: kill one -> another spawns.
                    manager.set_editor_property("arena_mode", unreal.ArenaMode.SUSTAIN)
                    pool = []
                    for key, weight in sustain.get("pool", []):
                        npc_cls = classes.get(key)
                        if npc_cls is None:
                            warn("sustain pool: unknown class key '{}'".format(key))
                            continue
                        entry = unreal.SustainSpawnEntry()
                        entry.set_editor_property("npc_class", npc_cls)
                        entry.set_editor_property("weight", float(weight))
                        pool.append(entry)
                    manager.set_editor_property("sustain_enemy_pool", pool)
                    manager.set_editor_property("max_sustain_enemies",
                                                int(sustain.get("max_alive", 5)))
                    manager.set_editor_property("sustain_total_enemies",
                                                int(sustain.get("total", -1)))
                    log("ArenaManager mode=Sustain: pool={} max_alive={} total={}".format(
                        len(pool), sustain.get("max_alive", 5), sustain.get("total", -1)))
                else:
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

    # --- author sublevels: guarantee they stay attached across rebuilds ---
    for sub_path in spec.get("extra_sublevels", []):
        try:
            world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
            current = report_sublevels("Pre-attach check")
            if any(sub_path in name for name in current):
                continue
            unreal.EditorLevelUtils.add_level_to_world(world, sub_path,
                                                       unreal.LevelStreamingAlwaysLoaded)
            log("Attached author sublevel {}".format(sub_path))
        except Exception as e:
            warn("Could not attach sublevel {}: {}".format(sub_path, e))

    # --- navmesh rebuild (saved navmesh otherwise stays stale and breaks NPC nav) ---
    try:
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        unreal.SystemLibrary.execute_console_command(world, "RebuildNavigation")
        log("RebuildNavigation issued (if NPC nav still acts up: Build Paths in editor)")
    except Exception as e:
        warn("RebuildNavigation failed: {}".format(e))

    # --- save + dump (with .umap backup first) ---
    backup_level(level_path, arena)
    report_sublevels("Before save")
    if not les.save_current_level():
        warn("save_current_level returned false")
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    dump_actors(eas, tag, arena, level_path)

    # --- unlit verification screenshots (after save: capture actor is never persisted) ---
    take_screenshots(eas, spec, arena)
    log("DONE: {} built into {}".format(arena, level_path))


def shots_only(arena_name):
    """Re-take screenshots of an already-built arena WITHOUT touching the level file.

    Safe to run while the author has the level open in another editor instance:
    nothing is cleared, spawned-persistent, or saved."""
    spec_path = os.path.join(TOOLS_DIR, "Arenas", arena_name + ".json")
    if not os.path.isfile(spec_path):
        raise RuntimeError("Spec not found: " + spec_path)
    with open(spec_path, "r", encoding="utf-8") as f:
        spec = json.load(f)
    les, eas = get_subsystems()
    ensure_materials()
    if not unreal.EditorAssetLibrary.does_asset_exist(spec["level_path"]):
        raise RuntimeError("Level does not exist yet, run a full build first: " + spec["level_path"])
    if not les.load_level(spec["level_path"]):
        raise RuntimeError("Failed to load level " + spec["level_path"])
    take_screenshots(eas, spec, spec["name"])


def main():
    args = [a for a in sys.argv[1:] if a and not a.startswith("-")]
    quit_when_done = "--quit" in sys.argv[1:]
    ok = False
    try:
        if not args:
            raise RuntimeError("Usage: build_arena.py <ArenaSpecName> [--shots-only] [--quit]")
        if "--shots-only" in sys.argv[1:]:
            shots_only(args[0])
        else:
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
