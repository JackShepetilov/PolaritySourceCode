# Feedback for the kill bonuses and the melee hold: the notice line under the crosshair (WBP_Notice),
# the hold prompt (WBP_Hold), the screen-edge glow material, the see-through melee material, and the
# data that points at them. Also takes the pin-pull stage out of the thrown abilities.
# Idempotent on creation; configuration is rewritten each run. Run from the editor: main().
import unreal, json

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
WS = unreal.WidgetService
MEL = unreal.MaterialEditingLibrary
R = "/Game/Variant_Shooter/UI/Widgets/HUD/Registry"
FX = "/Game/Effects/Materials"
AB = "/Game/Variant_Shooter/Blueprints/Abilities"
UP = "/Game/Variant_Shooter/Blueprints/Upgrades"

def log(*a):
    print(" ".join(str(x) for x in a))
    unreal.log("[FEEDBACK_HUD] " + " ".join(str(x) for x in a))

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
    log("compiled", path.split("/")[-1], bp.get_editor_property("status") if bp else "no asset")

def obj(path, widget):
    return unreal.load_object(None, path + "." + path.split("/")[-1] + ":WidgetTree." + widget)

def names(path):
    return set(str(h.widget_name) for h in WS.get_hierarchy(path))

def ensure_wbp(name, parent_cls):
    p = R + "/" + name
    if not EAL.does_asset_exist(p):
        fac = unreal.WidgetBlueprintFactory()
        fac.set_editor_property("parent_class", parent_cls)
        AT.create_asset(name, R, unreal.WidgetBlueprint, fac)
        log("CREATED:", p)
    if not WS.get_hierarchy(p):
        WS.add_component(p, "Overlay", "Root", "", False)
    return p

def ensure_child(p, ctype, cname, parent, is_var=True):
    if cname not in names(p):
        r = WS.add_component(p, ctype, cname, parent, is_var)
        log("  +", cname, ctype, "->", parent, r.success if hasattr(r, "success") else r)

def align(w, h, v, pad=None):
    w.slot.set_horizontal_alignment(h)
    w.slot.set_vertical_alignment(v)
    if pad is not None:
        w.slot.set_padding(pad)

def root_slot(name, slot_tag, pos, size):
    pr = R + "/WBP_HudRoot"
    ensure_child(pr, "HudSlot", name, "Canvas", False)
    s = obj(pr, name)
    s.set_editor_property("slot_tag", tag(slot_tag))
    a = unreal.Anchors()
    a.minimum = unreal.Vector2D(0.5, 0.5)
    a.maximum = unreal.Vector2D(0.5, 0.5)
    s.slot.set_anchors(a)
    s.slot.set_alignment(unreal.Vector2D(0.5, 0.0))
    s.slot.set_position(unreal.Vector2D(*pos))
    s.slot.set_size(unreal.Vector2D(*size))
    s.slot.set_auto_size(False)
    log("  slot", name, slot_tag, pos, size)

def bind_layout(slot_tag, wbp):
    da = unreal.load_asset(R + "/DA_HudLayout")
    bindings = list(da.get_editor_property("slots"))
    cls = EAL.load_blueprint_class(wbp)
    for i, b in enumerate(bindings):
        if slot_tag in b.get_editor_property("slot_tag").export_text():
            b.set_editor_property("widget_class", cls)
            bindings[i] = b
            break
    else:
        nb = unreal.HudSlotBinding()
        nb.set_editor_property("slot_tag", tag(slot_tag))
        nb.set_editor_property("widget_class", cls)
        bindings.append(nb)
    da.set_editor_property("slots", bindings)
    log("  layout", slot_tag, "->", wbp.split("/")[-1], EAL.save_asset(R + "/DA_HudLayout", False))

# ---------------------------------------------------------------- materials

