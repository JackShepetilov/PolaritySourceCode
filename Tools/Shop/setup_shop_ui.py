# The widgets for the shop, the third weapon slot and the bag's scroll mark (2026-10-08).
#   WBP_InventoryBar     third name segment (Slot3*), copied from the second
#   WBP_InventoryScreen  third weapon panel (WeaponPanel3, Panel3*), copied from the second; GridScroll
#   WBP_ShopScreen       new, parent UShopScreenWidget; set on the player controller blueprint
# Idempotent: a widget that exists is left as the author has it. Run from the editor: main().
import unreal, json

L = r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/Shop/widget_lib.py"
g = {"__name__": "wl"}
exec(compile(open(L, encoding="utf-8").read(), L, "exec"), g)
log, tag, obj, names = g["log"], g["tag"], g["obj"], g["names"]
ensure_child, copy_look, compile_bp = g["ensure_child"], g["copy_look"], g["compile_bp"]
EAL, AT, WS = g["EAL"], g["AT"], g["WS"]

INV = "/Game/Variant_Shooter/UI/Widgets/HUD/Inventory"
REG = "/Game/Variant_Shooter/UI/Widgets/HUD/Registry"
SHOP_DIR = "/Game/Variant_Shooter/UI/Widgets/HUD/Shop"
H = unreal.HorizontalAlignment
V = unreal.VerticalAlignment


def step_bar():
    p = INV + "/WBP_InventoryBar"
    made = False
    for ctype, name, parent, src in (
            ("Overlay", "Slot3Segment", "NameStrip", "Slot2Segment"),
            ("Image", "Slot3NameBg", "Slot3Segment", "Slot2NameBg"),
            ("TextBlock", "Slot3NameText", "Slot3Segment", "Slot2NameText"),
            ("HorizontalBox", "ThirdWeaponAttachments", "Slot3Segment", "SecondWeaponAttachments")):
        if ensure_child(p, ctype, name, parent, True):
            copy_look(p, src, name)
            made = True
    if made:
        compile_bp(p)
        log("bar saved", EAL.save_asset(p, False))


def step_screen():
    p = INV + "/WBP_InventoryScreen"
    made = False
    for ctype, name, parent, src, text in (
            ("SizeBox", "WeaponPanel3", "WeaponPanels", "WeaponPanel2", False),
            ("Overlay", "Panel3Body", "WeaponPanel3", "Panel2Body", False),
            ("Image", "Panel3Bg", "Panel3Body", "Panel2Bg", False),
            ("VerticalBox", "Panel3Stack", "Panel3Body", "Panel2Stack", False),
            ("TextBlock", "Panel3Header", "Panel3Stack", "Panel2Header", True),
            ("Spacer", "Panel3Spacer", "Panel3Stack", "Spacer2", False),
            ("TextBlock", "Panel3NameText", "Panel3Stack", "Panel2NameText", False),
            ("HorizontalBox", "Panel3MidRow", "Panel3Stack", "Panel2MidRow", False),
            ("HorizontalBox", "ThirdWeaponAttachments", "Panel3MidRow", "SecondWeaponAttachments", False),
            ("Image", "Panel3Icon", "Panel3Body", "Panel2Icon", False),
            ("VerticalBox", "Panel3AmmoCol", "Panel3Body", "Panel2AmmoCol", False),
            ("TextBlock", "Panel3AmmoText", "Panel3AmmoCol", "Panel2AmmoText", False),
            ("TextBlock", "Panel3ReserveText", "Panel3AmmoCol", "Panel2ReserveText", False)):
        if ensure_child(p, ctype, name, parent, True):
            copy_look(p, src, name, with_text=text)
            made = True
    if made:
        # The header reads "2 WEAPON" on the panel it was copied from.
        head = obj(p, "Panel3Header")
        old = str(obj(p, "Panel2Header").get_editor_property("text"))
        head.set_editor_property("text", unreal.Text(old.replace("2", "3")))
        log("  header", old, "->", head.get_editor_property("text"))

    if ensure_child(p, "SizeBox", "GridScroll", "GridBody", True):
        made = True
        ensure_child(p, "Overlay", "GridScrollBody", "GridScroll", False)
        ensure_child(p, "Image", "GridScrollTrack", "GridScrollBody", True)
        ensure_child(p, "Image", "GridScrollThumb", "GridScrollBody", True)
        box = obj(p, "GridScroll")
        box.set_width_override(6.0)
        box.set_height_override(150.0)
        box.slot.set_horizontal_alignment(H.H_ALIGN_RIGHT)
        box.slot.set_vertical_alignment(V.V_ALIGN_BOTTOM)
        box.slot.set_padding(unreal.Margin(0.0, 0.0, 8.0, 14.0))
        box.set_editor_property("visibility", unreal.SlateVisibility.COLLAPSED)
        for n, a in (("GridScrollTrack", 0.15), ("GridScrollThumb", 0.85)):
            im = obj(p, n)
            im.set_color_and_opacity(unreal.LinearColor(1.0, 1.0, 1.0, a))
            im.slot.set_horizontal_alignment(H.H_ALIGN_FILL)
            im.slot.set_vertical_alignment(V.V_ALIGN_FILL)
            im.set_editor_property("visibility", unreal.SlateVisibility.HIT_TEST_INVISIBLE)
    if made:
        compile_bp(p)
        cdo = unreal.get_default_object(EAL.load_blueprint_class(p))
        cdo.set_editor_property("grid_scroll_track_height", 150.0)
        log("screen saved", EAL.save_asset(p, False))


