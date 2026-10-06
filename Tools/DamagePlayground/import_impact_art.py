"""Импорт заготовок эффектов попадания и материалы под них.

Текстуры: Docs/VFX_Refs_2026-10-03/processed/*.png (512, альфа посчитана из яркости).
Материалы unlit, под спрайты Niagara. Обломки Masked (без сортировки и перерисовки), ворс
Translucent, щит Additive. Идемпотентно: существующие ассеты пересобираются на месте.
"""

import unreal

SRC = r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Docs/VFX_Refs_2026-10-03/processed/"
TEX_DIR = "/Game/Effects/Impacts/Textures"
MAT_DIR = "/Game/Effects/Impacts/Materials"
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary

# (текстура, материал, режим, субкадры)
ROWS = (
    ("T_Impact_ArmorFragments_4x4", "M_Impact_ArmorFragments", "masked", True),
    ("T_Impact_ClothScraps_2x2", "M_Impact_ClothScraps", "masked", True),
    ("T_Impact_FluffPuffs_2x2", "M_Impact_FluffPuffs", "translucent", True),
    ("T_Shield_Shards_4x4", "M_Shield_Shards", "additive", True),
    ("T_Shield_Ring", "M_Shield_Ring", "additive", False),
    ("T_Blood_Drops_4x4", "M_Blood_Drops", "masked", True),
    ("T_Blood_Burst_2x2", "M_Blood_Burst", "masked", True),
    ("T_Blood_Mist_2x2", "M_Blood_Mist", "translucent", True),
)


def import_textures():
    tasks = []
    for tex, _, _, _ in ROWS:
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", SRC + tex + ".png")
        t.set_editor_property("destination_path", TEX_DIR)
        t.set_editor_property("destination_name", tex)
        t.set_editor_property("automated", True)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("save", True)
        tasks.append(t)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    for tex, _, _, _ in ROWS:
        a = unreal.load_asset(TEX_DIR + "/" + tex)
        print("IMPORTED:", tex, bool(a))


def build_materials():
    at = unreal.AssetToolsHelpers.get_asset_tools()
    for tex, mat_name, mode, subuv in ROWS:
        path = MAT_DIR + "/" + mat_name
        mat = unreal.load_asset(path) if EAL.does_asset_exist(path) else at.create_asset(mat_name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
        MEL.delete_all_material_expressions(mat)
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        mat.set_editor_property("two_sided", True)
        mat.set_editor_property("used_with_niagara_sprites", True)
        mat.set_editor_property("blend_mode", {
            "masked": unreal.BlendMode.BLEND_MASKED,
            "translucent": unreal.BlendMode.BLEND_TRANSLUCENT,
            "additive": unreal.BlendMode.BLEND_ADDITIVE}[mode])

        cls = unreal.MaterialExpressionParticleSubUV if subuv else unreal.MaterialExpressionTextureSample
        ts = MEL.create_material_expression(mat, cls, -600, 0)
        ts.set_editor_property("texture", unreal.load_asset(TEX_DIR + "/" + tex))
        pc = MEL.create_material_expression(mat, unreal.MaterialExpressionParticleColor, -600, 300)
        rgb = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, 0)
        MEL.connect_material_expressions(ts, "RGB", rgb, "A")
        MEL.connect_material_expressions(pc, "", rgb, "B")
        if mode == "additive":
            # Аддитив гасится яркостью: альфа частицы умножает цвет.
            dim = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -100, 100)
            MEL.connect_material_expressions(rgb, "", dim, "A")
            MEL.connect_material_expressions(pc, "A", dim, "B")
            MEL.connect_material_property(dim, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        else:
            MEL.connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
            al = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, 300)
            MEL.connect_material_expressions(ts, "A", al, "A")
            MEL.connect_material_expressions(pc, "A", al, "B")
            MEL.connect_material_property(al, "", unreal.MaterialProperty.MP_OPACITY_MASK if mode == "masked"
                                          else unreal.MaterialProperty.MP_OPACITY)
        MEL.recompile_material(mat)
        print("BUILT:", mat_name, mode, "| SAVED:", EAL.save_asset(path, only_if_is_dirty=False))


def check():
    for _, mat_name, _, _ in ROWS:
        m = unreal.load_asset(MAT_DIR + "/" + mat_name)
        st = MEL.get_statistics(m)
        print("STAT:", mat_name, "ps instr", st.num_pixel_shader_instructions, "samplers", st.num_samplers)
