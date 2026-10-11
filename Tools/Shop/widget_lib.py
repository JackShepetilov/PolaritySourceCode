# Helpers for building widget blueprints from the editor's python: add a widget, and make a new
# widget look like one the author already made (same properties, same slot). Used by setup_shop_ui.
import unreal, json

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
WS = unreal.WidgetService

SKIP = {"Slot", "Slots", "Navigation", "ToolTipWidget", "Text", "bIsVariable", "DisplayLabel", "CategoryName"}
SLOT_PROPS = ("padding", "size", "horizontal_alignment", "vertical_alignment")


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
    bp = unreal.load_asset(path)
    log("compiled", path.split("/")[-1], "err:", res.error, bp.get_editor_property("status") if bp else "no asset")


def obj(path, widget):
    return unreal.load_object(None, path + "." + path.split("/")[-1] + ":WidgetTree." + widget)


def names(path):
    return set(str(h.widget_name) for h in WS.get_hierarchy(path))


def ensure_child(p, ctype, cname, parent, is_var=True):
    """True when the widget was made by this call."""
    if cname in names(p):
        return False
    r = WS.add_component(p, ctype, cname, parent, is_var)
    log("  +", cname, ctype, "->", parent, r.success if hasattr(r, "success") else r)
    return True


def copy_look(p, src, dst, with_text=False):
    """Every editable property of src onto dst, then the slot (padding, size, alignment)."""
    bad = []
    for pr in WS.get_component_properties(p, src):
        n = str(pr.property_name)
        if not pr.is_editable or n in SKIP or n.endswith("Delegate"):
            continue
        if not WS.set_property(p, dst, n, str(pr.current_value)):
            bad.append(n)
    if with_text:
        for pr in WS.get_component_properties(p, src):
            if str(pr.property_name) == "Text":
                WS.set_property(p, dst, "Text", str(pr.current_value))
    s, d = obj(p, src), obj(p, dst)
    # The text copy above loses part of a brush (a rounded box came out with HalfHeightRadius, an
    # oval): the brush itself is copied object to object.
    if s and d and isinstance(s, unreal.Image) and isinstance(d, unreal.Image):
        d.set_editor_property("brush", s.get_editor_property("brush"))
    if s and d and s.slot and d.slot and type(s.slot) == type(d.slot):
        for n in SLOT_PROPS:
            try:
                d.slot.set_editor_property(n, s.slot.get_editor_property(n))
            except Exception:
                pass
    if bad:
        log("   not copied", src, "->", dst, bad)
