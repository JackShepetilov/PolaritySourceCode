# microwave_vfx.py: the microwave grenade's look (Docs/Microwave_Grenade_VFX_Prompts_2026-10-14.md,
# "Выбрано автором": A1, A6, C1; a blue to violet gradient, near-white edges, a little colour split).
# Run step by step: Tools/ue_step.sh Grenades microwave_vfx <step>. Refuses to touch Niagara during PIE.
#
# Steps: materials, field, pulse, headache, wire
#
# Most of the motion lives in the materials, driven by the particle's age (ParticleRelativeTime), so the
# Niagara side stays one still particle per part: Niagara curves cannot be written from Python
# (Docs/Gotchas/Niagara.md), materials can.
import json
import unreal

LIB = r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/DamagePlayground/niagara_lib.py"
nl = {"__name__": "nl"}
exec(compile(open(LIB, encoding="utf-8").read(), LIB, "exec"), nl)

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
OUT = "/Game/Variant_Shooter/Blueprints/Grenades/Microwave"

BLUE = (0.10, 0.22, 1.00, 1.0)
VIOLET = (0.62, 0.16, 1.00, 1.0)
WHITE = (0.85, 0.80, 1.00, 1.0)


def _pie():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None


# ==================== Materials ====================

# The dome and the pulse wave: one sphere mesh particle. N, V: world normal and camera vector; WP: world
# position (cm); Age: the particle's 0..1; Inter: depth fade (0 where the sphere cuts a wall or the floor).
FIELD_CODE = """
float ndv = abs(dot(normalize(N), normalize(V)));
float rim = pow(saturate(1.0 - ndv), RimPower);
float3 p = WP * NoiseScale;
float t = T * Flow;
float n1 = sin(p.x * 1.7 + t * 1.3) * sin(p.y * 1.3 - t * 0.9) * sin(p.z * 1.1 + t * 0.7);
float n2 = sin(p.x * 3.1 - t * 1.9 + n1 * 2.0) * sin(p.y * 2.7 + t * 1.4) * sin(p.z * 2.3 - t * 1.1);
float veins = pow(saturate(1.0 - abs(n1 + 0.5 * n2) * 3.0), VeinSharp);
float inter = 1.0 - saturate(Inter);
float life = saturate(Age / max(FadeIn, 0.001)) * saturate((1.0 - Age) / max(FadeOut, 0.001));
float h = rim * FilmBands + n1 * 0.5 + T * 0.15;
float3 film = 0.5 + 0.5 * cos(6.2832 * (h + float3(0.0, 0.33, 0.67)));
float3 grad = lerp(BlueColor, VioletColor, saturate(rim * 1.3 + n2 * 0.3));
float3 c = grad * (rim * RimBoost + veins * VeinBoost * (0.3 + rim) + inter * InterBoost)
         + film * rim * FilmBoost + EdgeColor * pow(rim, 6.0) * EdgeBoost + EdgeColor * inter * inter * InterBoost * 0.5;
return c * PColor.rgb * PColor.a * life * Intensity;
"""

# Grows the sphere from its centre to full size over GrowFraction of its life: 1 for the pulse wave (it
# crosses the room as it lives), small for the dome (it inflates, then stands).
FIELD_WPO_CODE = """
float g = saturate(Age / max(GrowFraction, 0.001));
g = 1.0 - (1.0 - g) * (1.0 - g);
return (Center - WP) * (1.0 - g);
"""

FIELD_SCALARS = {"Intensity": 1.0, "RimPower": 2.5, "RimBoost": 1.6, "NoiseScale": 0.004, "Flow": 1.0,
                 "VeinSharp": 6.0, "VeinBoost": 0.35, "InterBoost": 3.0, "FilmBands": 2.0, "FilmBoost": 0.35,
                 "EdgeBoost": 1.5, "FadeIn": 0.08, "FadeOut": 0.15, "GrowFraction": 0.1, "DepthFadeDistance": 40.0}
FIELD_VECTORS = {"BlueColor": BLUE, "VioletColor": VIOLET, "EdgeColor": WHITE}

# Rings on the floor around the grenade, a flat plane mesh particle. UV 0..1 over the plane.
RIPPLE_CODE = """
float r = length(UV - 0.5) * 2.0;
float a = 0.0;
for (int i = 0; i < 3; i++)
{
    float at = Age * Speed - i * Spacing;
    a += exp(-pow((r - at) / Width, 2.0)) * saturate(at) * saturate(1.0 - at);
}
float3 c = lerp(EdgeColor, lerp(BlueColor, VioletColor, saturate(r * 1.4)), saturate(r * 3.0));
return c * a * saturate(1.0 - r) * PColor.rgb * PColor.a * Intensity;
"""
RIPPLE_SCALARS = {"Intensity": 4.0, "Speed": 1.4, "Spacing": 0.22, "Width": 0.035}
RIPPLE_VECTORS = {"BlueColor": BLUE, "VioletColor": VIOLET, "EdgeColor": WHITE}

