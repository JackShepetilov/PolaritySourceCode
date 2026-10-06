"""M_Tracer: additive streak for bullet tracers. All the per-view math lives here.

The particle sits ON the bullet (local-space Niagara, Particles.Velocity = bullet velocity).
Vertex shader (WPO) rebuilds the quad:
  - head stays at the bullet, tail goes back Speed * TailTime, never behind the muzzle;
  - width is max(BaseWidth, MinPixels screen pixels) at every vertex, so far tracers stay 1-2 px;
  - own shot (DynamicParameter.w, set by AShooterWeapon::SpawnProjectileAtTransform): the tail starts
    HideDist out, fades in over FadeLen, and the streak is bent from the muzzle the player SEES
    (DynamicParameter.xyz offset) onto the real path over ConvergeDist.
Pixel shader: hot core + soft glow across, fading toward the tail.
Plan: Docs/Tracers_Plan_2026-10-05.md
"""

import unreal

FOLDER = "/Game/Effects/Tracers"
NAME = "M_Tracer"
MEL = unreal.MaterialEditingLibrary

COMMON = """
float T = RelTime * Lifetime * Speed;
float L = Speed * TailTime;
bool own = DP.w > 0.5;
float Lvis = max(own ? min(L, T - HideDist) : min(L, T), 0.0);
float along = dot(d, Dir);
float newAlong = (along > 0.0) ? 0.0 : -Lvis;
float s = max(T + newAlong, 0.0);
float3 bend = own ? DP.xyz * saturate(1.0 - s / max(ConvergeDist, 1.0)) : float3(0.0, 0.0, 0.0);
float3 base = Dir * newAlong + bend;
float pix = 2.0 * TanHalf.x / max(VSize.x, 1.0);
float distV = max(length(toCam - base), 1.0);
float W = max(BaseWidth, distV * pix * MinPixels);
"""

WPO_CODE = COMMON + """
float3 acrossV = d - along * Dir;
float3 acrossN = acrossV / max(length(acrossV), 1e-4);
return base + acrossN * (0.5 * W) - d;
"""

BRIGHT_CODE = COMMON + """
float fade = own ? saturate((T - HideDist) / max(FadeLen, 1.0)) : 1.0;
float comp = clamp(BaseWidth / W, FarDim, 1.0);
// Head/tail and left/right from the geometry itself, not from the sprite UVs (their orientation
// for a velocity-aligned sprite is a guess): head = 1 at the bullet, side = -1..1 across.
float head = (along > 0.0) ? 1.0 : 0.0;
float3 acrossV = d - along * Dir;
float side = (dot(acrossV, cross(Dir, toCam)) > 0.0) ? 1.0 : -1.0;
return float4(fade * comp, W / (distV * pix), head, side);
"""

PIXEL_CODE = """
float t = abs(BW.w);
float sharp = saturate((BW.y - 2.0) / 4.0);
float glow = lerp(1.0 - 0.5 * t, exp(-t * t * 5.0), sharp);
float core = lerp(1.0, exp(-t * t * 40.0), sharp);
float tail = pow(saturate(BW.z), TailPower);
float3 c = (Color * glow + CoreColor * core * CoreBoost) * tail * Intensity * BW.x;
return c * PColor.rgb * PColor.a;
"""

SCALARS = {
    "Lifetime": 12.0, "TailTime": 0.005, "BaseWidth": 2.0, "MinPixels": 1.5,
    "HideDist": 300.0, "FadeLen": 200.0, "ConvergeDist": 2000.0, "FarDim": 0.35,
    "Intensity": 8.0, "CoreBoost": 1.5, "TailPower": 1.5,
}
VECTORS = {"Color": (1.0, 0.45, 0.12, 1.0), "CoreColor": (1.0, 0.9, 0.75, 1.0)}

VS_INPUTS = ["d", "toCam", "Dir", "Speed", "RelTime", "TanHalf", "VSize",
             "Lifetime", "TailTime", "BaseWidth", "MinPixels", "HideDist", "ConvergeDist", "DP"]


def make(mat, cls, x, y):
    return MEL.create_material_expression(mat, cls, x, y)


def custom(mat, code, inputs, out_type, desc, x, y):
    c = make(mat, unreal.MaterialExpressionCustom, x, y)
    c.set_editor_property("code", code)
    c.set_editor_property("output_type", out_type)
    c.set_editor_property("description", desc)
    arr = []
    for n in inputs:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        arr.append(ci)
    c.set_editor_property("inputs", arr)
    return c


