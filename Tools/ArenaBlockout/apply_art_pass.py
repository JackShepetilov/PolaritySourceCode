# Arena ART-PASS for Polarity — "blockout -> art" layer on a DUPLICATE level.
#
# Blockout level (Lvl_<Arena>) is the source of truth and is NEVER modified. This regenerates
# Lvl_<Arena>_Art: duplicate the blockout, hide the gray geometry (collision kept), dress it as an
# LSJ villa. Materials use the lead's chosen CC0 textures (diffuse + normal + ARM) on the
# world-projected villa master M_ArtPass_Villa (WorldCoordinate3Way -> tiling const at any size).
#
# Per the lead's direction:
#  - terrace TOP = floor stone; each terrace gets a thin vertical RISER panel on its courtyard-
#    facing edge with the WALL material (the pl_s_e2 example).
#  - small decorations are NOT static meshes: they are EMF props (BP_EMFProp_1) with their mesh
#    set to the decoration (so they read as villa pottery AND are throwable gameplay props).
#  - large items (columns, fountain, altar) stay as meshes.
#
# MODES (REST): (default) regen dupe + dress | --reskin (0 loads, on open dupe) | --shots-only | --revert
# Log: [ARTPASS].

import json
import math
import os
import sys

import unreal

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
ART_MAT_DIR = "/Game/ArenaArtPass/Materials"

CUBE_MESH = "/Engine/BasicShapes/Cube.Cube"
CYL_MESH = "/Engine/BasicShapes/Cylinder.Cylinder"

VILLA_MASTER = "/Game/ArenaArtPass/Materials/M_ArtPass_Villa.M_ArtPass_Villa"
KIT_MASTER = "/Game/Greek_island/Materials/Master/M_Master_kit_detail.M_Master_kit_detail"
TEX_GOLD = "/Game/temple/Textures/Gold.Gold"

# The lead's chosen CC0 textures (imported flat into /Game/ArenaArtPass/Textures/). Each surface
# uses diffuse + normal (_nor_gl) + ARM (_arm).
TX = "/Game/ArenaArtPass/Textures"


def _set(diff):
    return {"base": "{}/{}.{}".format(TX, diff, diff),
            "nor": "{}/{}_nor_gl.{}_nor_gl".format(TX, diff, diff),
            "arm": "{}/{}_arm.{}_arm".format(TX, diff, diff)}


ART_MATERIALS = {
    "wall":   dict(master="villa", tint=(1.04, 1.01, 0.95), world_scale=400.0, **_set("beige_wall_001")),
    "floor":  dict(master="villa", tint=(1.0, 0.98, 0.95),  world_scale=300.0, **_set("coral_stone_wall")),
    "roof":   dict(master="villa", tint=(1.0, 0.92, 0.85),  world_scale=200.0, **_set("grey_roof_tiles_02")),
    "podium": dict(master="villa", tint=(1.08, 1.05, 0.98), world_scale=420.0, **_set("beige_wall_001")),
    "gold":   dict(master="kit", color=TEX_GOLD, tint=(1.0, 0.95, 0.78), tiling=1.0,
                   scalars={"Metallic": 1.0, "Roughness": 0.3}),
}

# decorative props now go through EMF props (gameplay) with a custom mesh
EMF_PROP = "/Game/Variant_Shooter/Blueprints/Objects/BP_EMFProp_1"
SM_JAR1 = "/Game/Greek_island/Assets/Vases/SM_Big_Jar_01.SM_Big_Jar_01"
SM_JAR2 = "/Game/Greek_island/Assets/Vases/SM_Big_Jar_02.SM_Big_Jar_02"
SM_COLUMN = "/Game/Greek_island/Assets/Temple/SM_small_column.SM_small_column"
SM_ALTAR = "/Game/Greek_island/Assets/Altar/SM_altar.SM_altar"

GROUP_ART = {
    "Floor":          {"kind": "box",     "mat": "floor"},
    "Ramps":          {"kind": "box",     "mat": "floor"},
    "BalconyL1":      {"kind": "terrace", "mat": "floor"},   # top=floor stone + wall-material riser
    "Buildings":      {"kind": "wall",    "mat": "wall"},
    "Parapets":       {"kind": "box",     "mat": "wall"},
    "WallrunFacades": {"kind": "box",     "mat": "wall"},
    "Cover":          {"kind": "box",     "mat": "wall"},
    "Center":         {"kind": "podium",  "mat": "podium"},
    "Field":          {"kind": "skip"},
}

