# Turret feed menu: WBP_TurretFeed(Entry), the HUD slot, the layout row, Escape in IMC_BuildMenu.
# Run AFTER the rebuild that brings UTurretFeedWidget / UTurretFeedEntryWidget and the Feeding mode.
# Idempotent on creation; configuration is rewritten each run. Same shapes as ed3_widgets.py.
import unreal, json

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
WS = unreal.WidgetService
IS = unreal.InputService
R = "/Game/Variant_Shooter/UI/Widgets/HUD/Registry"
INP = "/Game/Variant_Shooter/Input"
ACT = INP + "/Actions"
SOUND = "/Game/InfimaGames/ArtCore/Audio/_Common/S_IG_Demo_Buzzer"

def log(*a):
    print(" ".join(str(x) for x in a))

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
    return p

def ensure_child(p, ctype, cname, parent, is_var=True):
    if cname not in names(p):
        r = WS.add_component(p, ctype, cname, parent, is_var)
        log("  +", cname, ctype, "->", parent, r.success if hasattr(r, "success") else r)

def ensure_root(p):
    h = WS.get_hierarchy(p)
    if not h:
        r = WS.add_component(p, "Overlay", "Root", "", False)
        log("  root Overlay", r.success if hasattr(r, "success") else r)
        return "Root"
    for e in h:
        if e.is_root_widget:
            return str(e.widget_name)
    return "Root"

def fill_overlay(w):
    w.slot.set_editor_property("horizontal_alignment", unreal.HorizontalAlignment.H_ALIGN_FILL)
    w.slot.set_editor_property("vertical_alignment", unreal.VerticalAlignment.V_ALIGN_FILL)

def text(p, name, value, size, min_width=0.0, justify=None):
    WS.set_property(p, name, "Text", value)
    WS.set_property(p, name, "Font.Size", str(size))
    t = obj(p, name)
    if min_width > 0:
        t.set_min_desired_width(min_width)
    if justify is not None:
        t.set_editor_property("justification", justify)

def plate(p, name, mirror=True):
    pl = obj(p, name)
    pl.set_editor_property("fill_color_tag", tag("Palette.HUD.Plate"))
    pl.set_editor_property("lean", 30.0)
    pl.set_editor_property("radius", 8.0)
    pl.set_editor_property("mirror", mirror)
    fill_overlay(pl)

