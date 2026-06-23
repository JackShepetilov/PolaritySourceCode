# Render a SWATCH BOARD for a CATEGORY of imported textures, so per-surface textures are chosen
# BY SIGHT. Spawns labelled cubes (BaseTop=BaseSide=candidate) on the villa master, TICK-DEFERRED
# capture (lets textures stream ~3s), screenshots, deletes cubes. 0 level loads.
#
#   py "<...>/swatch_textures.py" <category>      e.g. wall | floor | stone | roof
#   reads /Game/ArenaArtPass/Textures/<category>/*  ->  Build/Screenshots/swatch_<category>.png
# Log [SWATCH]: grid col/row -> texture name.

import os
import sys
import unreal

TAG = "[SWATCH]"
TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
VILLA = "/Game/ArenaArtPass/Materials/M_ArtPass_Villa.M_ArtPass_Villa"
CUBE = "/Engine/BasicShapes/Cube.Cube"
TEX_ROOT = "/Game/ArenaArtPass/Textures"


def log(m):
    unreal.log("{} {}".format(TAG, m))


def main():
    args = [a for a in sys.argv[1:] if a and not a.startswith("-")]
    cat = args[0] if args else "wall"
    eal = unreal.EditorAssetLibrary
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    try:
        ar.wait_for_completion()
    except Exception:
        pass
    villa = eal.load_asset(VILLA)
    cube = eal.load_asset(CUBE)
    if villa is None or cube is None:
        log("missing villa/cube; abort")
        return
    path = "{}/{}".format(TEX_ROOT, cat)
    tex_paths = []
    try:
        for d in ar.get_assets_by_path(path, True, False):
            if str(d.asset_class_path.asset_name) == "Texture2D":
                tex_paths.append("{}.{}".format(str(d.package_name), str(d.asset_name)))
    except Exception as e:
        log("scan {} failed: {}".format(path, e))
    tex_paths.sort()
    log("category '{}' textures: {}".format(cat, len(tex_paths)))
    if not tex_paths:
        log("no textures in {}".format(path))
        return

    cols, gap, z0 = 6, 600.0, 5000.0
    spawned = []
    for i, tp in enumerate(tex_paths[:18]):
        col, row = i % cols, i // cols
        x = col * gap - (cols - 1) * gap * 0.5
        a = eas.spawn_actor_from_object(cube, unreal.Vector(x, 0.0, z0 + row * 750.0))
        a.set_actor_scale3d(unreal.Vector(4.5, 4.5, 4.5))
        mid = a.static_mesh_component.create_dynamic_material_instance(0, villa)
        tex = eal.load_asset(tp)
        if tex is not None:
            mid.set_texture_parameter_value("BaseTop", tex)
            mid.set_texture_parameter_value("BaseSide", tex)
        mid.set_vector_parameter_value("Tint", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
        mid.set_scalar_parameter_value("WorldScale", 220.0)
        nm = tp.split("/")[-1].split(".")[0]
        a.set_actor_label("SWATCH_{}".format(nm))
        a.set_editor_property("tags", [unreal.Name("ARTPASS_A2_Courtyard")])
        spawned.append(a)
        log("grid col{} row{} = {}".format(col, row, nm))

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    try:
        unreal.SystemLibrary.execute_console_command(world, "r.Streaming.FullyLoadUsedTextures 1")
    except Exception:
        pass
    rows = (min(len(tex_paths), 18) + cols - 1) // cols
    rt = unreal.RenderingLibrary.create_render_target2d(world, 1900, 350 * max(1, rows),
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
    except Exception:
        pass
    sun = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 6000),
                                     unreal.Rotator(roll=0.0, pitch=-45.0, yaw=70.0))
    cz = z0 + (rows - 1) * 375.0
    cap.set_actor_location_and_rotation(unreal.Vector(0, -3300, cz),
                                        unreal.Rotator(roll=0.0, pitch=-3.0, yaw=90.0), False, False)
    comp.set_editor_property("fov_angle", 90.0)
    comp.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)

    out_dir = os.path.join(TOOLS_DIR, "Build", "Screenshots")
    state = {"t": 0.0, "done": False, "h": None}

    def do_capture():
        for _ in range(4):
            comp.capture_scene()
        unreal.RenderingLibrary.export_render_target(world, rt, out_dir, "swatch_{}.png".format(cat))
        log("Swatch exported: swatch_{}.png".format(cat))
        for a in spawned + [cap, sun]:
            try:
                eas.destroy_actor(a)
            except Exception:
                pass
        log("SWATCH DONE {}".format(cat))

    def cb(dt):
        if state["done"]:
            return
        state["t"] += dt
        if state["t"] >= 3.0:
            state["done"] = True
            try:
                do_capture()
            finally:
                if state["h"] is not None:
                    try:
                        unreal.unregister_slate_post_tick_callback(state["h"])
                    except Exception:
                        pass

    state["h"] = unreal.register_slate_post_tick_callback(cb)
    log("swatch_{} scheduled (~3s)".format(cat))


main()