ROOF_CAP_H = 140.0
ROOF_OVERHANG = 160.0
COLONNADE_SPACING = 1300.0
COLONNADE_INSET = 120.0
RISER_THICK = 14.0
INFLATE = 1.0


def log(m):
    unreal.log("[ARTPASS] {}".format(m))


def warn(m):
    unreal.log_warning("[ARTPASS] {}".format(m))


def vec(xyz):
    return unreal.Vector(float(xyz[0]), float(xyz[1]), float(xyz[2]))


def rot(spec):
    return unreal.Rotator(roll=float(spec.get("roll", 0.0)),
                          pitch=float(spec.get("pitch", 0.0)),
                          yaw=float(spec.get("yaw", 0.0)))


def level_disk_path(level_path):
    content = unreal.SystemLibrary.get_project_content_directory()
    return os.path.normpath(os.path.join(content, level_path.replace("/Game/", "", 1) + ".umap"))


def resolve_ramp(piece):
    fx, fy, fz = [float(v) for v in piece["from"]]
    tx, ty, tz = [float(v) for v in piece["to"]]
    width = float(piece.get("width", 500))
    thick = float(piece.get("thick", 60))
    dx, dy, dz = tx - fx, ty - fy, tz - fz
    slope_len = math.hypot(math.hypot(dx, dy), dz) or 1.0
    pitch = math.degrees(math.asin(dz / slope_len))
    yaw = math.degrees(math.atan2(dy, dx))
    over = 60.0
    ux, uy, uz = dx / slope_len, dy / slope_len, dz / slope_len
    cx = (fx + tx) * 0.5 - ux * (over * 0.5)
    cy = (fy + ty) * 0.5 - uy * (over * 0.5)
    cz = (fz + tz) * 0.5 - uz * (over * 0.5) - (thick * 0.5) * math.cos(math.radians(pitch))
    return dict(piece, pos=[cx, cy, cz], size=[slope_len + over, width, thick],
                pitch=pitch, yaw=yaw, shape="box")


def subsystems():
    return (unreal.get_editor_subsystem(unreal.LevelEditorSubsystem),
            unreal.get_editor_subsystem(unreal.EditorActorSubsystem))


def current_level_package():
    try:
        w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        return w.get_package().get_name() if w else ""
    except Exception:
        return ""


def in_package(actor, package_name):
    try:
        return actor.get_package().get_name() == package_name
    except Exception:
        return False


def ensure_safe():
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None:
        raise RuntimeError("PIE is running - art-pass refused. Stop PIE.")


def foreign_dirty_guard(allow):
    dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    foreign = [p for p in dirty if p.get_name() not in allow]
    if foreign:
        raise RuntimeError("UNSAVED foreign map changes ({}) - save them first".format(
            ", ".join(p.get_name() for p in foreign)))


def load_level(les, path):
    if not les.load_level(path):
        raise RuntimeError("Failed to load level " + path)
    log("Loaded {}".format(path))


# ---- materials -----------------------------------------------------------------

