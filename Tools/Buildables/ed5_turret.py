# Turret: the feed key, the character wiring, BP_Buildable_Turret on ATurretBuildable with its
# vices, the owner's quiet feedback set, the third hit marker picture in the crosshair.
# Run AFTER the full rebuild that brings ATurretBuildable in. Idempotent: creation is guarded,
# configuration is rewritten each run. Loads only the handful of Blueprints it names: loading
# every Blueprint of a folder tree to look at its parent crashes the editor (Python_Editor.md).
import unreal, json

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
IS = unreal.InputService
WS = unreal.WidgetService
ACT = "/Game/Variant_Shooter/Input/Actions"
INP = "/Game/Variant_Shooter/Input"
BUILD = "/Game/Variant_Shooter/Buildables"
CHAR = "/Game/Variant_Shooter/Blueprints/BP_ShooterCharacter1"
TURRET_BP = BUILD + "/BP_Buildable_Turret"
FEEDBACK = BUILD + "/DA_HitFeedback_Turret"
FEEDBACK_SRC = "/Game/Variant_Shooter/Blueprints/DA_HitFeedbackSet"
WEAPONS = "/Game/Variant_Shooter/Blueprints/Pickups/Weapons"
DROPS_DIR = WEAPONS + "/Drops"
ROCKET_WEAPONS = [WEAPONS + "/Kinemation/BP_RPG7"]
CROSSHAIR = "/Game/Variant_Shooter/UI/Widgets/HUD/WBP_Crosshair"

# First free one in IMC_Weapons wins. Reported in the log; change here if the pick is bad.
PREFERRED_KEYS = ["F", "G", "T", "H", "X", "Z"]

def log(*a):
    print(" ".join(str(x) for x in a))

def tool(toolset, name, args):
    res = unreal.ToolsetRegistry.execute_tool(toolset, name, json.dumps(args))
    if res.error:
        log("TOOL ERROR", name, res.error)
        return None
    try:
        return json.loads(res.get_value_as_json_string())
    except Exception:
        return {}

def compile_bp(path):
    tool("editor_toolset.toolsets.blueprint.BlueprintTools", "compile_blueprint",
         {"blueprint": {"refPath": path + "." + path.split("/")[-1]}})
    bp = unreal.load_asset(path)
    log("compiled", path.split("/")[-1], bp.get_editor_property("status") if bp else "no asset")

def obj(path, widget):
    return unreal.load_object(None, path + "." + path.split("/")[-1] + ":WidgetTree." + widget)

# ---------------- 1. input ----------------

def used_keys(ctx_path):
    # InputService.get_mappings reads a stale array; the 5.8 truth is default_key_mappings.
    imc = unreal.load_asset(ctx_path)
    keys = set()
    try:
        for m in imc.get_editor_property("default_key_mappings"):
            keys.add(str(m.get_editor_property("key").get_editor_property("key_name")))
    except Exception as e:
        log("could not list mappings of", ctx_path, e)
    return keys

def setup_input():
    p = ACT + "/IA_FeedTurret"
    if not IS.input_action_exists(p):
        log("CREATED:", p, IS.create_action("IA_FeedTurret", ACT, "Boolean"))
    imc_weapons = INP + "/IMC_Weapons"
    if IS.key_mapping_exists(imc_weapons, p):
        log("kept mapping IA_FeedTurret in IMC_Weapons")
    else:
        taken = used_keys(imc_weapons)
        log("IMC_Weapons keys in use:", sorted(taken))
        key = next((k for k in PREFERRED_KEYS if k not in taken), None)
        if key is None:
            log("NO FREE KEY among", PREFERRED_KEYS, "- map IA_FeedTurret by hand")
        else:
            log("MAPPED: IA_FeedTurret ->", key, IS.add_key_mapping(imc_weapons, p, key))
    EAL.save_asset(p)
    EAL.save_asset(imc_weapons)

    cls = EAL.load_blueprint_class(CHAR)
    cdo = unreal.get_default_object(cls)
    bc = cdo.get_editor_property("builder_component")
    bc.set_editor_property("feed_action", unreal.load_asset(p))
    compile_bp(CHAR)
    EAL.save_asset(CHAR)
    log("MODIFIED:", CHAR, "feed action set")

# ---------------- 2. the owner's quiet feedback set ----------------

def setup_feedback_set():
    if EAL.does_asset_exist(FEEDBACK):
        log("kept", FEEDBACK)
        return unreal.load_asset(FEEDBACK)
    if EAL.does_asset_exist(FEEDBACK_SRC):
        EAL.duplicate_asset(FEEDBACK_SRC, FEEDBACK)
        log("CREATED:", FEEDBACK, "as a copy of", FEEDBACK_SRC)
    else:
        log("no", FEEDBACK_SRC, "to copy; creating an empty set (author fills sounds)")
        fac = unreal.DataAssetFactory()
        fac.set_editor_property("data_asset_class", unreal.HitFeedbackSet)
        AT.create_asset("DA_HitFeedback_Turret", BUILD, None, fac)
    da = unreal.load_asset(FEEDBACK)
    # Quieter by half and a touch lower: the same sounds until the author records the turret's own.
    for cue_name in ("hit_flesh", "hit_shield", "headshot", "shield_break", "kill", "headshot_kill", "zero_damage"):
        cue = da.get_editor_property(cue_name)
        cue.set_editor_property("volume", max(0.05, float(cue.get_editor_property("volume")) * 0.5))
        cue.set_editor_property("pitch_min", 0.85)
        cue.set_editor_property("pitch_max", 0.9)
        da.set_editor_property(cue_name, cue)
    da.set_editor_property("min_cue_interval", 0.08)
    EAL.save_asset(FEEDBACK)
    log("MODIFIED:", FEEDBACK, "volumes halved, pitch lowered")
    return da

