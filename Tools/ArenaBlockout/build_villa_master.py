# Builds MY OWN villa master M_ArtPass_Villa (v3): full PBR, world-projected via WorldCoordinate3Way
# so tiling is constant in world space regardless of object size.
#   Base_Texture --WCW--> XYZ --xTint--> Base Color
#   NormalMap    --WCW--> XYZ --(x2-1)--> Normal
#   ARM          --WCW--> XYZ --> G:Roughness  B:Metallic  R:AmbientOcclusion
#   WorldScale (scalar) feeds all 3 WCW scale inputs (maps stay aligned).
# Params for MICs: Base_Texture, NormalMap, ARM, WorldScale, Tint. Run via REST. Log [VILLA_MAT].

import sys

import unreal

TAG = "[VILLA_MAT]"
MAT_DIR = "/Game/ArenaArtPass/Materials"
MAT_NAME = "M_ArtPass_Villa"
MAT_PATH = "{}/{}".format(MAT_DIR, MAT_NAME)
WCW = "/Engine/Functions/Engine_MaterialFunctions03/Texturing/WorldCoordinate3Way.WorldCoordinate3Way"
DEF_DIFF = "/Game/ArenaArtPass/Textures/beige_wall_001.beige_wall_001"
DEF_NOR = "/Game/ArenaArtPass/Textures/beige_wall_001_nor_gl.beige_wall_001_nor_gl"
DEF_ARM = "/Game/ArenaArtPass/Textures/beige_wall_001_arm.beige_wall_001_arm"


def log(m):
    unreal.log("{} {}".format(TAG, m))


def warn(m):
    unreal.log_warning("{} {}".format(TAG, m))


def cx(mel, frm, fout, to, name, what):
    try:
        if mel.connect_material_expressions(frm, fout, to, name):
            return True
    except Exception:
        pass
    warn("connect FAIL {} -> '{}'".format(what, name))
    return False


def make_wcw(mel, mat, func, tex_expr, scale_expr, y):
    f = mel.create_material_expression(mat, unreal.MaterialExpressionMaterialFunctionCall, -560, y)
    try:
        f.set_material_function(func)
    except Exception:
        f.set_editor_property("material_function", func)
    for nm in ("XY Texture", "YZ Texture", "XZ Texture"):
        cx(mel, tex_expr, "", f, nm, "tex")
    for nm in ("XY Scale", "YZ Scale", "XZ Scale"):
        cx(mel, scale_expr, "", f, nm, "scale")
    return f


def texparam(mel, mat, name, default_path, y):
    e = mel.create_material_expression(mat, unreal.MaterialExpressionTextureObjectParameter, -950, y)
    e.set_editor_property("parameter_name", name)
    t = unreal.EditorAssetLibrary.load_asset(default_path)
    if t is not None:
        e.set_editor_property("texture", t)
    return e


def mask(mel, mat, r, g, b, y):
    m = mel.create_material_expression(mat, unreal.MaterialExpressionComponentMask, -120, y)
    m.set_editor_property("r", r)
    m.set_editor_property("g", g)
    m.set_editor_property("b", b)
    m.set_editor_property("a", False)
    return m


def main():
    eal = unreal.EditorAssetLibrary
    mel = unreal.MaterialEditingLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    func = eal.load_asset(WCW)
    if func is None:
        warn("WCW missing")
        log("RESULT: FAILED")
        return
    if not eal.does_directory_exist(MAT_DIR):
        eal.make_directory(MAT_DIR)
    # SAFETY: never silently clobber a master that already exists (it may hold the author's
    # manual edits). Refuse unless explicitly forced.
    force = "--force" in sys.argv[1:]
    if eal.does_asset_exist(MAT_PATH) and not force:
        warn("{} already exists - NOT overwriting (your manual edits are safe). "
             "Pass --force to rebuild from scratch.".format(MAT_PATH))
        log("RESULT: SKIPPED (master preserved)")
        return
    if eal.does_asset_exist(MAT_PATH):
        eal.delete_asset(MAT_PATH)
        log("Deleted existing {} (--force)".format(MAT_PATH))
    mat = tools.create_asset(MAT_NAME, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())

    scale = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -950, 520)
    scale.set_editor_property("parameter_name", "WorldScale")
    scale.set_editor_property("default_value", 300.0)

    base_t = texparam(mel, mat, "Base_Texture", DEF_DIFF, -150)
    nor_t = texparam(mel, mat, "NormalMap", DEF_NOR, 180)
    arm_t = texparam(mel, mat, "ARM", DEF_ARM, 420)

    base_w = make_wcw(mel, mat, func, base_t, scale, -150)
    nor_w = make_wcw(mel, mat, func, nor_t, scale, 180)
    arm_w = make_wcw(mel, mat, func, arm_t, scale, 420)

    # Base color = WCW(base) * Tint
    tint = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -350, -40)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    mult = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -120, -120)
    cx(mel, base_w, "XYZ Output", mult, "A", "base->mult.A")
    cx(mel, tint, "", mult, "B", "tint->mult.B")
    log("BaseColor = {}".format(mel.connect_material_property(mult, "", unreal.MaterialProperty.MP_BASE_COLOR)))

    # Normal = (WCW(nor) - 0.5) * 2
    bs = mel.create_material_expression(mat, unreal.MaterialExpressionConstantBiasScale, -120, 180)
    bs.set_editor_property("bias", -0.5)
    bs.set_editor_property("scale", 2.0)
    cx(mel, nor_w, "XYZ Output", bs, "", "nor->biasscale")
    log("Normal = {}".format(mel.connect_material_property(bs, "", unreal.MaterialProperty.MP_NORMAL)))

    # ARM: G=Roughness, B=Metallic, R=AO
    mg = mask(mel, mat, False, True, False, 420)
    mb = mask(mel, mat, False, False, True, 500)
    mr = mask(mel, mat, True, False, False, 580)
    cx(mel, arm_w, "XYZ Output", mg, "", "arm->maskG")
    cx(mel, arm_w, "XYZ Output", mb, "", "arm->maskB")
    cx(mel, arm_w, "XYZ Output", mr, "", "arm->maskR")
    log("Roughness = {}".format(mel.connect_material_property(mg, "", unreal.MaterialProperty.MP_ROUGHNESS)))
    log("Metallic = {}".format(mel.connect_material_property(mb, "", unreal.MaterialProperty.MP_METALLIC)))
    try:
        log("AO = {}".format(mel.connect_material_property(mr, "", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)))
    except Exception as e:
        warn("AO connect: {}".format(e))

    mel.recompile_material(mat)
    eal.save_asset(MAT_PATH)
    log("Built {} (Base_Texture, NormalMap, ARM, WorldScale, Tint)".format(MAT_PATH))
    log("RESULT: SUCCESS")


main()