def _text(p, name, text, size, colour=None):
    WS.set_property(p, name, "Text", text)
    WS.set_property(p, name, "Font.Size", str(size))
    if colour is not None:
        obj(p, name).set_editor_property("color_and_opacity", unreal.SlateColor(colour))


def step_shop():
    p = SHOP_DIR + "/WBP_ShopScreen"
    if not EAL.does_asset_exist(p):
        fac = unreal.WidgetBlueprintFactory()
        fac.set_editor_property("parent_class", unreal.ShopScreenWidget)
        AT.create_asset("WBP_ShopScreen", SHOP_DIR, unreal.WidgetBlueprint, fac)
        log("CREATED:", p)
    if not WS.get_hierarchy(p):
        WS.add_component(p, "Overlay", "Root", "", False)

    for ctype, name, parent, var in (
            ("Border", "Dim", "Root", False),
            ("SizeBox", "Frame", "Root", False),
            ("Overlay", "FrameBody", "Frame", False),
            ("WBP_HudPlate", "Plate", "FrameBody", True),
            ("VerticalBox", "Stack", "FrameBody", False),
            ("HorizontalBox", "Header", "Stack", False),
            ("TextBlock", "TitleText", "Header", True),
            ("Spacer", "HeaderGap", "Header", False),
            ("TextBlock", "CreditLabel", "Header", False),
            ("TextBlock", "CreditText", "Header", True),
            ("HorizontalBox", "Columns", "Stack", False),
            ("VerticalBox", "SellColumn", "Columns", False),
            ("TextBlock", "SellTitle", "SellColumn", False),
            ("VerticalBox", "SellPanel", "SellColumn", True),
            ("VerticalBox", "ShelfColumn", "Columns", False),
            ("TextBlock", "ShelfTitle", "ShelfColumn", False),
            ("VerticalBox", "ShelfPanel", "ShelfColumn", True),
            ("VerticalBox", "RestockPanel", "ShelfColumn", True),
            ("VerticalBox", "WeaponColumn", "Columns", False),
            ("TextBlock", "WeaponTitle", "WeaponColumn", False),
            ("VerticalBox", "WeaponShelfPanel", "WeaponColumn", True),
            ("TextBlock", "HintText", "Stack", False)):
        ensure_child(p, ctype, name, parent, var)

    dim = obj(p, "Dim")
    dim.set_editor_property("brush_color", unreal.LinearColor(0.0, 0.0, 0.0, 0.5))
    dim.slot.set_horizontal_alignment(H.H_ALIGN_FILL)
    dim.slot.set_vertical_alignment(V.V_ALIGN_FILL)

    frame = obj(p, "Frame")
    frame.set_width_override(1260.0)
    frame.slot.set_horizontal_alignment(H.H_ALIGN_CENTER)
    frame.slot.set_vertical_alignment(V.V_ALIGN_CENTER)

    plate = obj(p, "Plate")
    plate.set_editor_property("fill_color_tag", tag("Palette.HUD.Plate"))
    plate.set_editor_property("lean", 0.0)
    plate.set_editor_property("radius", 10.0)
    plate.slot.set_horizontal_alignment(H.H_ALIGN_FILL)
    plate.slot.set_vertical_alignment(V.V_ALIGN_FILL)

    stack = obj(p, "Stack")
    stack.slot.set_horizontal_alignment(H.H_ALIGN_FILL)
    stack.slot.set_vertical_alignment(V.V_ALIGN_FILL)
    stack.slot.set_padding(unreal.Margin(28.0, 22.0, 28.0, 22.0))

    dim_text = unreal.LinearColor(1.0, 1.0, 1.0, 0.55)
    _text(p, "TitleText", "SHOP", 24)
    _text(p, "CreditLabel", "CREDIT", 14, dim_text)
    _text(p, "CreditText", "0", 28)
    _text(p, "SellTitle", "SELL A GUN", 14, dim_text)
    _text(p, "ShelfTitle", "SHELF", 14, dim_text)
    _text(p, "WeaponTitle", "FOR YOUR GUNS", 14, dim_text)
    _text(p, "HintText", "Click a row to sell or buy.    F or Esc: close", 13, dim_text)

    fill = unreal.SlateChildSize()
    fill.set_editor_property("size_rule", unreal.SlateSizeRule.FILL)
    fill.set_editor_property("value", 1.0)
    obj(p, "HeaderGap").slot.set_size(fill)
    for n in ("TitleText", "CreditLabel", "CreditText"):
        obj(p, n).slot.set_vertical_alignment(V.V_ALIGN_BOTTOM)
    obj(p, "CreditLabel").slot.set_padding(unreal.Margin(0.0, 0.0, 12.0, 4.0))
    obj(p, "Columns").slot.set_padding(unreal.Margin(0.0, 18.0, 0.0, 14.0))
    for i, n in enumerate(("SellColumn", "ShelfColumn", "WeaponColumn")):
        c = obj(p, n)
        c.slot.set_size(fill)
        c.slot.set_padding(unreal.Margin(0.0 if i == 0 else 12.0, 0.0, 0.0 if i == 2 else 12.0, 0.0))
    for n in ("SellTitle", "ShelfTitle", "WeaponTitle"):
        obj(p, n).slot.set_padding(unreal.Margin(0.0, 0.0, 0.0, 8.0))
    obj(p, "RestockPanel").slot.set_padding(unreal.Margin(0.0, 14.0, 0.0, 0.0))

    compile_bp(p)
    cdo = unreal.get_default_object(EAL.load_blueprint_class(p))
    feed = unreal.get_default_object(EAL.load_blueprint_class(REG + "/WBP_TurretFeed"))
    cdo.set_editor_property("entry_class", EAL.load_blueprint_class(REG + "/WBP_TurretFeedEntry"))
    for n in ("available_color_tag", "unavailable_color_tag", "plate_color_tag", "refused_flash_tag"):
        cdo.set_editor_property(n, feed.get_editor_property(n))
        log("  ", n, feed.get_editor_property(n).export_text())
    compile_bp(p)
    log("shop saved", EAL.save_asset(p, False))
    return p