def ensure_art_materials(force=False):
    eal = unreal.EditorAssetLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mel = unreal.MaterialEditingLibrary
    if not eal.does_directory_exist(ART_MAT_DIR):
        eal.make_directory(ART_MAT_DIR)
    villa = eal.load_asset(VILLA_MASTER)
    kit = eal.load_asset(KIT_MASTER)
    if villa is None:
        raise RuntimeError("Villa master missing - run build_villa_master.py first: " + VILLA_MASTER)
    out = {}
    for name, spec in ART_MATERIALS.items():
        path = "{}/MI_ArtPass_{}".format(ART_MAT_DIR, name)
        master = villa if spec["master"] == "villa" else kit
        if force and eal.does_asset_exist(path):
            eal.delete_asset(path)
        mic = eal.load_asset(path) if eal.does_asset_exist(path) else tools.create_asset(
            "MI_ArtPass_{}".format(name), ART_MAT_DIR, unreal.MaterialInstanceConstant,
            unreal.MaterialInstanceConstantFactoryNew())
        if mic is None:
            raise RuntimeError("Failed to create MIC " + path)
        mel.set_material_instance_parent(mic, master)
        r, g, b = spec.get("tint", (1.0, 1.0, 1.0))
        if spec["master"] == "villa":
            for pkey, tpath in (("Base_Texture", spec["base"]), ("NormalMap", spec["nor"]), ("ARM", spec["arm"])):
                tex = eal.load_asset(tpath)
                if tex is not None:
                    mel.set_material_instance_texture_parameter_value(mic, pkey, tex)
                else:
                    warn("texture missing {} for {}".format(tpath, name))
            mel.set_material_instance_vector_parameter_value(mic, "Tint", unreal.LinearColor(r, g, b, 1.0))
            mel.set_material_instance_scalar_parameter_value(mic, "WorldScale", float(spec.get("world_scale", 300.0)))
        else:
            tex = eal.load_asset(spec["color"])
            if tex is not None:
                mel.set_material_instance_texture_parameter_value(mic, "Color", tex)
            mel.set_material_instance_vector_parameter_value(mic, "Base Color", unreal.LinearColor(r, g, b, 1.0))
            mel.set_material_instance_scalar_parameter_value(mic, "Tiling", float(spec.get("tiling", 1.0)))
            for sk, sv in spec.get("scalars", {}).items():
                try:
                    mel.set_material_instance_scalar_parameter_value(mic, sk, float(sv))
                except Exception as e:
                    warn("scalar {}.{}: {}".format(name, sk, e))
        mel.update_material_instance(mic)
        eal.save_asset(path)
        out[name] = mic
    log("Materials ready: {}".format(sorted(out.keys())))
    return out


# ---- spawn helpers -------------------------------------------------------------

def _finish(actor, tag, label, folder):
    actor.set_actor_label(label)
    actor.set_folder_path(folder)
    tags = list(actor.tags)
    tags.append(unreal.Name(tag))
    actor.set_editor_property("tags", tags)
    return actor


def _no_collision(actor):
    try:
        actor.static_mesh_component.set_collision_profile_name("NoCollision")
    except Exception:
        pass


def spawn_box(eas, pos, size, material, tag, label, folder, yaw=0.0, pitch=0.0,
              inflate=INFLATE, cyl=False):
    mesh = unreal.EditorAssetLibrary.load_asset(CYL_MESH if cyl else CUBE_MESH)
    actor = eas.spawn_actor_from_object(mesh, vec(pos), unreal.Rotator(0.0, pitch, yaw))
    if actor is None:
        warn("box spawn failed: {}".format(label))
        return None
    actor.set_actor_scale3d(unreal.Vector((size[0] + inflate) / 100.0,
                                          (size[1] + inflate) / 100.0,
                                          (size[2] + inflate) / 100.0))
    if material is not None:
        actor.static_mesh_component.set_material(0, material)
    _no_collision(actor)
    return _finish(actor, tag, label, folder)


def spawn_mesh(eas, mesh_path, pos, scale3d, tag, label, folder, yaw=0.0, material=None):
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    if mesh is None:
        warn("mesh not found: {}".format(mesh_path))
        return None
    actor = eas.spawn_actor_from_object(mesh, vec(pos), unreal.Rotator(0.0, 0.0, yaw))
    if actor is None:
        warn("mesh spawn failed: {}".format(label))
        return None
    actor.set_actor_scale3d(unreal.Vector(scale3d[0], scale3d[1], scale3d[2]))
    if material is not None:
        try:
            actor.static_mesh_component.set_material(0, material)
        except Exception:
            pass
    _no_collision(actor)
    return _finish(actor, tag, label, folder)