# Horizontal glitch streaks with the colours pulled apart, on a camera facing sprite at the head (C1).
GLITCH_CODE = """
float rows = Rows;
float row = floor(UV.y * rows);
float tick = floor(T * Rate);
float rnd = frac(sin(row * 12.9898 + tick * 78.233 + Seed * 3.17) * 43758.5453);
float on = step(1.0 - Density, rnd);
float x0 = frac(rnd * 3.7) * 0.55;
float len = 0.15 + frac(rnd * 7.13) * 0.45;
float band = 1.0 - abs(frac(UV.y * rows) - 0.5) * 2.0;
band = pow(saturate(band), 3.0);
float segR = step(x0 + Split, UV.x) * step(UV.x, x0 + len + Split);
float segG = step(x0, UV.x) * step(UV.x, x0 + len);
float segB = step(x0 - Split, UV.x) * step(UV.x, x0 + len - Split);
float fall = exp(-dot(UV - 0.5, UV - 0.5) * 7.0);
float3 split = float3(segR, segG * 0.6, segB);
float3 tint = lerp(BlueColor, VioletColor, frac(rnd * 5.1));
return (split * tint + segG * EdgeColor * 0.4) * on * band * fall * PColor.rgb * PColor.a * Intensity;
"""
GLITCH_SCALARS = {"Intensity": 6.0, "Rows": 18.0, "Rate": 14.0, "Density": 0.28, "Split": 0.03, "Seed": 0.0}
GLITCH_VECTORS = {"BlueColor": BLUE, "VioletColor": VIOLET, "EdgeColor": WHITE}


def _new_material(name, mesh):
    path = OUT + "/" + name
    mat = unreal.load_asset(path)
    if mat:
        for e in MEL.get_material_expressions(mat):
            MEL.delete_material_expression(mat, e)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, OUT, unreal.Material, unreal.MaterialFactoryNew())
        print("CREATED:", path)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("used_with_niagara_mesh_particles" if mesh else "used_with_niagara_sprites", True)
    return path, mat


def _custom(mat, code, out_type, names, x, y, desc):
    c = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, x, y)
    c.set_editor_property("code", code)
    c.set_editor_property("output_type", out_type)
    c.set_editor_property("description", desc)
    ins = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    c.set_editor_property("inputs", ins)
    return c


def _params(mat, c, scalars, vectors, y, skip=()):
    for n, v in vectors.items():
        p = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -900, y)
        p.set_editor_property("parameter_name", n)
        p.set_editor_property("default_value", unreal.LinearColor(*v))
        MEL.connect_material_expressions(p, "", c, n)
        y += 160
    for n, v in scalars.items():
        if n in skip:
            continue
        p = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -900, y)
        p.set_editor_property("parameter_name", n)
        p.set_editor_property("default_value", v)
        MEL.connect_material_expressions(p, "", c, n)
        y += 90
    return y


def _node(mat, cls, x, y):
    return MEL.create_material_expression(mat, cls, x, y)


def _finish_material(path, mat):
    MEL.recompile_material(mat)
    print("SAVED material:", path, EAL.save_asset(path, only_if_is_dirty=False))


