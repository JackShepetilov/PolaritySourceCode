# Probe #4: discover the WorldCoordinate3Way material function + its exact input/output pin
# names, so build_villa_master.py wires the graph correctly (no blind pin guessing).
# Creates a TEMP material to introspect the function-call node, then deletes it.
# Results: Polarity.log [ARTPASS_PROBE4].

import unreal

TAG = "[ARTPASS_PROBE4]"
TMP = "/Game/ArenaArtPass/_tmp_probe_mat"


def log(m):
    unreal.log("{} {}".format(TAG, m))


def warn(m):
    unreal.log_warning("{} {}".format(TAG, m))


def main():
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    try:
        ar.wait_for_completion()
    except Exception:
        pass
    eal = unreal.EditorAssetLibrary

    # 1) find the WorldCoordinate3Way material function
    func = None
    func_path = None
    try:
        cls = unreal.TopLevelAssetPath("/Script/Engine", "MaterialFunction")
        for d in ar.get_assets_by_class(cls, False):
            if str(d.asset_name) == "WorldCoordinate3Way":
                func_path = "{}.{}".format(str(d.package_name), str(d.asset_name))
                func = eal.load_asset(func_path)
                break
    except Exception as e:
        warn("function search failed: {}".format(e))
    log("WorldCoordinate3Way -> {}".format(func_path or "NOT FOUND"))

    # 2) confirm the MaterialExpression python classes we will use exist
    for cn in ["MaterialExpressionTextureObjectParameter", "MaterialExpressionScalarParameter",
               "MaterialExpressionVectorParameter", "MaterialExpressionMaterialFunctionCall",
               "MaterialExpressionMultiply", "MaterialExpressionConstant3Vector"]:
        log("class {} = {}".format(cn, hasattr(unreal, cn)))

    if func is None:
        log("PROBE4 DONE (no function)")
        return

    # 3) temp material, add a function-call node, set the function, dump pin names
    if eal.does_asset_exist(TMP):
        eal.delete_asset(TMP)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mat = tools.create_asset("_tmp_probe_mat", "/Game/ArenaArtPass",
                             unreal.Material, unreal.MaterialFactoryNew())
    mel = unreal.MaterialEditingLibrary
    try:
        fcall = mel.create_material_expression(mat, unreal.MaterialExpressionMaterialFunctionCall, -400, 0)
        try:
            fcall.set_material_function(func)
        except Exception as e:
            warn("set_material_function failed: {}; trying set_editor_property".format(e))
            fcall.set_editor_property("material_function", func)
        # dump inputs
        try:
            fins = fcall.get_editor_property("function_inputs")
            log("function_inputs count = {}".format(len(fins)))
            for fi in fins:
                nm = "?"
                try:
                    fie = fi.get_editor_property("expression_input")
                    nm = str(fie.get_editor_property("input_name"))
                except Exception as e:
                    nm = "<err {}>".format(e)
                log("  IN: {}".format(nm))
        except Exception as e:
            warn("function_inputs read failed: {}".format(e))
        try:
            fouts = fcall.get_editor_property("function_outputs")
            log("function_outputs count = {}".format(len(fouts)))
            for fo in fouts:
                nm = "?"
                try:
                    foe = fo.get_editor_property("expression_output")
                    nm = str(foe.get_editor_property("output_name"))
                except Exception as e:
                    nm = "<err {}>".format(e)
                log("  OUT: {}".format(nm))
        except Exception as e:
            warn("function_outputs read failed: {}".format(e))
    finally:
        if eal.does_asset_exist(TMP):
            eal.delete_asset(TMP)
            log("temp material deleted")
    log("PROBE4 DONE")


main()
