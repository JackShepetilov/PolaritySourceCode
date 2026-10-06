"""Tracer head: a glowing dot at the head of the trail, the "bullet" that still reads far away when the
line itself is a hair. One local-space sprite per system (it sits on the component, which
UTracerSubsystem moves to the head every frame), killed at once when the tracer stops while the trail
fades out. Size = max(HeadSize cm, distance to camera * HeadMinAngleDeg).

Tuning in NS_Tracer_<X> -> User Parameters: HeadSize (cm), HeadMinAngleDeg. Look: M_TracerHead.
Refuses to run while PIE is up (editing a live Niagara system crashed the editor once).
"""

import json
import sys

import unreal

sys.path.insert(0, "C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/DamagePlayground")
import niagara_lib as nl  # noqa: E402

FOLDER = "/Game/Effects/Tracers"
TEMPLATE = "/Niagara/DefaultAssets/Templates/Emitters/SingleLoopingParticle.SingleLoopingParticle"
DYNAMIC = nl.DYNAMIC
LINKED = nl.LINKED
ENUM = "/Script/NiagaraEditor.NiagaraExt_StackInputData_Enum"
FLOAT = {"refPath": nl.FLOAT}
V2 = {"refPath": "/Script/CoreUObject.Vector2f"}
POS = {"refPath": "/Script/Niagara.NiagaraPosition"}
MAX_FLOAT = "/Niagara/DynamicInputs/Math/Max_Float.Max_Float"
MULTIPLY = "/Niagara/DynamicInputs/Multiply/Multiply_Float.Multiply_Float"
DIST_BETWEEN = "/Niagara/DynamicInputs/Vectors/Position/DistanceBetweenPositions.DistanceBetweenPositions"
CAMERA_POS = "/Niagara/DynamicInputs/Vectors/Position/ReturnCameraPosition.ReturnCameraPosition"
VEC2_FROM_FLOAT = "/Niagara/DynamicInputs/TypeConversions/Vector2DFromFloat.Vector2DFromFloat"
EM = "Head"

HEADS = {
    "Bullet": {"HeadSize": 8.0, "HeadMinAngleDeg": 0.15},
    "Sniper": {"HeadSize": 12.0, "HeadMinAngleDeg": 0.2},
    "Pellet": {"HeadSize": 5.0, "HeadMinAngleDeg": 0.1},
}

MAT = "M_TracerHead"
PIXEL_CODE = """
float r = length(UV - 0.5) * 2.0;
float glow = exp(-r * r * GlowFalloff) * GlowStrength;
float core = exp(-r * r * CoreSharp);
float3 c = (Color * glow + CoreColor * core * CoreBoost) * Intensity;
return c * PColor.rgb * PColor.a;
"""
SCALARS = {"Intensity": 40.0, "CoreBoost": 2.0, "CoreSharp": 25.0, "GlowFalloff": 6.0, "GlowStrength": 1.0}
VECTORS = {"Color": (1.0, 0.45, 0.12, 1.0), "CoreColor": (1.0, 0.9, 0.75, 1.0)}


def build_material():
    mel = unreal.MaterialEditingLibrary
    path = FOLDER + "/" + MAT
    mat = unreal.load_asset(path)
    if mat:
        for e in mel.get_material_expressions(mat):
            mel.delete_material_expression(mat, e)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(MAT, FOLDER, unreal.Material, unreal.MaterialFactoryNew())
        print("CREATED:", path)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("used_with_niagara_sprites", True)
    c = mel.create_material_expression(mat, unreal.MaterialExpressionCustom, -300, 0)
    c.set_editor_property("code", PIXEL_CODE)
    c.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    c.set_editor_property("description", "TracerHead")
    names = ["UV", "PColor", "Color", "CoreColor"] + list(SCALARS)
    ins = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        ins.append(ci)
    c.set_editor_property("inputs", ins)
    mel.connect_material_expressions(mel.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -700, 0), "", c, "UV")
    mel.connect_material_expressions(mel.create_material_expression(mat, unreal.MaterialExpressionParticleColor, -700, 100), "RGBA", c, "PColor")
    y = 250
    for n, v in VECTORS.items():
        p = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -700, y)
        p.set_editor_property("parameter_name", n)
        p.set_editor_property("default_value", unreal.LinearColor(*v))
        mel.connect_material_expressions(p, "", c, n)
        y += 160
    for n, v in SCALARS.items():
        p = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, y)
        p.set_editor_property("parameter_name", n)
        p.set_editor_property("default_value", v)
        mel.connect_material_expressions(p, "", c, n)
        y += 90
    mel.connect_material_property(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    print("SAVED material:", unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False))
    return path


def ref(path, script="", module="", stack=None):
    return {"system": nl.sysref(path), "emitterName": EM, "scriptName": script, "moduleName": module,
            "rendererIndex": -1, "inputNameStack": stack or []}


def put(path, script, module, stack, data, label):
    err, _ = nl.call("SetStackInputData", {"stackInputRef": ref(path, script, module, stack), "inputData": data})
    if err:
        print("ERR:", label, "|", err)


