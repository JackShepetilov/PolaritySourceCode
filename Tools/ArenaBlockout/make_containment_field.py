# Builds the containment-field assets for arena blockouts:
#   /Game/ArenaBlockout/Materials/M_ContainmentField  (hit-reveal force field shader)
#   /Game/ArenaBlockout/BP_ContainmentField           (BlockAll cube that feeds hits into the material)
#
# Behavior: the field mesh is invisible in game (opacity 0) until something slams
# into it; then a round patch (world-aligned pattern texture, tinted, emissive)
# appears around the hit point and fades out over FadeTime seconds.
# Combat-time enabling/disabling is NOT here - the actor is wired into
# AArenaManager.ExitBlockers by build_arena.py and toggled by SetBlockersEnabled.
#
# Idempotent: deletes and recreates both assets.
# Run (editor): py "<...>/make_containment_field.py"
# Log tag: [FIELD_BUILD]

import unreal

MAT_DIR = "/Game/ArenaBlockout/Materials"
MAT_NAME = "M_ContainmentField"
MAT_PATH = "{}/{}".format(MAT_DIR, MAT_NAME)
BP_DIR = "/Game/ArenaBlockout"
BP_NAME = "BP_ContainmentField"
BP_PATH = "{}/{}".format(BP_DIR, BP_NAME)
WAT_FUNC = "/Engine/Functions/Engine_MaterialFunctions01/Texturing/WorldAlignedTexture.WorldAlignedTexture"
PATTERN_CANDIDATES = [
    "/Game/LevelPrototyping/Textures/T_GridChecker_A.T_GridChecker_A",
    "/Game/LevelPrototyping/Textures/T_GridChecker.T_GridChecker",
    "/Engine/EngineMaterials/T_Default_Material_Grid_M.T_Default_Material_Grid_M",
]

eal = unreal.EditorAssetLibrary
ms = unreal.MaterialService
mns = unreal.MaterialNodeService
bs = unreal.BlueprintService


def log(msg):
    unreal.log("[FIELD_BUILD] {}".format(msg))


def fail(msg):
    raise RuntimeError("[FIELD_BUILD] " + msg)


def pname(p):
    """Pin name across APIs: FBlueprintPinInfo.pin_name vs MaterialNodePinInfo.name."""
    n = getattr(p, "pin_name", None)
    if n is None:
        n = getattr(p, "name", "")
    return str(n)


def find_pin(pins, *needles):
    """First pin whose name contains any needle (case-insensitive)."""
    for needle in needles:
        for p in pins:
            if needle.lower() in pname(p).lower():
                return pname(p)
    return None