def make_screen_edge():
    p = FX + "/M_ScreenEdge"
    if EAL.does_asset_exist(p):
        log("EXISTS", p)
        return p
    m = AT.create_asset("M_ScreenEdge", FX, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    m.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)

    scene = MEL.create_material_expression(m, unreal.MaterialExpressionSceneTexture, -700, -200)
    scene.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    uv = MEL.create_material_expression(m, unreal.MaterialExpressionScreenPosition, -700, 0)
    col = MEL.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -700, 150)
    col.set_editor_property("parameter_name", "Color")
    col.set_editor_property("default_value", unreal.LinearColor(0.2, 1.0, 0.4, 1.0))
    inten = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -700, 350)
    inten.set_editor_property("parameter_name", "Intensity")
    inten.set_editor_property("default_value", 0.0)

    c = MEL.create_material_expression(m, unreal.MaterialExpressionCustom, -300, 0)
    c.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    ins = []
    for n in ("Scene", "UV", "Color", "Intensity"):
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    c.set_editor_property("inputs", ins)
    # A glow that lives on the border of the screen and is gone by the middle: a blend of a box
    # distance and a round one, so it follows the corners without squaring off.
    c.set_editor_property("code",
        "float2 p = (UV - 0.5) * 2.0;\n"
        "float box = max(abs(p.x), abs(p.y));\n"
        "float round = length(p) * 0.7071;\n"
        "float e = lerp(box, round, 0.5);\n"
        "float m = saturate((e - 0.55) / 0.45);\n"
        "m = m * m * (3.0 - 2.0 * m);\n"
        "return Scene.rgb + Color.rgb * (m * Intensity * 0.9);")
    log("  connect", MEL.connect_material_expressions(scene, "Color", c, "Scene"),
        MEL.connect_material_expressions(uv, "ViewportUV", c, "UV"),
        MEL.connect_material_expressions(col, "", c, "Color"),
        MEL.connect_material_expressions(inten, "", c, "Intensity"),
        MEL.connect_material_property(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR))
    MEL.recompile_material(m)
    log("CREATED:", p, EAL.save_asset(p, False))
    return p

def make_cloak_material():
    p = FX + "/M_CloakMelee"
    if EAL.does_asset_exist(p):
        log("EXISTS", p)
        return p
    m = AT.create_asset("M_CloakMelee", FX, unreal.Material, unreal.MaterialFactoryNew())
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("used_with_skeletal_mesh", True)
    tint = MEL.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -500, -100)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.45, 0.75, 1.0, 1.0))
    fres = MEL.create_material_expression(m, unreal.MaterialExpressionFresnel, -500, 150)
    fres.set_editor_property("exponent", 2.0)
    fres.set_editor_property("base_reflect_fraction", 0.12)
    log("  connect", MEL.connect_material_property(tint, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR),
        MEL.connect_material_property(fres, "", unreal.MaterialProperty.MP_OPACITY))
    MEL.recompile_material(m)
    log("CREATED:", p, EAL.save_asset(p, False))
    return p

# ---------------------------------------------------------------- main

