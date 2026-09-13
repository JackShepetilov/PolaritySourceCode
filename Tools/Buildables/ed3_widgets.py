# Buildables HUD: the build menu and the building status panel, two slots in WBP_HudRoot,
# two bindings in DA_HudLayout. Idempotent on creation; configuration is rewritten each run.
import unreal, json

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
WS = unreal.WidgetService
R = "/Game/Variant_Shooter/UI/Widgets/HUD/Registry"

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
        try:
            fac.set_editor_property("root_widget_class", unreal.Overlay)
        except Exception as e:
            log("no root_widget_class on factory:", e)
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
    # ---------------- WBP_BuildMenuEntry ----------------
    pe = ensure_wbp("WBP_BuildMenuEntry", unreal.BuildMenuEntryWidget)
    root = ensure_root(pe)
    ensure_child(pe, "WBP_HudPlate", "Plate", root, True)
    ensure_child(pe, "HorizontalBox", "Row", root, False)
    for n in ("KeyText", "NameText", "CostText", "StatusText"):
        ensure_child(pe, "TextBlock", n, "Row", True)
    plate(pe, "Plate")
    row = obj(pe, "Row")
    row.slot.set_editor_property("padding", unreal.Margin(34.0, 8.0, 26.0, 8.0))
    fill_overlay(row)
    text(pe, "KeyText", "1", 20, 26.0)
    text(pe, "NameText", "Turret", 20)
    text(pe, "CostText", "130", 18, 60.0, unreal.TextJustify.RIGHT)
    text(pe, "StatusText", "", 14, 70.0, unreal.TextJustify.RIGHT)
    for n, fill in (("KeyText", False), ("NameText", True), ("CostText", False), ("StatusText", False)):
        s = obj(pe, n).slot
        s.set_padding(unreal.Margin(0.0, 0.0, 10.0, 0.0))
        s.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
        if fill:
            s.set_size(unreal.SlateChildSize(1.0, unreal.SlateSizeRule.FILL))
    compile_bp(pe)
    EAL.save_asset(pe)

    # ---------------- WBP_BuildMenu ----------------
    pm = ensure_wbp("WBP_BuildMenu", unreal.BuildMenuWidget)
    root = ensure_root(pm)
    ensure_child(pm, "VerticalBox", "EntryPanel", root, True)
    fill_overlay(obj(pm, "EntryPanel"))
    cls = EAL.load_blueprint_class(pm)
    cdo = unreal.get_default_object(cls)
    cdo.set_editor_property("entry_class", EAL.load_blueprint_class(pe))
    cdo.set_editor_property("first_key_number", 1)
    cdo.set_editor_property("available_color_tag", tag("Palette.HUD.Text"))
    cdo.set_editor_property("unaffordable_color_tag", tag("Palette.HUD.TextDim"))
    cdo.set_editor_property("built_color_tag", tag("Palette.HUD.Built"))
    cdo.set_editor_property("plate_color_tag", tag("Palette.HUD.Plate"))
    cdo.set_editor_property("plate_selected_color_tag", tag("Palette.HUD.PlateSelected"))
    compile_bp(pm)
    EAL.save_asset(pm)

    # ---------------- WBP_BuildableStatusEntry ----------------
    ps = ensure_wbp("WBP_BuildableStatusEntry", unreal.BuildableStatusEntryWidget)
    root = ensure_root(ps)
    ensure_child(ps, "WBP_HudPlate", "Plate", root, True)
    ensure_child(ps, "VerticalBox", "Column", root, False)
    ensure_child(ps, "HorizontalBox", "TitleRow", "Column", False)
    ensure_child(ps, "TextBlock", "NameText", "TitleRow", True)
    ensure_child(ps, "TextBlock", "LevelText", "TitleRow", True)
    ensure_child(ps, "TextBlock", "HealthText", "TitleRow", True)
    ensure_child(ps, "SizeBox", "BarSize", "Column", False)
    ensure_child(ps, "WBP_HudBar", "HealthBar", "BarSize", True)
    ensure_child(ps, "TextBlock", "StatusText", "Column", True)
    plate(ps, "Plate")
    col = obj(ps, "Column")
    col.slot.set_editor_property("padding", unreal.Margin(34.0, 8.0, 26.0, 8.0))
    fill_overlay(col)
    text(ps, "NameText", "Turret", 16)
    text(ps, "LevelText", "1", 16, 20.0, unreal.TextJustify.CENTER)
    text(ps, "HealthText", "150 / 150", 13, 80.0, unreal.TextJustify.RIGHT)
    text(ps, "StatusText", "", 12)
    obj(ps, "NameText").slot.set_size(unreal.SlateChildSize(1.0, unreal.SlateSizeRule.FILL))
    for n in ("LevelText", "HealthText"):
        obj(ps, n).slot.set_padding(unreal.Margin(8.0, 0.0, 0.0, 0.0))
    for n in ("NameText", "LevelText", "HealthText"):
        obj(ps, n).slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
    bs = obj(ps, "BarSize")
    bs.set_height_override(14.0)
    bs.slot.set_padding(unreal.Margin(0.0, 6.0, 0.0, 4.0))
    bar = obj(ps, "HealthBar")
    bar.set_editor_property("fill_color_tag", tag("Palette.HUD.Health"))
    bar.set_editor_property("track_color_tag", tag("Palette.HUD.Track"))
    bar.set_editor_property("lag_color_tag", tag("Palette.HUD.HealthGhost"))
    bar.set_editor_property("lean", 30.0)
    bar.set_editor_property("radius", 6.0)
    bar.set_editor_property("mirror", True)
    bar.set_editor_property("segments", 4)
    compile_bp(ps)
    EAL.save_asset(ps)

    # ---------------- WBP_BuildableStatus ----------------
    pp = ensure_wbp("WBP_BuildableStatus", unreal.BuildableStatusWidget)
    root = ensure_root(pp)
    ensure_child(pp, "VerticalBox", "EntryPanel", root, True)
    fill_overlay(obj(pp, "EntryPanel"))
    cls = EAL.load_blueprint_class(pp)
    cdo = unreal.get_default_object(cls)
    cdo.set_editor_property("entry_class", EAL.load_blueprint_class(ps))
    cdo.set_editor_property("text_color_tag", tag("Palette.HUD.Text"))
    cdo.set_editor_property("dim_text_color_tag", tag("Palette.HUD.TextDim"))
    compile_bp(pp)
    EAL.save_asset(pp)

    # ---------------- slots in WBP_HudRoot ----------------
    pr = R + "/WBP_HudRoot"
    slots = {
        # name: (tag, anchor, position, size, alignment)
        "Slot_BuildMenu":  ("HUD.Slot.BuildMenu",  (0.0, 0.5), (48.0, 150.0), (360.0, 190.0), (0.0, 0.5)),
        "Slot_Buildables": ("HUD.Slot.Buildables", (0.0, 0.0), (48.0, 290.0), (300.0, 240.0), (0.0, 0.0)),
    }
    for n, (t, anc, pos, size, al) in slots.items():
        ensure_child(pr, "HudSlot", n, "Canvas", False)
        s = obj(pr, n)
        s.set_editor_property("slot_tag", tag(t))
        a = unreal.Anchors()
        a.minimum = unreal.Vector2D(*anc)
        a.maximum = unreal.Vector2D(*anc)
        s.slot.set_anchors(a)
        s.slot.set_position(unreal.Vector2D(*pos))
        s.slot.set_size(unreal.Vector2D(*size))
        s.slot.set_alignment(unreal.Vector2D(*al))
        s.slot.set_auto_size(False)
        log("  slot", n, t, pos, size)
    compile_bp(pr)
    EAL.save_asset(pr)

    # ---------------- DA_HudLayout ----------------
    da = unreal.load_asset(R + "/DA_HudLayout")
    bindings = list(da.get_editor_property("slots"))
    have = set(b.get_editor_property("slot_tag").export_text() for b in bindings)
    for t, wbp in (("HUD.Slot.BuildMenu", pm), ("HUD.Slot.Buildables", pp)):
        if '(TagName="%s")' % t in have:
            log("  layout has", t)
            continue
        b = unreal.HudSlotBinding()
        b.set_editor_property("slot_tag", tag(t))
        b.set_editor_property("widget_class", EAL.load_blueprint_class(wbp))
        b.set_editor_property("start_hidden", False)
        bindings.append(b)
        log("  layout +", t, wbp.split("/")[-1])
    da.set_editor_property("slots", bindings)
    EAL.save_asset(R + "/DA_HudLayout")
    log("DONE")

main()