# ---------------------------------------------------------------- material --
def build_material():
    if eal.does_asset_exist(MAT_PATH):
        eal.delete_asset(MAT_PATH)
        log("Deleted old " + MAT_PATH)
    res = ms.create_material(MAT_NAME, MAT_DIR + "/")
    if not res.success:
        fail("create_material: " + str(res.error_message))
    path = res.asset_path
    log("Created material " + path)

    mat = eal.load_asset(path)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)

    def param(ptype, name, value, x, y):
        e = mns.create_parameter(path, ptype, name, "Field", value, x, y)
        return e.id

    def expr(cls, x, y):
        e = mns.create_expression(path, cls, x, y)
        return e.id

    def pin_is_input(p):
        for attr in ("b_is_input", "is_input"):
            if hasattr(p, attr):
                return bool(getattr(p, attr))
        return "in" in str(getattr(p, "direction", "")).lower()

    def wire(src, src_pin, dst, dst_pin):
        if mns.connect_expressions(path, src, src_pin, dst, dst_pin):
            return
        # Retry with discovered pin names (single-input nodes reject "").
        s_pins = mns.get_expression_pins(path, src)
        d_pins = mns.get_expression_pins(path, dst)
        s_name = src_pin
        if not s_name:
            outs = [pname(p) for p in s_pins if not pin_is_input(p)]
            s_name = outs[0] if outs else ""
        ins = [pname(p) for p in d_pins if pin_is_input(p)]
        cand = []
        if dst_pin:
            cand = [n for n in ins if dst_pin.lower() in n.lower()]
        d_name = cand[0] if cand else (ins[0] if ins else "")
        if mns.connect_expressions(path, src, s_name, dst, d_name):
            log("wire fallback ok: '{}' -> '{}'".format(s_name, d_name))
            return
        fail("connect {}:{} -> {}:{} (pins in={} )".format(
            src, src_pin or s_name, dst, dst_pin or d_name, ins))

    # Parameters
    hit_pos = param("Vector", "HitPos", "0,0,-100000,1", -1700, -300)
    hit_time = param("Scalar", "HitTime", "-100000", -1700, 100)
    radius = param("Scalar", "Radius", "350", -1700, -100)
    fade = param("Scalar", "FadeTime", "1.5", -1700, 300)
    tint = param("Vector", "Tint", "0.2,0.7,1.0,1", -1700, 500)
    intensity = param("Scalar", "Intensity", "4", -1700, 700)
    pat_base = param("Scalar", "PatternBase", "0.25", -1700, 900)
    pat_size = param("Vector", "PatternWorldSize", "500,500,500,1", -1700, 1100)
    max_op = param("Scalar", "MaxOpacity", "0.85", -1700, 1300)

    tex_default = ""
    for cand in PATTERN_CANDIDATES:
        if eal.does_asset_exist(cand):
            tex_default = cand
            break
    pat_tex = param("TextureObject", "PatternTex", tex_default, -1700, 1500)
    log("Pattern texture default: " + (tex_default or "<none>"))

    # Reveal mask: 1 - saturate(dist(WorldPos, HitPos)/Radius), squared
    wpos = expr("WorldPosition", -1400, -400)
    dist = expr("Distance", -1200, -300)
    div_r = expr("Divide", -1000, -300)
    inv_m = expr("OneMinus", -850, -300)
    sat_m = expr("Saturate", -700, -300)
    sq_m = expr("Multiply", -550, -300)
    wire(wpos, "", dist, "A")
    wire(hit_pos, "", dist, "B")
    wire(dist, "", div_r, "A")
    wire(radius, "", div_r, "B")
    wire(div_r, "", inv_m, "")
    wire(inv_m, "", sat_m, "")
    wire(sat_m, "", sq_m, "A")
    wire(sat_m, "", sq_m, "B")

    # Age fade: 1 - saturate((Time - HitTime)/FadeTime)
    t = expr("Time", -1400, 150)
    sub_t = expr("Subtract", -1200, 150)
    div_f = expr("Divide", -1000, 150)
    inv_f = expr("OneMinus", -850, 150)
    sat_f = expr("Saturate", -700, 150)
    wire(t, "", sub_t, "A")
    wire(hit_time, "", sub_t, "B")
    wire(sub_t, "", div_f, "A")
    wire(fade, "", div_f, "B")
    wire(div_f, "", inv_f, "")
    wire(inv_f, "", sat_f, "")

    reveal = expr("Multiply", -400, -100)
    wire(sq_m, "", reveal, "A")
    wire(sat_f, "", reveal, "B")

    # Pattern: WorldAlignedTexture(PatternTex, PatternWorldSize) + PatternBase
    wat = mns.create_function_call(path, WAT_FUNC, -1400, 700)
    wat_id = wat.id
    wat_pins = mns.get_expression_pins(path, wat_id)
    tex_in = find_pin(wat_pins, "TextureObject")
    size_in = find_pin(wat_pins, "TextureSize")
    xyz_out = find_pin(wat_pins, "XYZ")
    if not (tex_in and xyz_out):
        fail("WorldAlignedTexture pins not found: {}".format(
            [str(p.pin_name) for p in wat_pins]))
    wire(pat_tex, "", wat_id, tex_in)
    if size_in:
        wire(pat_size, "", wat_id, size_in)

    boost = expr("Add", -1000, 800)
    wire(wat_id, xyz_out, boost, "A")
    wire(pat_base, "", boost, "B")

    colored = expr("Multiply", -800, 700)
    wire(boost, "", colored, "A")
    wire(tint, "", colored, "B")
    glow = expr("Multiply", -600, 700)
    wire(colored, "", glow, "A")
    wire(intensity, "", glow, "B")

    emissive = expr("Multiply", -250, 300)
    wire(glow, "", emissive, "A")
    wire(reveal, "", emissive, "B")

    opacity = expr("Multiply", -250, 550)
    wire(reveal, "", opacity, "A")
    wire(max_op, "", opacity, "B")

    if not mns.connect_to_output(path, emissive, "", "EmissiveColor"):
        fail("connect EmissiveColor")
    if not mns.connect_to_output(path, opacity, "", "Opacity"):
        fail("connect Opacity")

    ms.compile_material(path)
    eal.save_asset(path)
    log("Material compiled + saved: " + path)
    return path