def field_material():
    path, mat = _new_material("M_Microwave_Field", mesh=True)
    names = ["N", "V", "WP", "T", "Age", "Inter", "PColor"] + list(FIELD_VECTORS) + [n for n in FIELD_SCALARS if n not in ("GrowFraction", "DepthFadeDistance")]
    c = _custom(mat, FIELD_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, names, -300, 0, "MicrowaveField")
    MEL.connect_material_expressions(_node(mat, unreal.MaterialExpressionVertexNormalWS, -900, -600), "", c, "N")
    MEL.connect_material_expressions(_node(mat, unreal.MaterialExpressionCameraVectorWS, -900, -520), "", c, "V")
    MEL.connect_material_expressions(_node(mat, unreal.MaterialExpressionWorldPosition, -900, -440), "", c, "WP")
    MEL.connect_material_expressions(_node(mat, unreal.MaterialExpressionTime, -900, -360), "", c, "T")
    age = _node(mat, unreal.MaterialExpressionParticleRelativeTime, -900, -280)
    MEL.connect_material_expressions(age, "", c, "Age")
    MEL.connect_material_expressions(_node(mat, unreal.MaterialExpressionParticleColor, -900, -200), "RGBA", c, "PColor")
    fade = _node(mat, unreal.MaterialExpressionDepthFade, -700, -120)
    dist = _node(mat, unreal.MaterialExpressionScalarParameter, -900, -120)
    dist.set_editor_property("parameter_name", "DepthFadeDistance")
    dist.set_editor_property("default_value", FIELD_SCALARS["DepthFadeDistance"])
    MEL.connect_material_expressions(dist, "", fade, "FadeDistance")
    MEL.connect_material_expressions(fade, "", c, "Inter")
    _params(mat, c, FIELD_SCALARS, FIELD_VECTORS, 0, skip=("GrowFraction", "DepthFadeDistance"))
    MEL.connect_material_property(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    w = _custom(mat, FIELD_WPO_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, ["Center", "WP", "Age", "GrowFraction"], -300, 700, "MicrowaveGrow")
    MEL.connect_material_expressions(_node(mat, unreal.MaterialExpressionObjectPositionWS, -900, 700), "", w, "Center")
    MEL.connect_material_expressions(_node(mat, unreal.MaterialExpressionWorldPosition, -900, 780), "", w, "WP")
    MEL.connect_material_expressions(_node(mat, unreal.MaterialExpressionParticleRelativeTime, -900, 860), "", w, "Age")
    g = _node(mat, unreal.MaterialExpressionScalarParameter, -900, 940)
    g.set_editor_property("parameter_name", "GrowFraction")
    g.set_editor_property("default_value", FIELD_SCALARS["GrowFraction"])
    MEL.connect_material_expressions(g, "", w, "GrowFraction")
    MEL.connect_material_property(w, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    _finish_material(path, mat)
    return path


def ripple_material():
    path, mat = _new_material("M_Microwave_Ripple", mesh=True)
    names = ["UV", "Age", "PColor"] + list(RIPPLE_VECTORS) + list(RIPPLE_SCALARS)
    c = _custom(mat, RIPPLE_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, names, -300, 0, "MicrowaveRipple")
    MEL.connect_material_expressions(_node(mat, unreal.MaterialExpressionTextureCoordinate, -900, -300), "", c, "UV")
    MEL.connect_material_expressions(_node(mat, unreal.MaterialExpressionParticleRelativeTime, -900, -220), "", c, "Age")
    MEL.connect_material_expressions(_node(mat, unreal.MaterialExpressionParticleColor, -900, -140), "RGBA", c, "PColor")
    _params(mat, c, RIPPLE_SCALARS, RIPPLE_VECTORS, 0)
    MEL.connect_material_property(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    _finish_material(path, mat)
    return path


def glitch_material():
    path, mat = _new_material("M_Microwave_Glitch", mesh=False)
    names = ["UV", "T", "PColor"] + list(GLITCH_VECTORS) + list(GLITCH_SCALARS)
    c = _custom(mat, GLITCH_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, names, -300, 0, "MicrowaveGlitch")
    MEL.connect_material_expressions(_node(mat, unreal.MaterialExpressionTextureCoordinate, -900, -300), "", c, "UV")
    MEL.connect_material_expressions(_node(mat, unreal.MaterialExpressionTime, -900, -220), "", c, "T")
    MEL.connect_material_expressions(_node(mat, unreal.MaterialExpressionParticleColor, -900, -140), "RGBA", c, "PColor")
    _params(mat, c, GLITCH_SCALARS, GLITCH_VECTORS, 0)
    MEL.connect_material_property(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    _finish_material(path, mat)
    return path


def _instance(name, parent, scalars):
    path = OUT + "/" + name
    mi = unreal.load_asset(path)
    if not mi:
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, OUT, unreal.MaterialInstanceConstant,
                                                                      unreal.MaterialInstanceConstantFactoryNew())
        print("CREATED:", path)
    MEL.set_material_instance_parent(mi, unreal.load_asset(parent))
    for k, v in scalars.items():
        MEL.set_material_instance_scalar_parameter_value(mi, k, v)
    print("SAVED instance:", path, EAL.save_asset(path, only_if_is_dirty=False))


def materials():
    if not EAL.does_directory_exist(OUT):
        EAL.make_directory(OUT)
    field = field_material()
    ripple_material()
    glitch_material()
    # The dome stands for the whole working time: inflates in a tenth of it, fades at the very end.
    _instance("MI_Microwave_Dome", field, {"GrowFraction": 0.08, "FadeIn": 0.05, "FadeOut": 0.08, "Intensity": 0.6})
    # The pulse wave: grows across all of its short life, brightest edge, out by the end.
    _instance("MI_Microwave_Wave", field, {"GrowFraction": 1.0, "FadeIn": 0.02, "FadeOut": 0.5, "Intensity": 1.4,
                                           "VeinBoost": 0.15, "RimBoost": 2.2})
    for name in ("M_Microwave_Field", "M_Microwave_Ripple", "M_Microwave_Glitch"):
        m = unreal.load_asset(OUT + "/" + name)
        try:
            d = unreal.MaterialNodeService.get_material_diagnostics(OUT + "/" + name)
            print("DIAG:", name, d)
        except Exception as e:
            print("DIAG unavailable:", name, e)


# ==================== Niagara ====================

SHELL = "/Game/NiagaraExamples/FX_Explosions/NS_Explosion_Small"
SINGLE = "/Niagara/DefaultAssets/Templates/Emitters/SingleLoopingParticle.SingleLoopingParticle"
SPHERE = "/Engine/BasicShapes/Sphere.Sphere"
PLANE = "/Engine/BasicShapes/Plane.Plane"
FLOAT = {"refPath": nl["FLOAT"]}
ENUM = "/Script/NiagaraEditor.NiagaraExt_StackInputData_Enum"
MESH_RENDERER = "/Script/Niagara.NiagaraMeshRendererProperties"
FIELD_NS = OUT + "/NS_Microwave_Field"
PULSE_NS = OUT + "/NS_Microwave_Pulse"
HEAD_NS = OUT + "/NS_Microwave_Headache"


def _ref(path, em, script="", module="", stack=None):
    return {"system": nl["sysref"](path), "emitterName": em, "scriptName": script, "moduleName": module,
            "rendererIndex": -1, "inputNameStack": stack or []}


def _put(path, em, script, module, stack, data, label):
    err, _ = nl["call"]("SetStackInputData", {"stackInputRef": _ref(path, em, script, module, stack), "inputData": data})
    print(("ERR: " if err else "SET: ") + label + (" | " + str(err)[:300] if err else ""))
    return not err


def _num(path, em, script, module, name, value):
    return _put(path, em, script, module, [name], {"struct": FLOAT, "value": {"value": float(value)}}, em + "." + name + " = " + str(value))


def _link(path, em, script, module, stack, var):
    return _put(path, em, script, module, stack, {"struct": {"refPath": nl["LINKED"]}, "value": {
        "linkedVariable": {"name": var, "type": {"classStructOrEnum": FLOAT}}}}, em + "." + "/".join(stack) + " <- " + var)


def _scaled_user(path, em, script, module, name, user, factor):
    """name = user * factor (Multiply_Float)."""
    ok = _put(path, em, script, module, [name], {"struct": {"refPath": nl["DYNAMIC"]}, "value": {
        "dynamicInputAsset": {"refPath": nl["MULTIPLY_FLOAT"]}}}, em + "." + name + " = Multiply")
    ok = _link(path, em, script, module, [name, "A"], user) and ok
    ok = _put(path, em, script, module, [name, "B"], {"struct": FLOAT, "value": {"value": float(factor)}}, em + "." + name + ".B") and ok
    return ok


def _new_system(path):
    if _pie():
        raise RuntimeError("PIE is running: stop it first (Docs/Gotchas/Niagara.md)")
    if not EAL.does_asset_exist(path):
        print("CREATED:", path, bool(EAL.duplicate_asset(SHELL, path)))
        EAL.save_asset(path, only_if_is_dirty=False)
    for e in nl["emitter_names"](path):
        nl["remove_emitter"](path, e)


def _add_single(path, em):
    err, _ = nl["call"]("AddEmitter", {"system": nl["sysref"](path), "templateEmitter": {"refPath": SINGLE}, "emitterName": em})
    print("ADDED: emitter", em, "| err:", err or "none")
    err, d = nl["call"]("GetEmitterData", {"emitterRef": _ref(path, em)})
    props = json.loads(d["returnValue"]["propertyValues"])
    props.update({"SimTarget": "CPUSim", "CalculateBoundsMode": "Fixed",
                  "FixedBounds": {"min": {"x": -2600, "y": -2600, "z": -2600}, "max": {"x": 2600, "y": 2600, "z": 2600}, "isValid": True}})
    err, _ = nl["call"]("SetEmitterData", {"emitter": _ref(path, em), "emitterData": {"propertyValues": json.dumps(props)}})
    print("MODIFIED: emitter data", em, "| err:", err or "none")


def _mesh_renderer(path, em, mesh, material):
    """A mesh renderer next to the template's sprite one, which is switched off."""
    err, d = nl["call"]("AddRenderer", {"newRendererLocation": _ref(path, em),
                                        "rendererClass": {"refPath": MESH_RENDERER}})
    print("ADDED: mesh renderer on", em, "| err:", err or "none", "| ret:", json.dumps(d)[:200])
    nl["set_renderer"](path, em, {"bIsEnabled": False}, index=0)
    ref = {"system": nl["sysref"](path), "emitterName": em, "rendererIndex": 1}
    err, d = nl["call"]("GetRendererData", {"rendererRef": ref})
    props = json.loads(d["returnValue"]["propertyValues"])
    print("   mesh renderer keys:", sorted(props.keys())[:40])
    return props


def probe(path=FIELD_NS, em="Dome"):
    """Prints what the template's modules take, for the next step to address them by name."""
    err, d = nl["call"]("GetEmitterInputValues", {"emitterRef": {"system": nl["sysref"](path), "emitterName": em}})
    for m in d["returnValue"]:
        print("MODULE", m["moduleName"], "|", [(i.get("name"), str(i.get("value"))[:60]) for i in m.get("inputs", [])][:14])
    for i in range(3):
        err, r = nl["call"]("GetRendererData", {"rendererRef": {"system": nl["sysref"](path), "emitterName": em, "rendererIndex": i}})
        if err or not r:
            break
        props = json.loads(r["returnValue"]["propertyValues"])
        print("RENDERER", i, r["returnValue"].get("rendererClass", ""), json.dumps({k: props[k] for k in props if k in (
            "Meshes", "OverrideMaterials", "bOverrideMaterials", "Material", "bIsEnabled", "FacingMode")})[:700])


def field_step1():
    _new_system(FIELD_NS)
    nl["add_user_float"](FIELD_NS, "Radius", 2000.0, "Reach of the field, cm (AGrenadeProjectile hands it in)")
    nl["add_user_float"](FIELD_NS, "Duration", 6.0, "Seconds it works (AGrenadeProjectile hands it in)")
    _add_single(FIELD_NS, "Dome")
    _mesh_renderer(FIELD_NS, "Dome", SPHERE, OUT + "/MI_Microwave_Dome")
    EAL.save_asset(FIELD_NS, only_if_is_dirty=False)
    probe(FIELD_NS, "Dome")


def probe_renderers(path=FIELD_NS, em="Dome"):
    print("emitters:", nl["emitter_names"](path))
    for i in range(4):
        err, r = nl["call"]("GetRendererData", {"rendererRef": {"system": nl["sysref"](path), "emitterName": em, "rendererIndex": i}})
        print("renderer", i, "err:", str(err)[:200], "| class:", (r or {}).get("returnValue", {}).get("rendererClass", "") if r else None)


def probe_add_mesh(path=FIELD_NS, em="Dome"):
    err, d = nl["call"]("AddRenderer", {"emitterRef": {"system": nl["sysref"](path), "emitterName": em},
                                        "rendererClass": {"refPath": MESH_RENDERER}})
    print("AddRenderer err:", " ".join(str(err).split())[300:2600], "| ret:", json.dumps(d)[:600])
    EAL.save_asset(path, only_if_is_dirty=False)


def probe_mesh_entry(path=FIELD_NS, em="Dome"):
    ref = {"system": nl["sysref"](path), "emitterName": em, "rendererIndex": 1}
    err, d = nl["call"]("GetRendererData", {"rendererRef": ref})
    props = json.loads(d["returnValue"]["propertyValues"])
    m = props["Meshes"][0]
    print("MESH ENTRY keys:", sorted(m.keys()))
    print("mesh value:", json.dumps({k: m[k] for k in m if "esh" in k and "Binding" not in k})[:400])
    print("OverrideMaterials:", json.dumps(props.get("OverrideMaterials"))[:300], "| bOverride:", props.get("bOverrideMaterials"))
    err, d = nl["call"]("GetEmitterInputValues", {"emitterRef": {"system": nl["sysref"](path), "emitterName": em}})
    for mod in d["returnValue"]:
        if mod["moduleName"] == "InitializeParticle":
            print("INIT inputs:", [i.get("name") for i in mod.get("inputs", [])])


def probe_enum(path=FIELD_NS, em="Dome", script="ParticleSpawnScript", module="InitializeParticle", name="Mesh Scale Mode"):
    err, d = nl["call"]("GetStackInputData", {"stackInputRef": _ref(path, em, script, module, [name])})
    print("INPUT", name, "err:", err, "|", json.dumps(d)[:600])
    try:
        enum = d["returnValue"]["value"]["enum"]["refPath"]
    except Exception:
        enum = None
    if enum:
        r = unreal.ToolsetRegistry.execute_tool("NiagaraToolsets.NiagaraToolset_Info", "UEnum_Info", json.dumps({"enum": {"refPath": enum}}))
        print("ENUM", enum, "err:", r.error[:300] if r.error else "", "|", (r.get_value_as_json_string() if not r.error else "")[:900])


def _enum(path, em, script, module, name, enum_path, value, label):
    return _put(path, em, script, module, [name], {"struct": {"refPath": ENUM}, "value": {
        "enum": {"refPath": enum_path}, "enumName": value}}, label)


SCALE_MODE = "/Niagara/Enums/ENiagara_SizeScaleMode.ENiagara_SizeScaleMode"
LIFECYCLE = "/Niagara/Enums/ENiagaraEmitterLifeCycleMode.ENiagaraEmitterLifeCycleMode"
STATE_OPTIONS = "/Niagara/Enums/ENiagara_EmitterStateOptions.ENiagara_EmitterStateOptions"
INACTIVE = "/Niagara/Enums/ENiagaraInactiveMode.ENiagaraInactiveMode"


def _setup_mesh_particle(path, em, mesh, material, mesh_radius, scale_user=None, scale_fixed=1.0, loop_user=None, loop_fixed=1.0):
    """One mesh particle: its own life cycle, Once, living the loop; mesh and material on renderer 1;
    uniform scale = User.<scale_user> / mesh_radius, or scale_fixed."""
    _enum(path, em, "EmitterUpdateScript", "EmitterState", "Life Cycle Mode", LIFECYCLE, "ENiagaraEmitterLifeCycleMode::NewEnumerator1", em + " life cycle = Self")
    _enum(path, em, "EmitterUpdateScript", "EmitterState", "Loop Behavior", STATE_OPTIONS, "ENiagara_EmitterStateOptions::NewEnumerator1", em + " loop = Once")
    if loop_user:
        _link(path, em, "EmitterUpdateScript", "EmitterState", ["Loop Duration"], loop_user)
    else:
        _num(path, em, "EmitterUpdateScript", "EmitterState", "Loop Duration", loop_fixed)
    _enum(path, em, "ParticleSpawnScript", "InitializeParticle", "Mesh Scale Mode", SCALE_MODE, "ENiagara_SizeScaleMode::NewEnumerator3", em + " mesh scale = Uniform")
    if scale_user:
        _scaled_user(path, em, "ParticleSpawnScript", "InitializeParticle", "Mesh Uniform Scale", scale_user, 1.0 / mesh_radius)
    else:
        _num(path, em, "ParticleSpawnScript", "InitializeParticle", "Mesh Uniform Scale", scale_fixed)

    ref = {"system": nl["sysref"](path), "emitterName": em, "rendererIndex": 1}
    err, d = nl["call"]("GetRendererData", {"rendererRef": ref})
    props = json.loads(d["returnValue"]["propertyValues"])
    props["Meshes"][0]["mesh"] = mesh
    props["bOverrideMaterials"] = True
    props["OverrideMaterials"] = [{"explicitMat": material + "." + material.split("/")[-1]}]
    props["SortMode"] = "None"
    err, _ = nl["call"]("SetRendererData", {"renderer": ref, "rendererData": {"propertyValues": json.dumps(props)}})
    print("MODIFIED:", em, "mesh renderer | err:", err or "none")
    err, d = nl["call"]("GetRendererData", {"rendererRef": ref})
    back = json.loads(d["returnValue"]["propertyValues"])
    print("   read back: mesh", back["Meshes"][0]["mesh"], "| override", back.get("bOverrideMaterials"), json.dumps(back.get("OverrideMaterials"))[:200])


def field_step2():
    if _pie():
        raise RuntimeError("PIE is running")
    _setup_mesh_particle(FIELD_NS, "Dome", SPHERE, OUT + "/MI_Microwave_Dome", 50.0, scale_user="User.Radius", loop_user="User.Duration")
    nl["finish"](FIELD_NS)


def _floor_offset(path, em, dz):
    ok = _put(path, em, "ParticleSpawnScript", "InitializeParticle", ["UsePositionOffset"],
              {"struct": {"refPath": "/Script/Niagara.NiagaraBool"}, "value": {"value": True}}, em + " use position offset")
    _put(path, em, "ParticleSpawnScript", "InitializeParticle", ["Position Offset"],
         {"struct": {"refPath": "/Script/CoreUObject.Vector3f"}, "value": {"x": 0.0, "y": 0.0, "z": float(dz)}}, em + " position offset z " + str(dz))
    err, d = nl["call"]("GetStackInputData", {"stackInputRef": _ref(path, em, "ParticleSpawnScript", "InitializeParticle", ["UsePositionOffset"])})
    print("   read back UsePositionOffset:", json.dumps(d)[:200])


def pulse():
    _new_system(PULSE_NS)
    nl["add_user_float"](PULSE_NS, "Radius", 2000.0, "Reach of the field, cm (AGrenadeProjectile hands it in)")
    _add_single(PULSE_NS, "Wave")
    _mesh_renderer(PULSE_NS, "Wave", SPHERE, OUT + "/MI_Microwave_Wave")
    _setup_mesh_particle(PULSE_NS, "Wave", SPHERE, OUT + "/MI_Microwave_Wave", 50.0, scale_user="User.Radius", loop_fixed=0.6)
    _add_single(PULSE_NS, "Ripples")
    _mesh_renderer(PULSE_NS, "Ripples", PLANE, OUT + "/M_Microwave_Ripple")
    # The plane is a metre across: four metres of rings, on the floor (the burst sits 20 cm above it).
    _setup_mesh_particle(PULSE_NS, "Ripples", PLANE, OUT + "/M_Microwave_Ripple", 50.0, scale_fixed=4.0, loop_fixed=0.9)
    _floor_offset(PULSE_NS, "Ripples", -17.0)
    nl["finish"](PULSE_NS)


def headache():
    """The glitch streaks at the head (C1): one sprite riding the head bone, looping until the NPC turns
    it off (AShooterNPC::RefreshHeadacheFX deactivates it: Inactive Response = Kill)."""
    _new_system(HEAD_NS)
    em = "Glitch"
    _add_single(HEAD_NS, em)
    err, d = nl["call"]("GetEmitterData", {"emitterRef": _ref(HEAD_NS, em)})
    props = json.loads(d["returnValue"]["propertyValues"])
    props.update({"bLocalSpace": True, "FixedBounds": {"min": {"x": -150, "y": -150, "z": -150}, "max": {"x": 150, "y": 150, "z": 150}, "isValid": True}})
    err, _ = nl["call"]("SetEmitterData", {"emitter": _ref(HEAD_NS, em), "emitterData": {"propertyValues": json.dumps(props)}})
    print("MODIFIED: local space | err:", err or "none")
    _enum(HEAD_NS, em, "EmitterUpdateScript", "EmitterState", "Life Cycle Mode", LIFECYCLE, "ENiagaraEmitterLifeCycleMode::NewEnumerator1", "life cycle = Self")
    _enum(HEAD_NS, em, "EmitterUpdateScript", "EmitterState", "Loop Behavior", STATE_OPTIONS, "ENiagara_EmitterStateOptions::NewEnumerator0", "loop = Infinite")
    _num(HEAD_NS, em, "EmitterUpdateScript", "EmitterState", "Loop Duration", 1.0)
    _enum(HEAD_NS, em, "EmitterUpdateScript", "EmitterState", "Inactive Response", INACTIVE, "ENiagaraInactiveMode::NewEnumerator1", "inactive = Kill")
    _num(HEAD_NS, em, "ParticleSpawnScript", "InitializeParticle", "Uniform Sprite Size", 45.0)
    _floor_offset(HEAD_NS, em, 8.0)
    nl["set_renderer"](HEAD_NS, em, {"Material": {"refPath": OUT + "/M_Microwave_Glitch.M_Microwave_Glitch"}}, index=0)
    nl["finish"](HEAD_NS)


NPC_BP = "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_ShooterNPC"


def wire():
    da_path = "/Game/Variant_Shooter/Blueprints/Grenades/DA_Grenade_Microwave"
    da = unreal.load_asset(da_path)
    da.set_editor_property("detonation_fx", unreal.load_asset(FIELD_NS))
    da.set_editor_property("pulse_fx", unreal.load_asset(PULSE_NS))
    print("MODIFIED: DA_Grenade_Microwave fx", da.get_editor_property("detonation_fx"), da.get_editor_property("pulse_fx"))
    print("SAVED:", EAL.save_asset(da_path, only_if_is_dirty=False))
    bp = unreal.load_asset(NPC_BP)
    cdo = unreal.get_default_object(bp.generated_class())
    cdo.set_editor_property("headache_fx", unreal.load_asset(HEAD_NS))
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    cdo = unreal.get_default_object(bp.generated_class())
    print("MODIFIED: BP_ShooterNPC headache_fx", cdo.get_editor_property("headache_fx"))
    print("SAVED:", EAL.save_asset(NPC_BP, only_if_is_dirty=False))


SOUND_SRC = "C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Art/Sounds/Mircowave/Built"
SOUND_DST = OUT + "/Sounds"


def sounds():
    """Art/Sounds/Mircowave/Built -> the grenade: start on going off, the loop while it works, the end."""
    tasks = []
    for n in ("S_Microwave_Start", "S_Microwave_Loop", "S_Microwave_End"):
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", SOUND_SRC + "/" + n + ".wav")
        t.set_editor_property("destination_path", SOUND_DST)
        t.set_editor_property("automated", True)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("save", True)
        tasks.append(t)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    loop = unreal.load_asset(SOUND_DST + "/S_Microwave_Loop")
    loop.set_editor_property("looping", True)
    EAL.save_asset(SOUND_DST + "/S_Microwave_Loop", only_if_is_dirty=False)

    da_path = "/Game/Variant_Shooter/Blueprints/Grenades/DA_Grenade_Microwave"
    da = unreal.load_asset(da_path)
    da.set_editor_property("detonation_sound", unreal.load_asset(SOUND_DST + "/S_Microwave_Start"))
    da.set_editor_property("working_loop_sound", loop)
    da.set_editor_property("working_end_sound", unreal.load_asset(SOUND_DST + "/S_Microwave_End"))
    print("MODIFIED: DA_Grenade_Microwave sounds", da.get_editor_property("detonation_sound"),
          da.get_editor_property("working_loop_sound"), da.get_editor_property("working_end_sound"),
          "looping", loop.get_editor_property("looping"), "attenuation", da.get_editor_property("detonation_attenuation"))
    print("SAVED:", EAL.save_asset(da_path, only_if_is_dirty=False))


# ==================== The player's picture under the headache ====================
# Docs/Microwave_Player_Screen_Prompts_2026-10-15.md, the author's pick E5 + E9 [2026-10-15]: thin
# horizontal glitch bands tear across the picture, slices shifted sideways with the colours split, a
# faint violet static in them (E5), in bursts at the pulse rate (2 Hz); and double vision, a ghost copy
# offset and drifting with a violet-blue tint (E9). Strength 0..1 is written by UPlayerSuppressionComponent.

SCREEN_CODE = """
float2 vuv = GetViewportUV(Parameters);
float s = saturate(Strength);
float ph = frac(T * PulseRate);
float burst = ph < BurstShare ? 1.0 : Calm;
float row = floor(vuv.y * Rows);
float tick = floor(T * Rate);
float r1 = frac(sin(row * 12.9898 + tick * 78.233) * 43758.5453);
float r2 = frac(sin(row * 39.3468 + tick * 11.135) * 24634.6345);
float band = step(1.0 - Density * s * burst, r1);
float shift = (r2 - 0.5) * Shift * band * s;
float split = (Split + band * Split * 2.0) * s;
float2 uv = vuv + float2(shift, 0.0);
float3 col;
col.r = SceneTextureLookup(ViewportUVToBufferUV(saturate(uv + float2(split, 0.0))), 14, false).r;
col.g = SceneTextureLookup(ViewportUVToBufferUV(saturate(uv)), 14, false).g;
col.b = SceneTextureLookup(ViewportUVToBufferUV(saturate(uv - float2(split, 0.0))), 14, false).b;
float2 go = float2(GhostOffset + sin(T * 0.9) * GhostDrift, cos(T * 0.7) * GhostDrift * 0.4) * s;
float3 ghost = SceneTextureLookup(ViewportUVToBufferUV(saturate(vuv + go)), 14, false).rgb;
float g = GhostAmount * s * (0.75 + 0.25 * cos(6.2832 * PulseRate * T));
col = lerp(col, ghost * GhostTint.rgb, g);
float st = frac(sin(dot(vuv * 913.0, float2(12.9898, 78.233)) + T * 37.0) * 43758.5453);
col += band * s * StaticAmount * st * StaticTint.rgb;
return col;
"""
SCREEN_SCALARS = {"Strength": 0.0, "PulseRate": 2.0, "BurstShare": 0.3, "Calm": 0.25, "Rows": 70.0, "Rate": 18.0,
                  "Density": 0.18, "Shift": 0.03, "Split": 0.003, "GhostOffset": 0.012, "GhostDrift": 0.006,
                  "GhostAmount": 0.35, "StaticAmount": 0.15}
SCREEN_VECTORS = {"GhostTint": (0.75, 0.75, 1.25, 1.0), "StaticTint": (0.6, 0.3, 1.0, 1.0)}
PLAYER_BP = "/Game/Variant_Shooter/Blueprints/BP_ShooterCharacter"


def screen():
    path = OUT + "/M_Microwave_Screen"
    mat = unreal.load_asset(path)
    if mat:
        for e in MEL.get_material_expressions(mat):
            MEL.delete_material_expression(mat, e)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset("M_Microwave_Screen", OUT, unreal.Material, unreal.MaterialFactoryNew())
        print("CREATED:", path)
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    names = ["T", "Scene"] + list(SCREEN_VECTORS) + list(SCREEN_SCALARS)
    c = _custom(mat, SCREEN_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, names, -300, 0, "MicrowaveScreen")
    MEL.connect_material_expressions(_node(mat, unreal.MaterialExpressionTime, -900, -300), "", c, "T")
    # Present only so the material reads the scene: the lookups in the code go through it.
    st = _node(mat, unreal.MaterialExpressionSceneTexture, -900, -220)
    st.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    MEL.connect_material_expressions(st, "Color", c, "Scene")
    _params(mat, c, SCREEN_SCALARS, SCREEN_VECTORS, 0)
    MEL.connect_material_property(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    _finish_material(path, mat)
    try:
        print("DIAG:", unreal.MaterialNodeService.get_material_diagnostics(path))
    except Exception as e:
        print("DIAG unavailable:", e)

    bp = unreal.load_asset(PLAYER_BP)
    cdo = unreal.get_default_object(bp.generated_class())
    comp = cdo.get_editor_property("suppression_component")
    comp.set_editor_property("headache_material", unreal.load_asset(path))
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    cdo = unreal.get_default_object(bp.generated_class())
    print("MODIFIED: player headache material", cdo.get_editor_property("suppression_component").get_editor_property("headache_material"))
    print("SAVED:", EAL.save_asset(PLAYER_BP, only_if_is_dirty=False))
