"""Let tracer pixels (custom stencil 40, set in AShooterWeapon::SpawnProjectileAtTransform) pass the
map's painterly post process untouched: Kuwahara smears a thin bright line into a blot and the outline
draws a dark rim around what is left.

Each material gets one Custom node in front of Emissive:
    stencil == 40 ? PostProcessInput0 (what came in) : <the filter's own result>
Idempotent: a material that already has the node (description TracerStencilPassthrough) is skipped.
Backup of the originals: Saved/Backup_PP_Kobo_2026-10-05/.
Author's explicit permission to edit these three materials: 2026-10-05.
"""

import unreal

MEL = unreal.MaterialEditingLibrary
TAG = "TracerStencilPassthrough"
STENCIL = 40
MATERIALS = [
    "/Game/Kobo_Nature/Materials/M-PostProcess/M-KuwaharaFilter",
    "/Game/Kobo_Nature/Materials/M-PostProcess/M-Outline",
    "/Game/Kobo_Nature/Materials/M-PostProcess/M-Sharpen",
]
CODE = "return (abs(Stencil.r - %d.0) < 0.5) ? Orig.rgb : Filtered.rgb;" % STENCIL


def scene_texture(mat, tex_id, x, y):
    n = MEL.create_material_expression(mat, unreal.MaterialExpressionSceneTexture, x, y)
    n.set_editor_property("scene_texture_id", tex_id)
    return n


def patch(path):
    mat = unreal.load_asset(path)
    for e in MEL.get_material_expressions(mat):
        if e.get_class().get_name() == "MaterialExpressionCustom" and e.get_editor_property("description") == TAG:
            # Already patched: only refresh the code (first version compared a float4 and failed to compile).
            if e.get_editor_property("code") != CODE:
                e.set_editor_property("code", CODE)
                MEL.recompile_material(mat)
                print("MODIFIED code:", path, "| SAVED:", unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False))
            else:
                print("SKIP (already patched):", path)
            return
    filtered = MEL.get_material_property_input_node(mat, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    out_name = MEL.get_material_property_input_node_output_name(mat, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    print(path.split("/")[-1], "emissive fed by", filtered.get_name() if filtered else None, "output", repr(out_name))
    if not filtered:
        print("NO EMISSIVE INPUT, left alone:", path)
        return
    x = filtered.get_editor_property("material_expression_editor_x") + 300
    y = filtered.get_editor_property("material_expression_editor_y")

    c = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, x, y)
    c.set_editor_property("code", CODE)
    c.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    c.set_editor_property("description", TAG)
    ins = []
    for n in ("Filtered", "Orig", "Stencil"):
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    c.set_editor_property("inputs", ins)

    orig = scene_texture(mat, unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0, x - 250, y + 150)
    stencil = scene_texture(mat, unreal.SceneTextureId.PPI_CUSTOM_STENCIL, x - 250, y + 300)
    ok = [MEL.connect_material_expressions(filtered, out_name or "", c, "Filtered"),
          MEL.connect_material_expressions(orig, "Color", c, "Orig"),
          MEL.connect_material_expressions(stencil, "Color", c, "Stencil")]
    MEL.connect_material_property(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    print("  links:", ok)
    MEL.recompile_material(mat)
    print("MODIFIED:", path, "| SAVED:", unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False))


for p in MATERIALS:
    patch(p)