def link(a, a_out, b, b_in):
    ok = MEL.connect_material_expressions(a, a_out, b, b_in)
    if not ok:
        print("LINK FAILED:", a.get_name(), a_out, "->", b.get_name(), b_in)


def main():
    path = FOLDER + "/" + NAME
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mat = unreal.load_asset(path)
        # delete_all_material_expressions left orphans from an aborted run: delete one by one.
        for e in MEL.get_material_expressions(mat):
            MEL.delete_material_expression(mat, e)
        MEL.delete_all_material_expressions(mat)
        print("REBUILD:", path)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            NAME, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
        print("CREATED:", path)

    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("used_with_niagara_sprites", True)

    wp = make(mat, unreal.MaterialExpressionWorldPosition, -1600, 0)
    wp.set_editor_property("world_position_shader_offset",
                           unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    ppos = make(mat, unreal.MaterialExpressionParticlePositionWS, -1600, 150)
    cam = make(mat, unreal.MaterialExpressionCameraPositionWS, -1600, 300)
    d = make(mat, unreal.MaterialExpressionSubtract, -1350, 50)
    link(wp, "", d, "A"); link(ppos, "", d, "B")
    tocam = make(mat, unreal.MaterialExpressionSubtract, -1350, 250)
    link(cam, "", tocam, "A"); link(ppos, "", tocam, "B")

    src = {"d": d, "toCam": tocam,
           "Dir": make(mat, unreal.MaterialExpressionParticleDirection, -1600, 450),
           "Speed": make(mat, unreal.MaterialExpressionParticleSpeed, -1600, 550),
           "RelTime": make(mat, unreal.MaterialExpressionParticleRelativeTime, -1600, 650)}
    th = make(mat, unreal.MaterialExpressionViewProperty, -1600, 750)
    th.set_editor_property("property", unreal.MaterialExposedViewProperty.MEVP_TAN_HALF_FIELD_OF_VIEW)
    vs = make(mat, unreal.MaterialExpressionViewProperty, -1600, 850)
    vs.set_editor_property("property", unreal.MaterialExposedViewProperty.MEVP_VIEW_SIZE)
    dp = make(mat, unreal.MaterialExpressionDynamicParameter, -1600, 950)
    src["DP"] = dp
    src["TanHalf"] = th
    src["VSize"] = vs

    y = 1000
    for n, v in SCALARS.items():
        p = make(mat, unreal.MaterialExpressionScalarParameter, -1600, y)
        p.set_editor_property("parameter_name", n)
        p.set_editor_property("default_value", v)
        src[n] = p
        y += 90
    for n, v in VECTORS.items():
        p = make(mat, unreal.MaterialExpressionVectorParameter, -1600, y)
        p.set_editor_property("parameter_name", n)
        p.set_editor_property("default_value", unreal.LinearColor(*v))
        src[n] = p
        y += 160

    wpo = custom(mat, WPO_CODE, VS_INPUTS, unreal.CustomMaterialOutputType.CMOT_FLOAT3, "TracerWPO", -900, 0)
    bright = custom(mat, BRIGHT_CODE, VS_INPUTS + ["FadeLen", "FarDim"],
                    unreal.CustomMaterialOutputType.CMOT_FLOAT4, "TracerBrightness", -900, 500)
    for n in VS_INPUTS:
        out = "RGBA" if n == "DP" else ""
        link(src[n], out, wpo, n)
        link(src[n], out, bright, n)
    link(src["FadeLen"], "", bright, "FadeLen")
    link(src["FarDim"], "", bright, "FarDim")

    interp = make(mat, unreal.MaterialExpressionVertexInterpolator, -600, 500)
    link(bright, "", interp, "")

    uv = make(mat, unreal.MaterialExpressionTextureCoordinate, -600, 700)
    pcol = make(mat, unreal.MaterialExpressionParticleColor, -600, 800)
    pix = custom(mat, PIXEL_CODE, ["UV", "BW", "Color", "CoreColor", "CoreBoost", "TailPower", "Intensity", "PColor"],
                 unreal.CustomMaterialOutputType.CMOT_FLOAT3, "TracerPixel", -300, 600)
    link(uv, "", pix, "UV")
    link(interp, "", pix, "BW")
    for n in ["Color", "CoreColor", "CoreBoost", "TailPower", "Intensity"]:
        link(src[n], "", pix, n)
    link(pcol, "RGBA", pix, "PColor")

    MEL.connect_material_property(wpo, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    MEL.connect_material_property(pix, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    print("SAVED:", unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False))


main()