def main():
    try:
        log("tags:", unreal.GameplayTagService.add_tags(["HUD.Slot.TurretFeed"], "Turret feed menu, see Docs/Turret_Buildable_Plan_2026-09-13.md"))
    except Exception as e:
        log("tags failed:", e)

    # ---------------- WBP_TurretFeedEntry ----------------
    pe = ensure_wbp("WBP_TurretFeedEntry", unreal.TurretFeedEntryWidget)
    root = ensure_root(pe)
    ensure_child(pe, "WBP_HudPlate", "Plate", root, True)
    ensure_child(pe, "HorizontalBox", "Row", root, False)
    ensure_child(pe, "TextBlock", "KeyText", "Row", True)
    ensure_child(pe, "SizeBox", "IconSize", "Row", False)
    ensure_child(pe, "Image", "Icon", "IconSize", True)
    for n in ("NameText", "AmmoText", "StatusText"):
        ensure_child(pe, "TextBlock", n, "Row", True)
    plate(pe, "Plate")
    row = obj(pe, "Row")
    row.slot.set_editor_property("padding", unreal.Margin(34.0, 8.0, 26.0, 8.0))
    fill_overlay(row)
    text(pe, "KeyText", "1", 20, 26.0)
    text(pe, "NameText", "Rifle", 20)
    text(pe, "AmmoText", "30 / 30", 16, 70.0, unreal.TextJustify.RIGHT)
    text(pe, "StatusText", "vice 1", 14, 110.0, unreal.TextJustify.RIGHT)
    isz = obj(pe, "IconSize")
    isz.set_width_override(56.0)
    isz.set_height_override(28.0)
    isz.slot.set_padding(unreal.Margin(0.0, 0.0, 10.0, 0.0))
    isz.slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
    ic = obj(pe, "Icon")
    ic.slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
    ic.slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)
    for n, fill in (("KeyText", False), ("NameText", True), ("AmmoText", False), ("StatusText", False)):
        s = obj(pe, n).slot
        s.set_padding(unreal.Margin(0.0, 0.0, 10.0, 0.0))
        s.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
        if fill:
            s.set_size(unreal.SlateChildSize(1.0, unreal.SlateSizeRule.FILL))
    compile_bp(pe)
    EAL.save_asset(pe)

    # ---------------- WBP_TurretFeed ----------------
    pm = ensure_wbp("WBP_TurretFeed", unreal.TurretFeedWidget)
    root = ensure_root(pm)
    ensure_child(pm, "VerticalBox", "Column", root, False)
    ensure_child(pm, "TextBlock", "TitleText", "Column", True)
    ensure_child(pm, "VerticalBox", "EntryPanel", "Column", True)
    fill_overlay(obj(pm, "Column"))
    text(pm, "TitleText", "Turret, level 1: 1 of 1 vices free", 14)
    obj(pm, "TitleText").slot.set_padding(unreal.Margin(34.0, 0.0, 0.0, 6.0))
    cls = EAL.load_blueprint_class(pm)
    cdo = unreal.get_default_object(cls)
    cdo.set_editor_property("entry_class", EAL.load_blueprint_class(pe))
    cdo.set_editor_property("first_key_number", 1)
    cdo.set_editor_property("available_color_tag", tag("Palette.HUD.Text"))
    cdo.set_editor_property("unavailable_color_tag", tag("Palette.HUD.TextDim"))
    cdo.set_editor_property("plate_color_tag", tag("Palette.HUD.Plate"))
    cdo.set_editor_property("refused_flash_tag", tag("Palette.HUD.Flash.Spend"))
    if EAL.does_asset_exist(SOUND):
        cdo.set_editor_property("refused_sound", unreal.load_asset(SOUND))
    compile_bp(pm)
    EAL.save_asset(pm)

    # ---------------- slot in WBP_HudRoot (same place as the build menu: never up together) ----------------
    pr = R + "/WBP_HudRoot"
    ensure_child(pr, "HudSlot", "Slot_TurretFeed", "Canvas", False)
    s = obj(pr, "Slot_TurretFeed")
    s.set_editor_property("slot_tag", tag("HUD.Slot.TurretFeed"))
    a = unreal.Anchors()
    a.minimum = unreal.Vector2D(0.0, 0.5)
    a.maximum = unreal.Vector2D(0.0, 0.5)
    s.slot.set_anchors(a)
    s.slot.set_position(unreal.Vector2D(48.0, 150.0))
    s.slot.set_size(unreal.Vector2D(420.0, 220.0))
    s.slot.set_alignment(unreal.Vector2D(0.0, 0.5))
    s.slot.set_auto_size(False)
    compile_bp(pr)
    EAL.save_asset(pr)

    # ---------------- DA_HudLayout ----------------
    da = unreal.load_asset(R + "/DA_HudLayout")
    bindings = list(da.get_editor_property("slots"))
    have = set(b.get_editor_property("slot_tag").export_text() for b in bindings)
    if '(TagName="HUD.Slot.TurretFeed")' in have:
        log("  layout has HUD.Slot.TurretFeed")
    else:
        b = unreal.HudSlotBinding()
        b.set_editor_property("slot_tag", tag("HUD.Slot.TurretFeed"))
        b.set_editor_property("widget_class", EAL.load_blueprint_class(pm))
        b.set_editor_property("start_hidden", False)
        bindings.append(b)
        da.set_editor_property("slots", bindings)
        log("  layout + HUD.Slot.TurretFeed")
    EAL.save_asset(R + "/DA_HudLayout")

    # ---------------- Escape closes the menus too ----------------
    imc_menu = INP + "/IMC_BuildMenu"
    ia_cancel = ACT + "/IA_BuildCancel"
    if IS.key_mapping_exists(imc_menu, ia_cancel):
        log("kept Escape in IMC_BuildMenu")
    else:
        log("MAPPED: IA_BuildCancel -> Escape in IMC_BuildMenu", IS.add_key_mapping(imc_menu, ia_cancel, "Escape"))
        EAL.save_asset(imc_menu)
    log("DONE")

main()