def main():
    H = unreal.HorizontalAlignment
    V = unreal.VerticalAlignment

    edge = make_screen_edge()
    cloak = make_cloak_material()

    unreal.GameplayTagService.add_tags(["HUD.Slot.Notice", "HUD.Slot.Hold"],
        "HUD registry slot, see Docs/Abilities_Melee_Design_2026-10-06.md")

    # ---------------- WBP_Notice ----------------
    pn = ensure_wbp("WBP_Notice", unreal.HudNoticeWidget)
    ensure_child(pn, "VerticalBox", "Lines", "Root", True)
    align(obj(pn, "Lines"), H.H_ALIGN_CENTER, V.V_ALIGN_TOP)
    compile_bp(pn)
    cdo = unreal.get_default_object(EAL.load_blueprint_class(pn))
    cdo.set_editor_property("default_color_tag", tag("Palette.HUD.Text"))
    cdo.set_editor_property("notice_font_size", 18)
    compile_bp(pn)
    log("saved", EAL.save_asset(pn, False))

    # ---------------- WBP_Hold ----------------
    ph = ensure_wbp("WBP_Hold", unreal.HudHoldWidget)
    ensure_child(ph, "HorizontalBox", "Row", "Root", False)
    ensure_child(ph, "SizeBox", "KeyCell", "Row", False)
    ensure_child(ph, "Overlay", "KeyStack", "KeyCell", False)
    ensure_child(ph, "WBP_HudPlate", "Plate", "KeyStack", True)
    ensure_child(ph, "TextBlock", "KeyText", "KeyStack", True)
    ensure_child(ph, "VerticalBox", "Column", "Row", False)
    ensure_child(ph, "TextBlock", "LabelText", "Column", True)
    ensure_child(ph, "SizeBox", "BarSize", "Column", False)
    ensure_child(ph, "WBP_HudBar", "ProgressBar", "BarSize", True)

    align(obj(ph, "Row"), H.H_ALIGN_CENTER, V.V_ALIGN_TOP)
    kc = obj(ph, "KeyCell")
    kc.set_width_override(44.0)
    kc.set_height_override(44.0)
    kc.slot.set_vertical_alignment(V.V_ALIGN_CENTER)
    kc.slot.set_padding(unreal.Margin(0.0, 0.0, 10.0, 0.0))
    plate = obj(ph, "Plate")
    plate.set_editor_property("fill_color_tag", tag("Palette.HUD.Plate"))
    plate.set_editor_property("lean", 0.0)
    plate.set_editor_property("radius", 8.0)
    align(plate, H.H_ALIGN_FILL, V.V_ALIGN_FILL)
    WS.set_property(ph, "KeyText", "Text", "V")
    WS.set_property(ph, "KeyText", "Font.Size", "20")
    kt = obj(ph, "KeyText")
    kt.set_min_desired_width(30.0)
    kt.set_editor_property("justification", unreal.TextJustify.CENTER)
    align(kt, H.H_ALIGN_CENTER, V.V_ALIGN_CENTER)

    obj(ph, "Column").slot.set_vertical_alignment(V.V_ALIGN_CENTER)
    WS.set_property(ph, "LabelText", "Text", "Hold")
    WS.set_property(ph, "LabelText", "Font.Size", "16")
    lt = obj(ph, "LabelText")
    lt.set_min_desired_width(170.0)
    lt.set_editor_property("shadow_offset", unreal.Vector2D(0.0, 1.0))
    lt.set_editor_property("shadow_color_and_opacity", unreal.LinearColor(0.0, 0.0, 0.0, 0.8))
    bs = obj(ph, "BarSize")
    bs.set_width_override(170.0)
    bs.set_height_override(10.0)
    bs.slot.set_padding(unreal.Margin(0.0, 4.0, 0.0, 0.0))
    bs.slot.set_horizontal_alignment(H.H_ALIGN_LEFT)
    bar = obj(ph, "ProgressBar")
    bar.set_editor_property("fill_color_tag", tag("Palette.HUD.Text"))
    bar.set_editor_property("track_color_tag", tag("Palette.HUD.Plate"))
    bar.set_editor_property("lag_color_tag", tag("Palette.HUD.Text"))
    bar.set_editor_property("lean", 0.0)
    bar.set_editor_property("radius", 2.0)
    bar.set_editor_property("segments", 6)
    compile_bp(ph)
    log("saved", EAL.save_asset(ph, False))

    # ---------------- slots under the crosshair, and what goes in them ----------------
    root_slot("Slot_Hold", "HUD.Slot.Hold", (0.0, 70.0), (360.0, 56.0))
    root_slot("Slot_Notice", "HUD.Slot.Notice", (0.0, 135.0), (500.0, 90.0))
    compile_bp(R + "/WBP_HudRoot")
    log("root saved", EAL.save_asset(R + "/WBP_HudRoot", False))
    bind_layout("HUD.Slot.Hold", ph)
    bind_layout("HUD.Slot.Notice", pn)

    # ---------------- kill bonuses: who says what ----------------
    edge_mat = unreal.load_asset(edge)
    for name, text, colour in (
            ("DA_Upgrade_Kill_Reload", "FAST RELOAD", None),
            ("DA_Upgrade_Kill_Regen", "", unreal.LinearColor(0.2, 1.0, 0.4, 1.0)),
            ("DA_Upgrade_Kill_Cooldown", "", unreal.LinearColor(0.3, 0.6, 1.0, 1.0))):
        p = UP + "/Passive/" + name
        d = unreal.load_asset(p)
        d.set_editor_property("notice_text", text)
        d.set_editor_property("notice_color_tag", tag("Palette.HUD.Text"))
        d.set_editor_property("screen_edge_material", edge_mat if colour else None)
        if colour:
            d.set_editor_property("screen_edge_color", colour)
        log("MODIFIED", name, EAL.save_asset(p, False))

    # ---------------- cloak: the see-through melee mesh ----------------
    p = UP + "/Melee/DA_Upgrade_MeleeCloak"
    d = unreal.load_asset(p)
    d.set_editor_property("cloak_melee_material", unreal.load_asset(cloak))
    log("MODIFIED DA_Upgrade_MeleeCloak hold_time", d.get_editor_property("hold_time"), EAL.save_asset(p, False))

    # ---------------- thrown abilities: no pin-pull stage ----------------
    # The wind-up montage is left out, so a press goes straight to the aim pose (hold) or to the
    # throw (tap). The blend into each is lengthened a little to carry what the wind-up carried.
    for name in ("DA_Ability_HealGrenade", "DA_Ability_SmokeScreen", "DA_Ability_NitroGate"):
        p = AB + "/" + name
        d = unreal.load_asset(p)
        d.set_editor_property("aim_start_montage", None)
        log("MODIFIED", name, "no wind-up", EAL.save_asset(p, False))
    for n in ("AM_Throw_Loop", "AM_Throw_End"):
        p = AB + "/Throw/" + n
        m = unreal.load_asset(p)
        b = m.get_editor_property("blend_in")
        b.set_editor_property("blend_time", 0.15)
        m.set_editor_property("blend_in", b)
        log("MODIFIED", n, "blend in 0.15", EAL.save_asset(p, False))
