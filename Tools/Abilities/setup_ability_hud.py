# The ability cell of the HUD: WBP_Ability (UHudAbilityWidget) on the house plate, placed in
# HUD.Slot.Abilities. Layout of the cell: Docs/HUD/HUD_Layout.html (.ab). Idempotent on creation;
# configuration is rewritten each run. Run from the editor: main().
import unreal, json

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
WS = unreal.WidgetService
R = "/Game/Variant_Shooter/UI/Widgets/HUD/Registry"
CELL = 96.0

def log(*a):
    print(" ".join(str(x) for x in a))
    unreal.log("[ABILITY_HUD] " + " ".join(str(x) for x in a))

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

def ensure_child(p, ctype, cname, parent, is_var=True):
    if cname not in names(p):
        r = WS.add_component(p, ctype, cname, parent, is_var)
        log("  +", cname, ctype, "->", parent, r.success if hasattr(r, "success") else r)

def align(w, h, v, pad=None):
    w.slot.set_horizontal_alignment(h)
    w.slot.set_vertical_alignment(v)
    if pad is not None:
        w.slot.set_padding(pad)

def main():
    H = unreal.HorizontalAlignment
    V = unreal.VerticalAlignment

    # ---------------- WBP_Ability ----------------
    p = R + "/WBP_Ability"
    if not EAL.does_asset_exist(p):
        fac = unreal.WidgetBlueprintFactory()
        fac.set_editor_property("parent_class", unreal.HudAbilityWidget)
        AT.create_asset("WBP_Ability", R, unreal.WidgetBlueprint, fac)
        log("CREATED:", p)
    if not WS.get_hierarchy(p):
        WS.add_component(p, "Overlay", "Root", "", False)

    ensure_child(p, "SizeBox", "Cell", "Root", False)
    ensure_child(p, "Overlay", "CellStack", "Cell", False)
    ensure_child(p, "WBP_HudPlate", "Plate", "CellStack", True)
    ensure_child(p, "Image", "CooldownFill", "CellStack", True)
    ensure_child(p, "Image", "AbilityIcon", "CellStack", True)
    ensure_child(p, "TextBlock", "CooldownText", "CellStack", True)
    ensure_child(p, "TextBlock", "KeyText", "CellStack", True)

    cell = obj(p, "Cell")
    cell.set_width_override(CELL)
    cell.set_height_override(CELL)
    align(cell, H.H_ALIGN_CENTER, V.V_ALIGN_BOTTOM)

    # A square cell: the plate keeps the house radius and drops the lean, so the fill sits inside it.
    plate = obj(p, "Plate")
    plate.set_editor_property("fill_color_tag", tag("Palette.HUD.Plate"))
    plate.set_editor_property("lean", 0.0)
    plate.set_editor_property("radius", 8.0)
    align(plate, H.H_ALIGN_FILL, V.V_ALIGN_FILL)

    # The fill: a flat tint over the plate, grown from the bottom by the widget's code.
    fill = obj(p, "CooldownFill")
    fill.set_color_and_opacity(unreal.LinearColor(0.35, 0.69, 0.88, 0.45))
    align(fill, H.H_ALIGN_FILL, V.V_ALIGN_FILL, unreal.Margin(4.0, 4.0, 4.0, 4.0))

    icon = obj(p, "AbilityIcon")
    b = icon.get_editor_property("brush")
    sz = b.get_editor_property("image_size")
    sz.import_text("(X=52.000000,Y=52.000000)")
    b.set_editor_property("image_size", sz)
    icon.set_editor_property("brush", b)
    align(icon, H.H_ALIGN_CENTER, V.V_ALIGN_TOP, unreal.Margin(0.0, 10.0, 0.0, 0.0))

    WS.set_property(p, "CooldownText", "Text", "")
    WS.set_property(p, "CooldownText", "Font.Size", "30")
    cd = obj(p, "CooldownText")
    cd.set_min_desired_width(56.0)
    cd.set_editor_property("justification", unreal.TextJustify.CENTER)
    cd.set_editor_property("shadow_offset", unreal.Vector2D(0.0, 1.0))
    cd.set_editor_property("shadow_color_and_opacity", unreal.LinearColor(0.0, 0.0, 0.0, 0.8))
    align(cd, H.H_ALIGN_CENTER, V.V_ALIGN_CENTER, unreal.Margin(0.0, 0.0, 0.0, 12.0))

    WS.set_property(p, "KeyText", "Text", "Q")
    WS.set_property(p, "KeyText", "Font.Size", "11")
    key = obj(p, "KeyText")
    key.set_min_desired_width(56.0)
    key.set_editor_property("justification", unreal.TextJustify.CENTER)
    align(key, H.H_ALIGN_CENTER, V.V_ALIGN_BOTTOM, unreal.Margin(0.0, 0.0, 0.0, 7.0))

    compile_bp(p)
    cdo = unreal.get_default_object(EAL.load_blueprint_class(p))
    cdo.set_editor_property("number_color_tag", tag("Palette.HUD.Text"))
    cdo.set_editor_property("caption_color_tag", tag("Palette.HUD.TextDim"))
    cdo.set_editor_property("ready_flash_tag", tag("Palette.HUD.Flash.Gain"))
    compile_bp(p)
    log("saved", EAL.save_asset(p, False))

    # ---------------- the slot in WBP_HudRoot: bottom centre, as in the layout ----------------
    pr = R + "/WBP_HudRoot"
    for h in WS.get_hierarchy(pr):
        if str(h.widget_class) == "HudSlot":
            s = obj(pr, str(h.widget_name))
            log("  root before", h.widget_name, s.slot.get_editor_property("layout_data").export_text())
    s = obj(pr, "Slot_Abilities")
    a = unreal.Anchors()
    a.minimum = unreal.Vector2D(0.5, 1.0)
    a.maximum = unreal.Vector2D(0.5, 1.0)
    s.slot.set_anchors(a)
    s.slot.set_alignment(unreal.Vector2D(0.5, 1.0))
    s.slot.set_position(unreal.Vector2D(0.0, -48.0))
    s.slot.set_size(unreal.Vector2D(600.0, CELL))
    s.slot.set_auto_size(False)
    compile_bp(pr)
    log("root saved", EAL.save_asset(pr, False))

    # ---------------- DA_HudLayout: the cell replaces the old bar in the slot ----------------
    da = unreal.load_asset(R + "/DA_HudLayout")
    bindings = list(da.get_editor_property("slots"))
    done = False
    for i, bnd in enumerate(bindings):
        if 'HUD.Slot.Abilities' in bnd.get_editor_property("slot_tag").export_text():
            log("  layout was", bnd.export_text())
            bnd.set_editor_property("widget_class", EAL.load_blueprint_class(p))
            bindings[i] = bnd
            done = True
    if not done:
        nb = unreal.HudSlotBinding()
        nb.set_editor_property("slot_tag", tag("HUD.Slot.Abilities"))
        nb.set_editor_property("widget_class", EAL.load_blueprint_class(p))
        bindings.append(nb)
    da.set_editor_property("slots", bindings)
    log("layout saved", EAL.save_asset(R + "/DA_HudLayout", False))