def step_row():
    """The shop's row: the dispenser menu's row class (UTurretFeedEntryWidget fills NameText, AmmoText,
    StatusText, Icon, Plate by name), laid out for a narrow column and long names: icon, name over a
    detail line, price on the right."""
    p = SHOP_DIR + "/WBP_ShopRow"
    if not EAL.does_asset_exist(p):
        fac = unreal.WidgetBlueprintFactory()
        fac.set_editor_property("parent_class", unreal.TurretFeedEntryWidget)
        AT.create_asset("WBP_ShopRow", SHOP_DIR, unreal.WidgetBlueprint, fac)
        log("CREATED:", p)
    if not WS.get_hierarchy(p):
        WS.add_component(p, "Overlay", "Root", "", False)
    for ctype, name, parent, var in (
            ("WBP_HudPlate", "Plate", "Root", True),
            ("HorizontalBox", "Row", "Root", False),
            ("SizeBox", "IconBox", "Row", False),
            ("Image", "Icon", "IconBox", True),
            ("VerticalBox", "Texts", "Row", False),
            ("TextBlock", "NameText", "Texts", True),
            ("TextBlock", "AmmoText", "Texts", True),
            ("TextBlock", "StatusText", "Row", True)):
        ensure_child(p, ctype, name, parent, var)

    plate = obj(p, "Plate")
    plate.set_editor_property("fill_color_tag", tag("Palette.HUD.Plate"))
    plate.set_editor_property("lean", 0.0)
    plate.set_editor_property("radius", 6.0)
    plate.slot.set_horizontal_alignment(H.H_ALIGN_FILL)
    plate.slot.set_vertical_alignment(V.V_ALIGN_FILL)
    row = obj(p, "Row")
    row.slot.set_horizontal_alignment(H.H_ALIGN_FILL)
    row.slot.set_vertical_alignment(V.V_ALIGN_CENTER)
    row.slot.set_padding(unreal.Margin(10.0, 7.0, 14.0, 7.0))

    box = obj(p, "IconBox")
    box.set_width_override(40.0)
    box.set_height_override(40.0)
    box.slot.set_vertical_alignment(V.V_ALIGN_CENTER)
    box.slot.set_padding(unreal.Margin(0.0, 0.0, 10.0, 0.0))

    fill = unreal.SlateChildSize()
    fill.set_editor_property("size_rule", unreal.SlateSizeRule.FILL)
    fill.set_editor_property("value", 1.0)
    texts = obj(p, "Texts")
    texts.slot.set_size(fill)
    texts.slot.set_vertical_alignment(V.V_ALIGN_CENTER)

    WS.set_property(p, "NameText", "Font.Size", "15")
    WS.set_property(p, "AmmoText", "Font.Size", "11")
    WS.set_property(p, "StatusText", "Font.Size", "18")
    for n in ("NameText", "AmmoText"):
        t = obj(p, n)
        t.set_editor_property("text_overflow_policy", unreal.TextOverflowPolicy.ELLIPSIS)
        t.set_editor_property("clipping", unreal.WidgetClipping.CLIP_TO_BOUNDS)
    st = obj(p, "StatusText")
    st.set_editor_property("justification", unreal.TextJustify.RIGHT)
    st.slot.set_vertical_alignment(V.V_ALIGN_CENTER)
    st.slot.set_padding(unreal.Margin(10.0, 0.0, 0.0, 0.0))

    compile_bp(p)
    log("row saved", EAL.save_asset(p, False))

    shop = SHOP_DIR + "/WBP_ShopScreen"
    cdo = unreal.get_default_object(EAL.load_blueprint_class(shop))
    cdo.set_editor_property("entry_class", EAL.load_blueprint_class(p))
    for n in ("SellPanel", "ShelfPanel", "WeaponShelfPanel", "RestockPanel"):
        pass
    compile_bp(shop)
    log("shop row class set", EAL.save_asset(shop, False))


def step_controller(shop_path):
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    cls = EAL.load_blueprint_class(shop_path)
    for a in ar.get_assets_by_path("/Game/Variant_Shooter/Blueprints", recursive=False):
        n = str(a.asset_name)
        if "PlayerController" not in n or str(a.asset_class_path.asset_name) != "Blueprint":
            continue
        path = str(a.package_name)
        cdo = unreal.get_default_object(EAL.load_blueprint_class(path))
        if not isinstance(cdo, unreal.ShooterPlayerController):
            continue
        if cdo.get_editor_property("shop_screen_class") == cls:
            log("controller already set", n)
            continue
        cdo.set_editor_property("shop_screen_class", cls)
        compile_bp(path)
        log("MODIFIED", n, "shop_screen_class", EAL.save_asset(path, False))


def main():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if les.is_in_play_in_editor():
        log("PIE is running, nothing done")
        return
    step_bar()
    step_screen()
    step_controller(step_shop())
    step_row()
    log("DONE")
