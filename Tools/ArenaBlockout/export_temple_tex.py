# Export the real temple PlazaLabyrinth texture to a PNG so the exact plaza pattern can be seen
# (and recreated hi-res). Tries AssetExportTask; falls back to render-target draw. Log [EXP].

import os
import unreal

TAG = "[EXP]"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "DownloadedTextures")
TEX = "/Game/temple/Textures/PlazaLabyrinth.PlazaLabyrinth"


def log(m):
    unreal.log("{} {}".format(TAG, m))


def warn(m):
    unreal.log_warning("{} {}".format(TAG, m))


def main():
    tex = unreal.EditorAssetLibrary.load_asset(TEX)
    if tex is None:
        warn("PlazaLabyrinth not found")
        return
    try:
        log("size {}x{}".format(tex.blueprint_get_size_x(), tex.blueprint_get_size_y()))
    except Exception:
        pass
    dest = os.path.join(OUT, "ref_PlazaLabyrinth.png")
    # 1) AssetExportTask + TextureExporterPNG
    try:
        task = unreal.AssetExportTask()
        task.set_editor_property("object", tex)
        task.set_editor_property("filename", dest)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_identical", True)
        task.set_editor_property("prompt", False)
        task.set_editor_property("exporter", unreal.TextureExporterPNG())
        ok = unreal.Exporter.run_asset_export_task(task)
        log("AssetExportTask ok={} -> {} exists={}".format(ok, dest, os.path.isfile(dest)))
        if os.path.isfile(dest):
            log("EXP DONE (export task)")
            return
    except Exception as e:
        warn("export task failed: {}".format(e))
    # 2) fallback: draw texture to a render target via a transient emissive material
    try:
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        sx = tex.blueprint_get_size_x() or 512
        sy = tex.blueprint_get_size_y() or 512
        rt = unreal.RenderingLibrary.create_render_target2d(world, int(sx), int(sy),
                                                            unreal.TextureRenderTargetFormat.RTF_RGBA8)
        mel = unreal.MaterialEditingLibrary
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        m = tools.create_asset("M_tmp_texdump", "/Game/ArenaArtPass", unreal.Material, unreal.MaterialFactoryNew())
        m.set_editor_property("material_domain", unreal.MaterialDomain.MD_UI)  # unlit-ish flat
        ts = mel.create_material_expression(m, unreal.MaterialExpressionTextureSample, -300, 0)
        ts.set_editor_property("texture", tex)
        mel.connect_material_property(ts, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        mel.recompile_material(m)
        unreal.RenderingLibrary.draw_material_to_render_target(world, rt, m)
        unreal.RenderingLibrary.export_render_target(world, rt, OUT, "ref_PlazaLabyrinth.png")
        unreal.EditorAssetLibrary.delete_asset("/Game/ArenaArtPass/M_tmp_texdump")
        log("fallback draw exists={}".format(os.path.isfile(dest)))
        log("EXP DONE (fallback)")
    except Exception as e:
        warn("fallback failed: {}".format(e))
        log("EXP FAILED")


main()
