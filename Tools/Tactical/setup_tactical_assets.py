import unreal
import json
import os
import tempfile

# Tactical side rail devices (2026-10-01), prototype assets. Docs/TacticalAttachment_Plan_2026-10-01.md.
# Run after the C++ is built, through the launcher (execute_python_code cuts long code):
#   Tools/mcp.sh py Source/Tools/Tactical/run_step.py    (edit STEP there)
# Idempotent: creates what is missing, fills prototype fields only when they are empty.

ROOT = "/Game/Variant_Shooter/Blueprints/Pickups/Attachments/Tactical"
MAT_DIR = "/Game/Variant_Shooter/Tactical/Materials"
MASTER = MAT_DIR + "/M_TacticalProto"
CURVE = ROOT + "/Curve_Tactical_DazzleByLitTime"
# The real rail parts from the Infima pack. NOT LowPolyShooterPack/Art/Effects/Models/SM_LaserSight:
# that one is an effect mesh (next to the muzzle flash), not the device body.
PART_DIR = "/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Weapons/_Common/Attachments/Models/"
PART_FLASHLIGHT = PART_DIR + "SM_ATT_Laser_Flashlight_01"
PART_LASER = PART_DIR + "SM_ATT_Laser_Sight_01"
HUD_LAYOUT = "/Game/Variant_Shooter/UI/Widgets/HUD/Registry/DA_HudLayout"
BT = "editor_toolset.toolsets.blueprint.BlueprintTools"
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary


def tools():
    return unreal.AssetToolsHelpers.get_asset_tools()


# ---------------------------------------------------------------- materials