# ---------------- 3. the turret blueprint ----------------

def drop_classes():
    """ADroppedRangedWeapon Blueprints of the Drops folder: (drop class, weapon class it grants)."""
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    out = []
    for d in reg.get_assets_by_path(DROPS_DIR, False):
        if str(d.asset_class_path.asset_name) != "Blueprint":
            continue
        p = str(d.package_name)
        cls = EAL.load_blueprint_class(p)
        if not cls:
            continue
        cdo = unreal.get_default_object(cls)
        try:
            wcls = cdo.get_editor_property("weapon_class")
        except Exception:
            continue  # the melee drop has no weapon_class
        if wcls:
            out.append((cls, wcls))
            log("  drop", p.split("/")[-1], "grants", wcls.get_name())
    return out

def rocket_classes(drops):
    """The heavy vice's list: the named rocket launchers, each checked for an exploding round."""
    candidates = []
    for p in ROCKET_WEAPONS:
        cls = EAL.load_blueprint_class(p) if EAL.does_asset_exist(p) else None
        if cls:
            candidates.append(cls)
    for dcls, wcls in drops:
        if "Rocket" in dcls.get_name() and wcls not in candidates:
            candidates.append(wcls)
    heavy = []
    for cls in candidates:
        cdo = unreal.get_default_object(cls)
        try:
            proj = cdo.get_editor_property("projectile_class")
            pcdo = unreal.get_default_object(proj) if proj else None
            explodes = bool(pcdo.get_editor_property("explode_on_hit")) if pcdo else False
        except Exception as e:
            log("  cannot read", cls.get_name(), e)
            explodes = False
        log("  heavy candidate", cls.get_name(), "explodes" if explodes else "DOES NOT explode (kept anyway, author decides)")
        heavy.append(cls)
    return heavy

def setup_turret_bp(feedback):
    cls = EAL.load_blueprint_class(TURRET_BP)
    if not unreal.MathLibrary.class_is_child_of(cls, unreal.TurretBuildable):
        # Direct parent_class writes do not exist in Python; Epic's tool does it (Python_Editor.md).
        r = tool("editor_toolset.toolsets.blueprint.BlueprintTools", "set_parent",
                 {"blueprint": {"refPath": TURRET_BP + ".BP_Buildable_Turret"},
                  "parent_class": {"refPath": "/Script/Polarity.TurretBuildable"}})
        log("  set_parent ->", r)
        compile_bp(TURRET_BP)
        cls = EAL.load_blueprint_class(TURRET_BP)
        log("MODIFIED:", TURRET_BP, "is TurretBuildable now:",
            bool(cls) and unreal.MathLibrary.class_is_child_of(cls, unreal.TurretBuildable))
    cdo = unreal.get_default_object(cls)

    # Vices on top of the placeholder cube (mesh at z 50, 100 tall): three mounts in a row across
    # Y, the middle one a little higher. Real placement comes with the real mesh.
    mounts = cdo.get_editor_property("vice_mounts")
    spots = (unreal.Vector(0, -28, 108), unreal.Vector(0, 0, 118), unreal.Vector(0, 28, 108))
    for m, loc in zip(mounts, spots):
        m.set_editor_property("relative_location", loc)
        m.set_editor_property("relative_rotation", unreal.Rotator(roll=0, pitch=0, yaw=0))
    log("  mounts:", [str(m.get_editor_property("relative_location")) for m in mounts])

    drops = drop_classes()
    cdo.set_editor_property("drop_class_fallbacks", {wcls: dcls for dcls, wcls in drops})
    cdo.set_editor_property("rocket_vice_weapon_classes", rocket_classes(drops))
    cdo.set_editor_property("owner_feedback_set", feedback)
    compile_bp(TURRET_BP)
    EAL.save_asset(TURRET_BP)
    log("MODIFIED:", TURRET_BP, "fallback drops:", len(drops))

# ---------------- 4. the third hit marker picture ----------------

def setup_crosshair_image():
    p = CROSSHAIR
    if not EAL.does_asset_exist(p):
        log("no", p, "- add RemoteHitMarkerImage by hand")
        return
    names = set(str(h.widget_name) for h in WS.get_hierarchy(p))
    if "HitMarkerImage" not in names:
        log("no HitMarkerImage in", p, "- add RemoteHitMarkerImage by hand")
        return
    if "RemoteHitMarkerImage" in names:
        log("kept RemoteHitMarkerImage in", p)
        return
    src = obj(p, "HitMarkerImage")
    parent = src.get_parent()
    if not parent:
        log("HitMarkerImage has no parent panel in", p, "- add RemoteHitMarkerImage by hand")
        return
    r = WS.add_component(p, "Image", "RemoteHitMarkerImage", parent.get_name(), True)
    log("  + RemoteHitMarkerImage ->", parent.get_name(), r.success if hasattr(r, "success") else r)
    dst = obj(p, "RemoteHitMarkerImage")
    dst.set_editor_property("brush", src.get_editor_property("brush"))
    dst.set_editor_property("visibility", unreal.SlateVisibility.COLLAPSED)
    # The same slot as the original, whatever panel it sits in (canvas: layout; overlay: alignment).
    ss, ds = src.slot, dst.slot
    for prop in ("layout_data", "horizontal_alignment", "vertical_alignment", "padding", "z_order"):
        try:
            ds.set_editor_property(prop, ss.get_editor_property(prop))
        except Exception:
            pass
    compile_bp(p)
    EAL.save_asset(p)
    log("MODIFIED:", p)

def main():
    setup_input()
    fb = setup_feedback_set()
    setup_turret_bp(fb)
    setup_crosshair_image()
    log("DONE")

main()