def spawn_emf_decor(eas, bp_cls, mesh_path, pos, scale, tag, label, folder, yaw=0.0):
    """Small decoration = an EMF prop (gameplay/throwable) with its mesh set to the decoration."""
    if bp_cls is None:
        return None
    actor = eas.spawn_actor_from_class(bp_cls, vec(pos), unreal.Rotator(0.0, 0.0, yaw))
    if actor is None:
        warn("EMF prop spawn failed: {}".format(label))
        return None
    actor.set_actor_scale3d(unreal.Vector(scale, scale, scale))
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    try:
        smc = actor.get_component_by_class(unreal.StaticMeshComponent)
        if smc and mesh:
            smc.set_static_mesh(mesh)
    except Exception as e:
        warn("EMF mesh set failed {}: {}".format(label, e))
    return _finish(actor, tag, label, folder)


def art_wall(eas, piece, mats, tag, arena, folder):
    pos = [float(v) for v in piece["pos"]]
    size = [float(v) for v in piece["size"]]
    pid = piece.get("id", "wall")
    spawn_box(eas, pos, size, mats["wall"], tag, "ART_{}_{}".format(arena, pid), folder)
    cap_pos = [pos[0], pos[1], pos[2] + size[2] * 0.5 + ROOF_CAP_H * 0.5]
    cap_size = [size[0] + ROOF_OVERHANG, size[1] + ROOF_OVERHANG, ROOF_CAP_H]
    spawn_box(eas, cap_pos, cap_size, mats["roof"], tag, "ART_{}_{}_roof".format(arena, pid), folder)
    long_x = size[0] >= size[1]
    length = size[0] if long_x else size[1]
    n = max(0, int(length // COLONNADE_SPACING))
    if n < 1:
        return
    half_t = (size[1] if long_x else size[0]) * 0.5
    thin_center = pos[1] if long_x else pos[0]
    inward = -1.0 if thin_center > 0 else 1.0
    col_scale_z = size[2] / 493.0
    step = length / (n + 1)
    base_long = (pos[0] if long_x else pos[1]) - length * 0.5
    for i in range(1, n + 1):
        along = base_long + step * i
        off = half_t + COLONNADE_INSET
        if long_x:
            cpos = [along, pos[1] + inward * off, pos[2] - size[2] * 0.5]
        else:
            cpos = [pos[0] + inward * off, along, pos[2] - size[2] * 0.5]
        spawn_mesh(eas, SM_COLUMN, cpos, (1.0, 1.0, max(0.5, col_scale_z)), tag,
                   "ART_{}_{}_col{}".format(arena, pid, i), "{}/Columns".format(folder),
                   material=mats["wall"])


def art_terrace(eas, piece, mats, tag, arena, folder):
    """Terrace platform: stone TOP (floor material) + a thin vertical RISER panel on the
    courtyard-facing edge with the WALL material (replicates the lead's pl_s_e2 example)."""
    pos = [float(v) for v in piece["pos"]]
    size = [float(v) for v in piece["size"]]
    pid = piece.get("id", "terr")
    spawn_box(eas, pos, size, mats["floor"], tag, "ART_{}_{}".format(arena, pid), folder)
    long_x = size[0] >= size[1]
    if long_x:
        sgn = 1.0 if pos[1] >= 0 else -1.0
        rpos = [pos[0], pos[1] - sgn * size[1] * 0.5, pos[2]]
        rsize = [size[0], RISER_THICK, size[2]]
    else:
        sgn = 1.0 if pos[0] >= 0 else -1.0
        rpos = [pos[0] - sgn * size[0] * 0.5, pos[1], pos[2]]
        rsize = [RISER_THICK, size[1], size[2]]
    spawn_box(eas, rpos, rsize, mats["wall"], tag, "ART_{}_{}_riser".format(arena, pid),
              "{}/Risers".format(folder), inflate=0.0)


def art_podium(eas, piece, mats, tag, arena, folder):
    pos = [float(v) for v in piece["pos"]]
    size = [float(v) for v in piece["size"]]
    spawn_box(eas, pos, size, mats["podium"], tag, "ART_{}_podium".format(arena), folder, cyl=True)
    top_z = pos[2] + size[2] * 0.5
    foot = min(size[0], size[1])
    altar_scale = max(0.6, (foot * 0.85) / 865.0)
    spawn_mesh(eas, SM_ALTAR, [pos[0], pos[1], top_z],
               (altar_scale, altar_scale, altar_scale), tag,
               "ART_{}_altar".format(arena), folder, material=mats["gold"])


def art_decor(eas, spec, tag, arena, folder):
    """Perimeter decoration as EMF props (throwable) with amphora meshes — NO static decor."""
    bp_cls = unreal.EditorAssetLibrary.load_blueprint_class(EMF_PROP)
    if bp_cls is None:
        warn("EMF prop class not found: {} - skipping decor".format(EMF_PROP))
        return
    half = 3000.0
    for p in spec.get("pieces", []):
        if p.get("id") == "ground":
            half = float(p["size"][0]) * 0.5
            break
    rim = half - 900.0
    placements = [(SM_JAR1, -430.0, -(half - 400.0), 1.3),
                  (SM_JAR1, 430.0, -(half - 400.0), 1.3)]
    for sx in (-1.0, 1.0):
        for sy in (-1.0, 1.0):
            placements.append((SM_JAR1, sx * rim, sy * (rim - 500.0), 1.25))
            placements.append((SM_JAR2, sx * (rim - 500.0), sy * rim, 1.1))
    for i in range(6):
        ang = math.radians(60.0 * i + 20.0)
        placements.append((SM_JAR2, 1000.0 * math.cos(ang), 1000.0 * math.sin(ang), 1.1))
    n = 0
    for mesh, x, y, s in placements:
        if spawn_emf_decor(eas, bp_cls, mesh, [x, y, 30.0], s, tag,
                           "ART_{}_decor{}".format(arena, n), "{}/Decor".format(folder),
                           yaw=float((n * 53) % 360)):
            n += 1
    log("Decor EMF props placed: {}".format(n))


# ---- blockout hide / art clear -------------------------------------------------

def hide_blockout_geometry(eas, blk_tag, package_name):
    hidden = 0
    for a in list(eas.get_all_level_actors()):
        try:
            if blk_tag not in [str(t) for t in a.tags] or not in_package(a, package_name):
                continue
            cls = a.get_class().get_name()
            if cls == "StaticMeshActor":
                a.static_mesh_component.set_visibility(False, True)
                hidden += 1
            elif "EMFProp" in cls:  # the gray/yellow blockout combat-prop boxes
                for smc in a.get_components_by_class(unreal.StaticMeshComponent):
                    smc.set_visibility(False, True)
                hidden += 1
        except Exception:
            pass
    log("Hid {} blockout meshes incl. EMF-prop boxes (collision kept)".format(hidden))


def clear_art(eas, tag, package_name):
    removed = 0
    for a in list(eas.get_all_level_actors()):
        try:
            if tag in [str(t) for t in a.tags] and in_package(a, package_name):
                eas.destroy_actor(a)
                removed += 1
        except Exception:
            pass
    if removed:
        log("Cleared {} previous art actors".format(removed))


def spawn_art_layer(eas, spec, mats, art_tag, arena, package_name):
    counts = {}
    for piece in spec.get("pieces", []):
        group = piece.get("group", "Geo")
        recipe = GROUP_ART.get(group) or {"kind": "box", "mat": "wall"}
        if recipe["kind"] == "skip":
            continue
        p = resolve_ramp(piece) if piece.get("shape") == "ramp" else piece
        folder = "{}_Art/{}".format(arena, group)
        try:
            if recipe["kind"] == "box":
                spawn_box(eas, p["pos"], p["size"], mats[recipe["mat"]], art_tag,
                          "ART_{}_{}".format(arena, p.get("id", "p")), folder,
                          yaw=float(p.get("yaw", 0.0)), pitch=float(p.get("pitch", 0.0)),
                          cyl=(p.get("shape") == "cylinder"))
            elif recipe["kind"] == "wall":
                art_wall(eas, p, mats, art_tag, arena, folder)
            elif recipe["kind"] == "terrace":
                art_terrace(eas, p, mats, art_tag, arena, folder)
            elif recipe["kind"] == "podium":
                art_podium(eas, p, mats, art_tag, arena, folder)
            counts[group] = counts.get(group, 0) + 1
        except Exception as e:
            warn("art piece {} ({}) failed: {}".format(p.get("id", "?"), group, e))
    log("Art spawned by group: {}".format(counts))
    art_decor(eas, spec, art_tag, arena, "{}_Art".format(arena))


# ---- screenshots ---------------------------------------------------------------

def take_screenshots(eas, spec, arena):
    shots = spec.get("screenshots") or []
    if not shots:
        return
    out_dir = os.path.join(TOOLS_DIR, "Build", "Screenshots")
    if not os.path.isdir(out_dir):
        os.makedirs(out_dir)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    try:
        unreal.SystemLibrary.execute_console_command(world, "r.Streaming.FullyLoadUsedTextures 1")
    except Exception:
        pass
    rt = unreal.RenderingLibrary.create_render_target2d(world, 1600, 900,
                                                        unreal.TextureRenderTargetFormat.RTF_RGBA8)
    cap = eas.spawn_actor_from_class(unreal.SceneCapture2D, unreal.Vector(0, 0, 0))
    comp = cap.capture_component2d
    comp.set_editor_property("texture_target", rt)
    comp.set_editor_property("capture_every_frame", False)
    try:
        pps = comp.get_editor_property("post_process_settings")
        pps.set_editor_property("override_auto_exposure_min_brightness", True)
        pps.set_editor_property("override_auto_exposure_max_brightness", True)
        pps.set_editor_property("auto_exposure_min_brightness", 1.0)
        pps.set_editor_property("auto_exposure_max_brightness", 1.0)
        comp.set_editor_property("post_process_settings", pps)
    except Exception as e:
        warn("fixed exposure: {}".format(e))
    sun = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 3000),
                                     unreal.Rotator(roll=0.0, pitch=-55.0, yaw=35.0))
    fill = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 3000),
                                      unreal.Rotator(roll=0.0, pitch=-30.0, yaw=215.0))
    try:
        fill.get_component_by_class(unreal.DirectionalLightComponent).set_editor_property("intensity", 3.0)
    except Exception:
        pass
    comp.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    # TICK-DEFERRED: let the editor tick ~3.5s so all textures fully STREAM in before capture
    # (synchronous capture_scene rendered checkerboard on the big floor's outer mips).
    state = {"t": 0.0, "done": False, "h": None}

    def do_caps():
        for shot in shots:
            cap.set_actor_location_and_rotation(vec(shot["pos"]), rot(shot), False, False)
            comp.set_editor_property("fov_angle", float(shot.get("fov", 90.0)))
            for _ in range(3):
                comp.capture_scene()
            name = "{}_art_{}.png".format(arena, shot.get("id", "shot"))
            unreal.RenderingLibrary.export_render_target(world, rt, out_dir, name)
            log("Screenshot {}".format(os.path.join(out_dir, name)))
        for a in (cap, sun, fill):
            if a:
                eas.destroy_actor(a)
        log("Screenshots done ({})".format(len(shots)))

    def cb(dt):
        if state["done"]:
            return
        state["t"] += dt
        if state["t"] >= 3.5:
            state["done"] = True
            try:
                do_caps()
            finally:
                if state["h"] is not None:
                    try:
                        unreal.unregister_slate_post_tick_callback(state["h"])
                    except Exception:
                        pass

    state["h"] = unreal.register_slate_post_tick_callback(cb)
    log("Screenshots scheduled (texture streaming ~3.5s)")