def make_master():
    if EAL.does_asset_exist(MASTER):
        print("EXISTS:", MASTER)
        return unreal.load_asset(MASTER)
    m = tools().create_asset("M_TacticalProto", MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("used_with_skeletal_mesh", True)
    m.set_editor_property("used_with_static_lighting", False)
    color = MEL.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -600, 0)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
    boost = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -600, 200)
    boost.set_editor_property("parameter_name", "EmissiveBoost")
    boost.set_editor_property("default_value", 2.0)
    mul = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -300, 50)
    MEL.connect_material_expressions(color, "RGB", mul, "A")
    MEL.connect_material_expressions(boost, "", mul, "B")
    MEL.connect_material_property(mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    # Rim-lit opacity: edges of a dome or a body read stronger than the middle.
    fres = MEL.create_material_expression(m, unreal.MaterialExpressionFresnel, -600, 400)
    fres.set_editor_property("exponent", 2.0)
    fres.set_editor_property("base_reflect_fraction", 0.35)
    op = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -600, 600)
    op.set_editor_property("parameter_name", "Opacity")
    op.set_editor_property("default_value", 0.3)
    opmul = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -300, 450)
    MEL.connect_material_expressions(fres, "", opmul, "A")
    MEL.connect_material_expressions(op, "", opmul, "B")
    MEL.connect_material_property(opmul, "", unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(m)
    EAL.save_asset(MASTER)
    print("CREATED:", MASTER)
    return m


def make_mi(name, color, opacity, boost):
    path = MAT_DIR + "/" + name
    if EAL.does_asset_exist(path):
        print("EXISTS:", path)
        return unreal.load_asset(path)
    mi = tools().create_asset(name, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property("parent", unreal.load_asset(MASTER))
    MEL.set_material_instance_vector_parameter_value(mi, "Color", unreal.LinearColor(*color))
    MEL.set_material_instance_scalar_parameter_value(mi, "Opacity", opacity)
    MEL.set_material_instance_scalar_parameter_value(mi, "EmissiveBoost", boost)
    MEL.update_material_instance(mi)
    EAL.save_asset(path)
    print("CREATED:", path)
    return mi


def step_materials():
    make_master()
    make_mi("MI_TacticalProto_Shield", (0.3, 0.6, 1.0, 1.0), 0.35, 2.0)
    make_mi("MI_TacticalProto_Beam", (1.0, 0.05, 0.05, 1.0), 1.0, 20.0)
    make_mi("MI_TacticalProto_Freeze", (0.5, 0.9, 1.0, 1.0), 0.25, 1.5)
    make_mi("MI_TacticalProto_Dazzled", (1.0, 0.85, 0.3, 1.0), 0.8, 3.0)
    make_mi("MI_TacticalProto_Frozen", (0.4, 0.85, 1.0, 1.0), 0.8, 3.0)


# ---------------------------------------------------------------- curve

def step_curve():
    if EAL.does_asset_exist(CURVE):
        print("EXISTS:", CURVE)
        return
    # No header row: it would become a key at (0, 0) (Docs/Gotchas/Python_Editor.md).
    rows = [(0.0, 0.0), (0.15, 1.0), (0.5, 2.0), (1.0, 3.5), (2.0, 6.0), (3.0, 8.0)]
    csv = os.path.join(tempfile.gettempdir(), "Curve_Tactical_DazzleByLitTime.csv")
    with open(csv, "w") as f:
        f.write("\n".join("%g,%g" % r for r in rows) + "\n")
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", csv)
    task.set_editor_property("destination_path", ROOT)
    task.set_editor_property("destination_name", "Curve_Tactical_DazzleByLitTime")
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    fac = unreal.CSVImportFactory()
    settings = unreal.CSVImportSettings()
    settings.set_editor_property("import_type", unreal.CSVImportType.ECSV_CURVE_FLOAT)
    fac.set_editor_property("automated_import_settings", settings)
    task.set_editor_property("factory", fac)
    tools().import_asset_tasks([task])
    c = unreal.load_asset(CURVE)
    print("CREATED:", CURVE, c.get_time_range() if c else None)


# ---------------------------------------------------------------- devices and attachments

def get_or_create_da(name, cls):
    path = ROOT + "/" + name
    if EAL.does_asset_exist(path):
        return unreal.load_asset(path), False
    fac = unreal.DataAssetFactory()
    fac.set_editor_property("data_asset_class", cls)
    a = tools().create_asset(name, ROOT, None, fac)
    print("CREATED:", path)
    return a, True


def fill_if_empty(asset, prop, value):
    if asset.get_editor_property(prop) is None:
        asset.set_editor_property(prop, value)
        print("MODIFIED:", asset.get_name(), prop)


def step_devices():
    mats = {n: unreal.load_asset(MAT_DIR + "/" + n) for n in
            ("MI_TacticalProto_Shield", "MI_TacticalProto_Beam", "MI_TacticalProto_Freeze",
             "MI_TacticalProto_Dazzled", "MI_TacticalProto_Frozen")}
    curve = unreal.load_asset(CURVE)
    sphere = unreal.load_asset("/Engine/BasicShapes/Sphere")
    cyl = unreal.load_asset("/Engine/BasicShapes/Cylinder")
    cone = unreal.load_asset("/Engine/BasicShapes/Cone")

    sh, new = get_or_create_da("DA_TacticalDevice_Shield", unreal.TacticalDevice_Shield)
    fill_if_empty(sh, "shield_mesh", sphere)
    fill_if_empty(sh, "shield_material", mats["MI_TacticalProto_Shield"])

    fl, new = get_or_create_da("DA_TacticalDevice_Flashlight", unreal.TacticalDevice_Light)
    fill_if_empty(fl, "dazzle_duration_by_lit_time", curve)
    fill_if_empty(fl, "affected_overlay_material", mats["MI_TacticalProto_Dazzled"])

    la, new = get_or_create_da("DA_TacticalDevice_Laser", unreal.TacticalDevice_Light)
    if new:
        la.set_editor_property("range", 4000.0)
        la.set_editor_property("cone_half_angle", 1.5)
        la.set_editor_property("light_intensity", 300.0)
        la.set_editor_property("draw_beam", True)
        la.set_editor_property("color", unreal.LinearColor(1.0, 0.1, 0.1, 1.0))
    fill_if_empty(la, "dazzle_duration_by_lit_time", curve)
    fill_if_empty(la, "affected_overlay_material", mats["MI_TacticalProto_Dazzled"])
    fill_if_empty(la, "beam_mesh", cyl)
    fill_if_empty(la, "beam_material", mats["MI_TacticalProto_Beam"])

    fr, new = get_or_create_da("DA_TacticalDevice_Freeze", unreal.TacticalDevice_Freeze)
    fill_if_empty(fr, "cone_mesh", cone)
    fill_if_empty(fr, "cone_material", mats["MI_TacticalProto_Freeze"])
    fill_if_empty(fr, "affected_overlay_material", mats["MI_TacticalProto_Frozen"])

    flash = unreal.load_asset(PART_FLASHLIGHT)
    laser = unreal.load_asset(PART_LASER)
    for dev, label, part in ((sh, "Gun Shield", flash), (fl, "Flashlight", flash), (la, "Laser", laser), (fr, "Freezer", flash)):
        name = "DA_Attach_Tactical_" + dev.get_name().replace("DA_TacticalDevice_", "")
        att, new = get_or_create_da(name, unreal.WeaponAttachmentDefinition)
        if new:
            att.set_editor_property("type", unreal.WeaponAttachmentType.TACTICAL)
            att.set_editor_property("display_name", label)
            # Every gun: whether a gun takes it is the gun's AttachmentSlots.
            att.set_editor_property("compatible_weapons", [unreal.ShooterWeapon])
        fill_if_empty(att, "device", dev)
        fill_if_empty(att, "mesh", part)

    for a in (sh, fl, la, fr):
        EAL.save_loaded_asset(a)
    for n in ("Shield", "Flashlight", "Laser", "Freeze"):
        EAL.save_asset(ROOT + "/DA_Attach_Tactical_" + n)
    print("SAVED devices and attachments")


# ---------------------------------------------------------------- weapons

WEAPON_DIRS = ["/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation"]
SKIP = ("BP_RPG7",)


def compile_bp(path):
    res = unreal.ToolsetRegistry.execute_tool(BT, "compile_blueprint",
        json.dumps({"blueprint": {"refPath": path + "." + path.split("/")[-1]}}))
    return res.error if res.error else "ok"


def step_weapons():
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    done = 0
    for d in WEAPON_DIRS:
        for ad in ar.get_assets_by_path(d, recursive=True):
            if str(ad.asset_class_path.asset_name) != "Blueprint":
                continue
            name = str(ad.asset_name)
            if name in SKIP:
                continue
            path = str(ad.package_name)
            cls = EAL.load_blueprint_class(path)
            if not cls:
                continue
            cdo = unreal.get_default_object(cls)
            if not isinstance(cdo, unreal.ShooterWeapon) or isinstance(cdo, unreal.ShooterWeapon_Melee):
                continue
            slots = list(cdo.get_editor_property("attachment_slots"))
            if unreal.WeaponAttachmentType.TACTICAL in slots:
                print("HAS:", name)
                continue
            slots.append(unreal.WeaponAttachmentType.TACTICAL)
            cdo.set_editor_property("attachment_slots", slots)
            r = compile_bp(path)
            EAL.save_asset(path)
            print("MODIFIED:", name, "slots", len(slots), r)
            done += 1
    print("WEAPONS done", done)


# ---------------------------------------------------------------- HUD

def step_hud_layout():
    layout = unreal.load_asset(HUD_LAYOUT)
    slots = list(layout.get_editor_property("slots"))
    for s in slots:
        if "HUD.Slot.Tactical" in s.export_text():
            print("HUD binding exists")
            return
    b = unreal.HudSlotBinding()
    b.import_text('(SlotTag=(TagName="HUD.Slot.Tactical"),WidgetClass="/Script/Polarity.TacticalChargeWidget",bStartHidden=False)')
    slots.append(b)
    layout.set_editor_property("slots", slots)
    EAL.save_asset(HUD_LAYOUT)
    print("MODIFIED:", HUD_LAYOUT, len(slots))


HUD_ROOT = "/Game/Variant_Shooter/UI/Widgets/HUD/Registry/WBP_HudRoot"


def step_hud_root():
    w = unreal.load_object(None, HUD_ROOT + ".WBP_HudRoot:WidgetTree.Slot_Weapon")
    s = w.slot
    print("WEAPON anchors", s.get_anchors().export_text(), "pos", s.get_position(), "size", s.get_size(),
          "align", s.get_alignment(), "auto", s.get_auto_size(), "z", s.get_z_order())
    print("WEAPON size box", w.get_editor_property("width_override"), w.get_editor_property("height_override"),
          w.get_editor_property("slot_tag").export_text())
    existing = unreal.load_object(None, HUD_ROOT + ".WBP_HudRoot:WidgetTree.Slot_Tactical")
    print("TACTICAL exists:", existing is not None)


def step_hud_root_add():
    existing = unreal.load_object(None, HUD_ROOT + ".WBP_HudRoot:WidgetTree.Slot_Tactical")
    if existing is None:
        r = unreal.WidgetService.add_component(HUD_ROOT, "HudSlot", "Slot_Tactical", "Canvas", False)
        print("ADDED:", r)
    w = unreal.load_object(None, HUD_ROOT + ".WBP_HudRoot:WidgetTree.Slot_Tactical")
    t = unreal.GameplayTag()
    t.import_text('(TagName="HUD.Slot.Tactical")')
    w.set_editor_property("slot_tag", t)
    w.set_width_override(260.0)
    w.set_height_override(10.0)
    s = w.slot
    a = unreal.Anchors()
    a.import_text("(Minimum=(X=1.0,Y=1.0),Maximum=(X=1.0,Y=1.0))")
    s.set_anchors(a)
    s.set_alignment(unreal.Vector2D(1.0, 1.0))
    s.set_position(unreal.Vector2D(-48.0, -234.0))
    s.set_size(unreal.Vector2D(260.0, 10.0))
    s.set_auto_size(False)
    res = unreal.ToolsetRegistry.execute_tool(BT, "compile_blueprint",
        json.dumps({"blueprint": {"refPath": HUD_ROOT + ".WBP_HudRoot"}}))
    EAL.save_asset(HUD_ROOT)
    print("MODIFIED:", HUD_ROOT, res.error if res.error else "compiled", unreal.load_asset(HUD_ROOT).get_editor_property("status"))
    step_hud_layout()
