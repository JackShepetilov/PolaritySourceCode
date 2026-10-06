"""M_TracerRibbon: plain additive material for the tracer trail ribbon (Apex-style, see build_ribbons.py).

Across the ribbon (UV.y): hot core + soft glow. Along the trail the fade comes from the particle
alpha (Niagara fades by distance from the head). Shape from DynamicParameter (cm along the path):
comet front over FrontLength (FrontPower: 1 = straight point, <1 = rounder), soft end at the shooter
over TailFadeLength. No near-camera cut: like Apex, the trail starts right at the barrel.
"""

import unreal

FOLDER = "/Game/Effects/Tracers"
NAME = "M_TracerRibbon"
MEL = unreal.MaterialEditingLibrary

PIXEL_CODE = """
// DP.x = distance to the head, DP.y = distance to where the trail started, DP.z = TrailLength (cm, NS_Tracer_*).
float s = saturate(DP.x / max(DP.z, 1.0));                                // 0 at the head, 1 at the far end
float front = pow(saturate(DP.x / max(FrontLength, 1.0)), FrontPower);    // comet point at the head
float rear = lerp(RearWidthStart, RearWidthEnd, pow(s, RearWidthPower));  // rear width profile
float width = front * rear;
float alpha = pow(1.0 - smoothstep(RearFadeStart, 1.0, s), RearFadePower); // rear transparency profile
float tailFade = smoothstep(0.0, max(TailFadeLength, 1.0), DP.y);          // soft end at the shooter
float t = abs(UV.y - 0.5) * 2.0 / max(width, 0.001);                       // across, relative to the shape
float edge = saturate((1.0 - t) * EdgeSoftness);                           // soft rim, nothing outside
float glow = exp(-t * t * GlowFalloff) * GlowStrength;
float core = exp(-t * t * CoreSharp) * pow(1.0 - s, CoreTailFalloff);      // the hot core cools down the tail
float3 col = lerp(Color.rgb, TailColor.rgb, pow(s, TailColorPower));
float3 c = (col * glow + CoreColor.rgb * core * CoreBoost) * Intensity * edge;
return c * PColor.rgb * PColor.a * alpha * tailFade;
"""

SCALARS = {"Intensity": 40.0, "CoreBoost": 2.0, "CoreSharp": 60.0, "GlowFalloff": 3.0, "GlowStrength": 1.0,
           "FrontLength": 300.0, "FrontPower": 0.6, "TailFadeLength": 400.0, "EdgeSoftness": 4.0,
           "RearWidthStart": 1.0, "RearWidthEnd": 0.15, "RearWidthPower": 1.0,
           "RearFadeStart": 0.3, "RearFadePower": 1.5, "CoreTailFalloff": 2.0, "TailColorPower": 1.0}
VECTORS = {"Color": (1.0, 0.45, 0.12, 1.0), "CoreColor": (1.0, 0.9, 0.75, 1.0), "TailColor": (1.0, 0.25, 0.05, 1.0)}


def link(a, a_out, b, b_in):
    if not MEL.connect_material_expressions(a, a_out, b, b_in):
        print("LINK FAILED:", a.get_name(), a_out, "->", b.get_name(), b_in)


def main():
    path = FOLDER + "/" + NAME
    mat = unreal.load_asset(path)
    if mat:
        for e in MEL.get_material_expressions(mat):
            MEL.delete_material_expression(mat, e)
        print("REBUILD:", path)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
        print("CREATED:", path)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("used_with_niagara_ribbons", True)

    c = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, -300, 0)
    c.set_editor_property("code", PIXEL_CODE)
    c.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    c.set_editor_property("description", "TracerRibbon")
    names = ["UV", "PColor", "DP", "Color", "CoreColor", "TailColor"] + list(SCALARS)
    inputs = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        inputs.append(ci)
    c.set_editor_property("inputs", inputs)

    link(MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -700, 0), "", c, "UV")
    link(MEL.create_material_expression(mat, unreal.MaterialExpressionParticleColor, -700, 200), "RGBA", c, "PColor")
    link(MEL.create_material_expression(mat, unreal.MaterialExpressionDynamicParameter, -700, 300), "RGBA", c, "DP")
    y = 350
    for n, v in VECTORS.items():
        p = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -700, y)
        p.set_editor_property("parameter_name", n)
        p.set_editor_property("default_value", unreal.LinearColor(*v))
        link(p, "", c, n)
        y += 160
    for n, v in SCALARS.items():
        p = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, y)
        p.set_editor_property("parameter_name", n)
        p.set_editor_property("default_value", v)
        link(p, "", c, n)
        y += 90
    MEL.connect_material_property(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    print("SAVED:", unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False))


main()