# ---- main flows ----------------------------------------------------------------

def _load_spec(arena_name):
    spec_path = os.path.join(TOOLS_DIR, "Arenas", arena_name + ".json")
    if not os.path.isfile(spec_path):
        raise RuntimeError("Spec not found: " + spec_path)
    with open(spec_path, "r", encoding="utf-8") as f:
        return json.load(f)


def _dress(eas, spec, art_tag, blk_tag, arena, dst, force_mats):
    clear_art(eas, art_tag, dst)
    mats = ensure_art_materials(force=force_mats)
    hide_blockout_geometry(eas, blk_tag, dst)
    spawn_art_layer(eas, spec, mats, art_tag, arena, dst)
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not les.save_current_level():
        warn("save_current_level returned false")
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    take_screenshots(eas, spec, arena)
    log("DONE: art-pass dupe at {}".format(dst))


def apply(arena_name):
    spec = _load_spec(arena_name)
    arena, src = spec["name"], spec["level_path"]
    art_tag, blk_tag, dst = "ARTPASS_" + arena, "BLOCKOUT_" + arena, src + "_Art"
    if not dst.endswith("_Art"):
        raise RuntimeError("dst safety check failed: " + dst)
    les, eas = subsystems()
    eal = unreal.EditorAssetLibrary
    foreign_dirty_guard({src, dst})
    try:
        unreal.AssetRegistryHelpers.get_asset_registry().wait_for_completion()
    except Exception:
        pass
    if not eal.does_asset_exist(src):
        raise RuntimeError("Blockout level missing - run build_arena.py first: " + src)
    if eal.does_asset_exist(dst):
        if current_level_package() == dst:
            load_level(les, src)
        eal.delete_asset(dst)
        log("Deleted old dupe {}".format(dst))
    if eal.duplicate_asset(src, dst) is None:
        raise RuntimeError("duplicate_asset failed: {} -> {}".format(src, dst))
    eal.save_asset(dst, False)
    if not os.path.isfile(level_disk_path(dst)):
        raise RuntimeError("dupe not on disk after save: " + dst)
    log("Duplicated + saved blockout -> {}".format(dst))
    load_level(les, dst)
    _dress(eas, spec, art_tag, blk_tag, arena, dst, force_mats=False)