# --------------------------------------------------------------- blueprint --
def build_blueprint(mat_path):
    if eal.does_asset_exist(BP_PATH):
        eal.delete_asset(BP_PATH)
        log("Deleted old " + BP_PATH)
    created = bs.create_blueprint(BP_NAME, "Actor", BP_DIR + "/")
    if not created:
        fail("create_blueprint returned empty")
    log("Created blueprint " + BP_PATH)

    if not bs.add_component(BP_PATH, "StaticMeshComponent", "FieldMesh"):
        fail("add_component FieldMesh")
    if not bs.set_component_property(BP_PATH, "FieldMesh", "StaticMesh",
                                     "/Engine/BasicShapes/Cube.Cube"):
        log("WARN: set StaticMesh property returned false")
    # Hit events on blocking collisions (player slamming into the field).
    hit_prop_ok = False
    for prop in ("bNotifyRigidBodyCollision", "NotifyRigidBodyCollision",
                 "Simulation Generates Hit Events"):
        if bs.set_component_property(BP_PATH, "FieldMesh", prop, "true"):
            try:
                readback = bs.get_component_property(BP_PATH, "FieldMesh", prop)
            except Exception as e:
                readback = ("readback failed", e)
            log("Hit-notify set via '{}', readback: {}".format(prop, readback))
            hit_prop_ok = True
            break
    if not hit_prop_ok:
        log("WARN: hit notifications NOT enabled - toggle 'Simulation Generates Hit"
            " Events' on FieldMesh manually")
    bs.set_collision_settings(BP_PATH, "FieldMesh", "QueryAndPhysics", "WorldStatic",
                              "BlockAll", {})

    if not bs.add_variable(BP_PATH, "DMI", "MaterialInstanceDynamic"):
        fail("add_variable DMI")
    bs.compile_blueprint(BP_PATH)  # variables must compile before node use

    g = "EventGraph"

    # BeginPlay -> CreateDynamicMaterialInstance(FieldMesh, 0, M_ContainmentField) -> DMI
    ev_begin = bs.add_event_node(BP_PATH, g, "ReceiveBeginPlay", -400, 0)
    get_mesh = bs.add_get_variable_node(BP_PATH, g, "FieldMesh", -400, 150)
    cdmi = bs.add_function_call_node(BP_PATH, g, "PrimitiveComponent",
                                     "CreateDynamicMaterialInstance", 0, 0)
    set_dmi = bs.add_set_variable_node(BP_PATH, g, "DMI", 350, 0)
    for nid, what in ((ev_begin, "ReceiveBeginPlay"), (get_mesh, "get FieldMesh"),
                      (cdmi, "CreateDynamicMaterialInstance"), (set_dmi, "set DMI")):
        if not nid:
            fail("node create failed: " + what)
    if not bs.set_node_pin_value(BP_PATH, g, cdmi, "SourceMaterial", mat_path):
        log("WARN: SourceMaterial pin default not set")

    def pins_of(node_id):
        return bs.get_node_pins(BP_PATH, g, node_id)

    def out_value_pin(node_id):
        for p in pins_of(node_id):
            if not p.is_input and str(p.pin_type) != "exec":
                return str(p.pin_name)
        return None

    def connect(src, sp, dst, dp, hard=True):
        ok = bs.connect_nodes(BP_PATH, g, src, sp, dst, dp)
        if not ok and hard:
            fail("connect {}:{} -> {}:{}".format(src, sp, dst, dp))
        if not ok:
            log("WARN: optional connect failed {}:{} -> {}:{}".format(src, sp, dst, dp))
        return ok

    # Hit notifications: the SCS template property is not settable through the
    # service, so enable it at runtime as the first BeginPlay step.
    set_notify = bs.add_function_call_node(BP_PATH, g, "PrimitiveComponent",
                                           "SetNotifyRigidBodyCollision", -200, 0)
    get_mesh2 = bs.add_get_variable_node(BP_PATH, g, "FieldMesh", -400, 300)
    if not (set_notify and get_mesh2):
        fail("node create failed: SetNotifyRigidBodyCollision chain")
    notify_pin = find_pin([p for p in pins_of(set_notify) if p.is_input], "Notify")
    if notify_pin:
        bs.set_node_pin_value(BP_PATH, g, set_notify, notify_pin, "true")
    else:
        log("WARN: notify bool pin not found on SetNotifyRigidBodyCollision")

    connect(ev_begin, "then", set_notify, "execute")
    connect(get_mesh2, out_value_pin(get_mesh2), set_notify, "self")
    connect(set_notify, "then", cdmi, "execute")
    mesh_out = out_value_pin(get_mesh)
    connect(get_mesh, mesh_out, cdmi, "self")
    connect(cdmi, "then", set_dmi, "execute")
    connect(cdmi, "ReturnValue", set_dmi, "DMI")

    # Hit -> BreakHitResult.Location -> ToLinearColor -> DMI.SetVectorParameterValue(HitPos)
    #     -> DMI.SetScalarParameterValue(HitTime, GetTimeSeconds)
    ev_hit = bs.add_event_node(BP_PATH, g, "ReceiveHit", -500, 500)
    conv = bs.add_function_call_node(BP_PATH, g, "KismetMathLibrary",
                                     "Conv_VectorToLinearColor", 0, 700)
    get_dmi1 = bs.add_get_variable_node(BP_PATH, g, "DMI", 0, 850)
    get_dmi2 = bs.add_get_variable_node(BP_PATH, g, "DMI", 350, 850)
    set_vec = bs.add_function_call_node(BP_PATH, g, "MaterialInstanceDynamic",
                                        "SetVectorParameterValue", 250, 500)
    set_scal = bs.add_function_call_node(BP_PATH, g, "MaterialInstanceDynamic",
                                         "SetScalarParameterValue", 600, 500)
    time_now = bs.add_function_call_node(BP_PATH, g, "GameplayStatics", "GetTimeSeconds", 350, 1000)
    for nid, what in ((ev_hit, "ReceiveHit"), (conv, "ToLinearColor"),
                      (get_dmi1, "get DMI 1"), (get_dmi2, "get DMI 2"),
                      (set_vec, "SetVectorParameterValue"), (set_scal, "SetScalarParameterValue"),
                      (time_now, "GetTimeSeconds")):
        if not nid:
            fail("node create failed: " + what)

    hit_pins = pins_of(ev_hit)
    hit_loc = find_pin(hit_pins, "HitLocation", "Hit Location")
    if not hit_loc:
        fail("ReceiveHit: HitLocation pin not found: {}".format(
            [str(p.pin_name) for p in hit_pins]))
    conv_in = find_pin([p for p in pins_of(conv) if p.is_input], "InVec", "In Vec", "Vec")

    connect(ev_hit, "then", set_vec, "execute")
    connect(set_vec, "then", set_scal, "execute")
    connect(ev_hit, hit_loc, conv, conv_in)
    connect(conv, "ReturnValue", set_vec, "Value")
    connect(get_dmi1, out_value_pin(get_dmi1), set_vec, "self")
    connect(get_dmi2, out_value_pin(get_dmi2), set_scal, "self")
    connect(time_now, "ReturnValue", set_scal, "Value")
    # GetTimeSeconds world context (hidden DefaultToSelf on most builds) - optional
    connect(get_mesh, mesh_out, time_now, "WorldContextObject", hard=False)

    bs.set_node_pin_value(BP_PATH, g, set_vec, "ParameterName", "HitPos")
    bs.set_node_pin_value(BP_PATH, g, set_scal, "ParameterName", "HitTime")

    result = bs.compile_blueprint(BP_PATH)
    log("Compile: success={} errors={} warnings={}".format(
        result.success, result.num_errors, result.num_warnings))
    for m in list(result.errors)[:5] + list(result.warnings)[:5]:
        log("  compile msg: {}".format(m))
    if not result.success or result.num_errors > 0:
        fail("Blueprint compile failed")
    eal.save_asset(BP_PATH)
    log("Blueprint compiled + saved: " + BP_PATH)


def main():
    mat_path = build_material()
    build_blueprint(mat_path)
    log("RESULT: SUCCESS")


main()
