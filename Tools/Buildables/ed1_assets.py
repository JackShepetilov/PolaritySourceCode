# Buildables: tags, palette, ghost materials, building blueprints, definitions.
# Idempotent: everything checks does_asset_exist / has_color first.
import unreal, json

LOG = []
def log(*a):
    s = " ".join(str(x) for x in a)
    LOG.append(s)
    print(s)

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
ROOT = "/Game/Variant_Shooter/Buildables"

def tag(name):
    t = unreal.GameplayTag()
    t.import_text('(TagName="%s")' % name)
    return t

def compile_bp(path):
    res = unreal.ToolsetRegistry.execute_tool(
        "editor_toolset.toolsets.blueprint.BlueprintTools", "compile_blueprint",
        json.dumps({"blueprint": {"refPath": path + "." + path.split("/")[-1]}}))
    if res.error:
        log("COMPILE ERROR", path, res.error)
    bp = unreal.load_asset(path)
    log("compiled", path, bp.get_editor_property("status") if bp else "no asset")

def main():
    # ---------- tags ----------
    new_tags = ["Buildable.Turret", "Buildable.Dispenser", "Buildable.Teleporter",
                "HUD.Slot.BuildMenu", "HUD.Slot.Buildables",
                "Palette.HUD.PlateSelected", "Palette.HUD.Built"]
    try:
        r = unreal.GameplayTagService.add_tags(new_tags, "Buildables (engineer loop), see Docs/Buildables_Engineer_Plan_2026-09-13.md")
        log("tags:", r)
    except Exception as e:
        log("tags failed:", e)

    # ---------- palette (only when the tag has no colour yet) ----------
    palette = {
        "Palette.HUD.PlateSelected": unreal.LinearColor(0.22, 0.28, 0.36, 1.0),
        "Palette.HUD.Built": unreal.LinearColor(0.55, 0.80, 0.45, 1.0),
    }
    for name, col in palette.items():
        t = tag(name)
        try:
            if not unreal.PolarityPalette.has_color(t):
                unreal.PolarityPalette.set_color(t, col)
                log("palette set", name)
            else:
                log("palette kept", name)
        except Exception as e:
            log("palette failed", name, e)

    if not EAL.does_directory_exist(ROOT):
        EAL.make_directory(ROOT)

    # ---------- ghost materials ----------
    mpath = ROOT + "/M_BuildGhost"
    if not EAL.does_asset_exist(mpath):
        mat = AT.create_asset("M_BuildGhost", ROOT, unreal.Material, unreal.MaterialFactoryNew())
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        mat.set_editor_property("two_sided", True)
        color = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -400, 0)
        color.set_editor_property("parameter_name", "Color")
        color.set_editor_property("default_value", unreal.LinearColor(0.2, 1.0, 0.3, 1.0))
        opac = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -400, 200)
        opac.set_editor_property("parameter_name", "Opacity")
        opac.set_editor_property("default_value", 0.35)
        MEL.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        MEL.connect_material_property(opac, "", unreal.MaterialProperty.MP_OPACITY)
        MEL.recompile_material(mat)
        EAL.save_asset(mpath)
        log("CREATED:", mpath)
    mat = unreal.load_asset(mpath)

    for name, col in (("MI_BuildGhostValid", unreal.LinearColor(0.2, 1.0, 0.3, 1.0)),
                      ("MI_BuildGhostInvalid", unreal.LinearColor(1.0, 0.15, 0.1, 1.0))):
        p = ROOT + "/" + name
        if not EAL.does_asset_exist(p):
            mi = AT.create_asset(name, ROOT, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
            mi.set_editor_property("parent", mat)
            MEL.set_material_instance_vector_parameter_value(mi, "Color", col)
            MEL.update_material_instance(mi)
            EAL.save_asset(p)
            log("CREATED:", p)

    # ---------- building blueprints ----------
    cube = unreal.load_asset("/Engine/BasicShapes/Cube")
    cyl = unreal.load_asset("/Engine/BasicShapes/Cylinder")
    flat = unreal.load_asset("/Game/LevelPrototyping/Materials/M_FlatCol") if EAL.does_asset_exist("/Game/LevelPrototyping/Materials/M_FlatCol") else None
    specs = {
        # name: (mesh, rel_location, rel_scale, metal gib count, metal per gib)
        "BP_Buildable_Turret":     (cube, unreal.Vector(0, 0, 50), unreal.Vector(0.8, 0.8, 1.0), 3, 20),
        "BP_Buildable_Dispenser":  (cyl,  unreal.Vector(0, 0, 50), unreal.Vector(0.9, 0.9, 1.0), 2, 25),
        "BP_Buildable_Teleporter": (cyl,  unreal.Vector(0, 0, 10), unreal.Vector(1.3, 1.3, 0.2), 1, 25),
    }
    for name, (mesh, loc, scale, count, amount) in specs.items():
        p = ROOT + "/" + name
        if not EAL.does_asset_exist(p):
            fac = unreal.BlueprintFactory()
            fac.set_editor_property("parent_class", unreal.BuildableActor)
            AT.create_asset(name, ROOT, unreal.Blueprint, fac)
            log("CREATED:", p)
        cls = EAL.load_blueprint_class(p)
        cdo = unreal.get_default_object(cls)
        mc = cdo.get_editor_property("mesh")
        mc.set_static_mesh(mesh)
        mc.set_editor_property("relative_location", loc)
        mc.set_editor_property("relative_scale3d", scale)
        if flat:
            mc.set_material(0, flat)
        loot = cdo.get_editor_property("loot_drop")
        entry = unreal.LootDropEntry()
        entry.set_editor_property("pickup_class", unreal.MetalPickup)
        entry.set_editor_property("chance", 1.0)
        entry.set_editor_property("count", count)
        entry.set_editor_property("amount", amount)
        loot.set_editor_property("loot", [entry])
        compile_bp(p)
        EAL.save_asset(p)
        log("MODIFIED:", p, "mesh", mesh.get_name(), "loot", count, "x", amount)

    # ---------- preview blueprint ----------
    pp = ROOT + "/BP_BuildablePreview"
    if not EAL.does_asset_exist(pp):
        fac = unreal.BlueprintFactory()
        fac.set_editor_property("parent_class", unreal.BuildablePreview)
        AT.create_asset("BP_BuildablePreview", ROOT, unreal.Blueprint, fac)
        log("CREATED:", pp)
    pcls = EAL.load_blueprint_class(pp)
    pcdo = unreal.get_default_object(pcls)
    pcdo.set_editor_property("valid_material", unreal.load_asset(ROOT + "/MI_BuildGhostValid"))
    pcdo.set_editor_property("invalid_material", unreal.load_asset(ROOT + "/MI_BuildGhostInvalid"))
    compile_bp(pp)
    EAL.save_asset(pp)

    # ---------- definitions ----------
    defs = {
        "DA_Buildable_Turret":     ("Buildable.Turret",     "Turret",     "BP_Buildable_Turret",     130, 10.5, 1),
        "DA_Buildable_Dispenser":  ("Buildable.Dispenser",  "Dispenser",  "BP_Buildable_Dispenser",  100, 21.0, 1),
        "DA_Buildable_Teleporter": ("Buildable.Teleporter", "Teleporter", "BP_Buildable_Teleporter", 50,  21.0, 2),
    }
    for name, (tname, disp, bp, cost, btime, maxc) in defs.items():
        p = ROOT + "/" + name
        if not EAL.does_asset_exist(p):
            fac = unreal.DataAssetFactory()
            fac.set_editor_property("data_asset_class", unreal.BuildableDefinition)
            AT.create_asset(name, ROOT, None, fac)
            log("CREATED:", p)
        da = unreal.load_asset(p)
        da.set_editor_property("buildable_tag", tag(tname))
        da.set_editor_property("display_name", disp)
        da.set_editor_property("actor_class", EAL.load_blueprint_class(ROOT + "/" + bp))
        da.set_editor_property("metal_cost", cost)
        levels = []
        for hp, upg in ((150.0, 200), (180.0, 200), (216.0, 0)):
            st = unreal.BuildableLevelStats()
            st.set_editor_property("max_health", hp)
            st.set_editor_property("build_time", btime)
            st.set_editor_property("upgrade_cost", upg)
            levels.append(st)
        da.set_editor_property("levels", levels)
        da.set_editor_property("max_count_per_player", maxc)
        EAL.save_asset(p)
        log("MODIFIED:", p, cost, btime, maxc)

    log("DONE")

main()
