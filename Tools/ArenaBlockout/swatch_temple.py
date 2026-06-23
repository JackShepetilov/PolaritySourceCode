# Render the REAL LSJ temple textures (PlazaLabyrinth = the actual plaza pattern, + stripes/whitestone)
# UV-mapped on cubes so I can SEE the true pattern before recreating it hi-res. Tick-deferred capture.
# Output: Build/Screenshots/swatch_temple.png. Log: [SWATCH_T].

import os
import unreal

TAG = "[SWATCH_T]"
TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
KIT = "/Game/Greek_island/Materials/Master/M_Master_kit_detail.M_Master_kit_detail"
CUBE = "/Engine/BasicShapes/Cube.Cube"
TEXES = [
    ("PlazaLabyrinth", "/Game/temple/Textures/PlazaLabyrinth.PlazaLabyrinth"),
    ("Walls_HorizStripes", "/Game/temple/Textures/Walls_HorizStripes.Walls_HorizStripes"),
    ("WhiteStone", "/Game/temple/Textures/WhiteStone.WhiteStone"),
    ("temple_pattern(mine)", "/Game/ArenaArtPass/Textures/temple_pattern.temple_pattern"),
]


def log(m):
    unreal.log("{} {}".format(TAG, m))


def main():
    eal = unreal.EditorAssetLibrary
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    kit = eal.load_asset(KIT)
    cube = eal.load_asset(CUBE)
    if kit is None or cube is None:
        log("missing kit/cube")
        return
    z0 = 5000.0
    spawned = []
    for i, (nm, p) in enumerate(TEXES):
        x = i * 650.0 - 1.5 * 650.0
        a = eas.spawn_actor_from_object(cube, unreal.Vector(x, 0.0, z0))
        a.set_actor_scale3d(unreal.Vector(5.0, 5.0, 5.0))
        mid = a.static_mesh_component.create_dynamic_material_instance(0, kit)
        tex = eal.load_asset(p)
        if tex is not None:
            mid.set_texture_parameter_value("Color", tex)
            mid.set_vector_parameter_value("Base Color", unreal.LinearColor(1, 1, 1, 1))
            try:
                mid.set_scalar_parameter_value("Tiling", 1.0)
            except Exception:
                pass
        else:
            log("MISSING {}".format(p))
        a.set_editor_property("tags", [unreal.Name("ARTPASS_A2_Courtyard")])
        spawned.append(a)
        log("cube {} = {}".format(i, nm))

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    try:
        unreal.SystemLibrary.execute_console_command(world, "r.Streaming.FullyLoadUsedTextures 1")
    except Exception:
        pass
    rt = unreal.RenderingLibrary.create_render_target2d(world, 1800, 500,
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
                                     unreal.Rotator(roll=0.0, pitch=-60.0, yaw=20.0))
    cap.set_actor_location_and_rotation(unreal.Vector(0, -2600, z0 + 40),
                                        unreal.Rotator(roll=0.0, pitch=0.0, yaw=90.0), False, False)
    comp.set_editor_property("fov_angle", 75.0)
    comp.set_editor_property("capture_source", unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
    out_dir = os.path.join(TOOLS_DIR, "Build", "Screenshots")
    st = {"t": 0.0, "done": False, "h": None}

    def cap_now():
        for _ in range(4):
            comp.capture_scene()
        unreal.RenderingLibrary.export_render_target(world, rt, out_dir, "swatch_temple.png")
        log("exported swatch_temple.png")
        for a in spawned + [cap, sun]:
            try:
                eas.destroy_actor(a)
            except Exception:
                pass
        log("SWATCH_T DONE")

    def cb(dt):
        if st["done"]:
            return
        st["t"] += dt
        if st["t"] >= 3.0:
            st["done"] = True
            try:
                cap_now()
            finally:
                if st["h"] is not None:
                    try:
                        unreal.unregister_slate_post_tick_callback(st["h"])
                    except Exception:
                        pass

    st["h"] = unreal.register_slate_post_tick_callback(cb)
    log("scheduled ~3s")


main()