def reskin(arena_name):
    spec = _load_spec(arena_name)
    arena, src = spec["name"], spec["level_path"]
    art_tag, blk_tag, dst = "ARTPASS_" + arena, "BLOCKOUT_" + arena, src + "_Art"
    if current_level_package() != dst:
        raise RuntimeError("reskin needs the _Art dupe open ({}); run default apply first".format(dst))
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    _dress(eas, spec, art_tag, blk_tag, arena, dst, force_mats=True)


def revert(arena_name):
    spec = _load_spec(arena_name)
    arena, src = spec["name"], spec["level_path"]
    art_tag, dst = "ARTPASS_" + arena, src + "_Art"
    les, eas = subsystems()
    eal = unreal.EditorAssetLibrary
    foreign_dirty_guard({src, dst})
    if current_level_package() != src:
        if not eal.does_asset_exist(src):
            raise RuntimeError("Blockout level missing: " + src)
        load_level(les, src)
    removed = 0
    for a in list(eas.get_all_level_actors()):
        try:
            if art_tag in [str(t) for t in a.tags] and in_package(a, src):
                eas.destroy_actor(a)
                removed += 1
        except Exception:
            pass
    log("Reverted: removed {} ARTPASS_ actors from {}".format(removed, src))
    if not les.save_current_level():
        warn("save_current_level returned false")
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    if eal.does_asset_exist(dst) and dst.endswith("_Art"):
        eal.delete_asset(dst)
        log("Deleted dupe {}".format(dst))
    log("REVERT COMPLETE for {}".format(arena))


def shots_only(arena_name):
    spec = _load_spec(arena_name)
    dst = spec["level_path"] + "_Art"
    les, eas = subsystems()
    if not unreal.EditorAssetLibrary.does_asset_exist(dst):
        raise RuntimeError("Art dupe not built yet: " + dst)
    if current_level_package() != dst:
        load_level(les, dst)
    take_screenshots(eas, spec, spec["name"])


def main():
    ensure_safe()
    args = [a for a in sys.argv[1:] if a and not a.startswith("-")]
    flags = [a for a in sys.argv[1:] if a.startswith("-")]
    ok = False
    try:
        if not args:
            raise RuntimeError("Usage: apply_art_pass.py <Arena> [--reskin|--shots-only|--revert] [--quit]")
        if "--revert" in flags:
            revert(args[0])
        elif "--reskin" in flags:
            reskin(args[0])
        elif "--shots-only" in flags:
            shots_only(args[0])
        else:
            apply(args[0])
        ok = True
    except Exception:
        import traceback
        for line in traceback.format_exc().splitlines():
            warn(line)
    finally:
        log("RESULT: {}".format("SUCCESS" if ok else "FAILED"))
        if "--quit" in flags:
            unreal.SystemLibrary.quit_editor()


main()