def build(name, cfg, mat_path):
    path = FOLDER + "/NS_Tracer_" + name
    if EM not in nl.emitter_names(path):
        err, _ = nl.call("AddEmitter", {"system": nl.sysref(path), "templateEmitter": {"refPath": TEMPLATE}, "emitterName": EM})
        print("ADDED: emitter Head | err:", err or "none")

    for user, v in cfg.items():
        nl.call("RemoveUserVariables", {"system": nl.sysref(path), "variablesToRemove": [{"name": user, "type": {"classStructOrEnum": FLOAT}}]})
        nl.add_user_float(path, user, v)

    # Local space: the dot rides the component, which the tracer subsystem moves to the head.
    err, d = nl.call("GetEmitterData", {"emitterRef": ref(path)})
    props = json.loads(d["returnValue"]["propertyValues"])
    props.update({"bLocalSpace": True, "SimTarget": "CPUSim", "CalculateBoundsMode": "Fixed",
                  "FixedBounds": {"min": {"x": -200, "y": -200, "z": -200}, "max": {"x": 200, "y": 200, "z": 200}, "isValid": True}})
    err, _ = nl.call("SetEmitterData", {"emitter": ref(path), "emitterData": {"propertyValues": json.dumps(props)}})
    print("MODIFIED: emitter data | err:", err or "none")

    # Lives as long as the tracer, dies at once when it stops (the trail fades on its own).
    put(path, "EmitterUpdateScript", "EmitterState", ["Life Cycle Mode"],
        {"struct": {"refPath": ENUM}, "value": {"enum": {"refPath": "/Niagara/Enums/ENiagaraEmitterLifeCycleMode.ENiagaraEmitterLifeCycleMode"},
                                                "enumName": "ENiagaraEmitterLifeCycleMode::NewEnumerator1"}}, "Life Cycle = Self")
    put(path, "EmitterUpdateScript", "EmitterState", ["Loop Duration"], {"struct": FLOAT, "value": {"value": 15.0}}, "Loop Duration")
    put(path, "EmitterUpdateScript", "EmitterState", ["Inactive Response"],
        {"struct": {"refPath": ENUM}, "value": {"enum": {"refPath": "/Niagara/Enums/ENiagaraInactiveMode.ENiagaraInactiveMode"},
                                                "enumName": "ENiagaraInactiveMode::NewEnumerator1"}}, "Inactive = Kill")

    # Size every frame: Vector2DFromFloat(Max(HeadSize, distance(head, camera) * HeadMinAngleDeg * DegToRad)).
    err, d = nl.call("GetEmitterInputValues", {"emitterRef": {"system": nl.sysref(path), "emitterName": EM}})
    mods = {m["moduleName"]: [i.get("name") for i in m.get("inputs", [])] for m in d["returnValue"]}
    found = [m for m, ins in mods.items() if m.startswith("SetVariables_") and "Particles.SpriteSize" in ins]
    if found:
        mod = found[0]
    else:
        err, d = nl.call("AddSetParametersModule", {
            "moduleLocationRef": ref(path, "ParticleUpdateScript", "ParticleState"),
            "parameters": [{"variable": {"name": "Particles.SpriteSize", "type": {"classStructOrEnum": V2}},
                            "defaultValue": {"struct": V2, "value": {"x": 8.0, "y": 8.0}}}]})
        mod = d["returnValue"]["moduleName"]
        print("ADDED: size module", mod, "| err:", err or "none")
    s = ["Particles.SpriteSize"]

    def dyn(stack, asset):
        put(path, "ParticleUpdateScript", mod, s + stack,
            {"struct": {"refPath": DYNAMIC}, "value": {"dynamicInputAsset": {"refPath": asset}}}, "/".join(stack) + " dyn")

    def link(stack, var, vtype=FLOAT):
        put(path, "ParticleUpdateScript", mod, s + stack,
            {"struct": {"refPath": LINKED}, "value": {"linkedVariable": {"name": var, "type": {"classStructOrEnum": vtype}}}},
            "/".join(stack) + " <- " + var)

    def args(stack):
        err, d = nl.call("GetDynamicInputChain", {"stackInputRef": ref(path, "ParticleUpdateScript", mod, s + stack)})
        return [i["value"]["name"] for i in d["returnValue"]["value"]["inputs"]]

    dyn([], VEC2_FROM_FLOAT)
    v2 = args([])
    print("  Vector2DFromFloat args:", v2)
    f = [v2[0]]
    dyn(f, MAX_FLOAT)
    ma, mb = args(f)[:2]
    link(f + [ma], "User.HeadSize")
    dyn(f + [mb], MULTIPLY)
    dyn(f + [mb, "A"], DIST_BETWEEN)
    link(f + [mb, "A", "A"], "Engine.Owner.Position", POS)
    dyn(f + [mb, "A", "B"], CAMERA_POS)
    dyn(f + [mb, "B"], MULTIPLY)
    link(f + [mb, "B", "A"], "User.HeadMinAngleDeg")
    link(f + [mb, "B", "B"], "User.DegToRad")

    rref = {"system": nl.sysref(path), "emitterName": EM, "rendererIndex": 0}
    err, d = nl.call("GetRendererData", {"rendererRef": rref})
    rp = json.loads(d["returnValue"]["propertyValues"])
    rp["Material"] = {"refPath": mat_path + "." + mat_path.split("/")[-1]}
    err, _ = nl.call("SetRendererData", {"renderer": rref, "rendererData": {"propertyValues": json.dumps(rp)}})
    print("MODIFIED: head material | err:", err or "none")

    err, d = nl.call("GetStackIssues", {"system": nl.sysref(path)})
    print("ISSUES:", err or "", str(d)[:300])
    print("SAVED:", unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False))


if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
    raise RuntimeError("PIE is running: editing a live Niagara system crashed the editor once (Docs/Gotchas/Niagara.md)")

MAT_PATH = build_material()
for n, c in HEADS.items():
    print("=====", n)
    build(n, c, MAT_PATH)
