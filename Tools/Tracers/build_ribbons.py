"""Apex-style tracer trails: NS_Tracer_* become a world-space ribbon that rides the round and fades.

Tuning, no rebuild needed:
  - NS_Tracer_<X> -> User Parameters: TrailLength (cm behind the round), TrailWidth (cm), TrailMinAngleDeg.
    TrailSpeed comes from C++, DegToRad is a constant (see TRAILS).
  - MI_TracerRibbon_<X>: Intensity, Color, CoreColor, CoreBoost, CoreSharp, GlowFalloff, GlowStrength.
Own-shot origin at the visible barrel: AShooterWeapon::SpawnProjectileAtTransform (C++).
Reference: Apex weapon scripts (projectile_trail_effect_0 on the round, projectile_adjust_to_gun_barrel).
"""

import json
import sys

import unreal

sys.path.insert(0, "C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/DamagePlayground")
import niagara_lib as nl  # noqa: E402

FOLDER = "/Game/Effects/Tracers"
TEMPLATE = "/Niagara/DefaultAssets/Templates/Emitters/LocationBasedRibbon.LocationBasedRibbon"
SPAWN_RATE = "/Niagara/Modules/Emitter/SpawnRate.SpawnRate"
SCALE_COLOR = "/Niagara/Modules/Update/Color/ScaleColor.ScaleColor"
DIVIDE = "/Niagara/DynamicInputs/Divide/Divide_Float.Divide_Float"
NORMALIZE_DISTANCE = "/Niagara/DynamicInputs/Helpers/NormalizeDistanceRange.NormalizeDistanceRange"
DIST_TO_CAMERA = "/Niagara/DynamicInputs/Camera/DistanceToCamera.DistanceToCamera"
DIST_BETWEEN = "/Niagara/DynamicInputs/Vectors/Position/DistanceBetweenPositions.DistanceBetweenPositions"
CAMERA_POS = "/Niagara/DynamicInputs/Vectors/Position/ReturnCameraPosition.ReturnCameraPosition"
REMAP = "/Niagara/DynamicInputs/Clamp/Float/RemapRange.RemapRange"
MAX_FLOAT = "/Niagara/DynamicInputs/Math/Max_Float.Max_Float"
MAKE_VEC4 = "/Niagara/DynamicInputs/TypeConversions/MakeVector4.MakeVector4"
MULTIPLY = "/Niagara/DynamicInputs/Multiply/Multiply_Float.Multiply_Float"
BOOL = {"refPath": "/Script/Niagara.NiagaraBool"}
ONE_MINUS = "/Niagara/DynamicInputs/Math/OneMinusFloat.OneMinusFloat"
ENUM = "/Script/NiagaraEditor.NiagaraExt_StackInputData_Enum"
LINKED = nl.LINKED
DYNAMIC = nl.DYNAMIC
FLOAT = {"refPath": nl.FLOAT}
EM = "Trail"

TRAILS = {
    # Tuned by hand in NS_Tracer_<X> -> User Parameters:
    #   TrailLength: cm behind the round where the trail has faded to nothing (distance, not time).
    #   TrailWidth: cm in the world. What you see near the barrel.
    #   TrailMinAngleDeg: the thinnest it may look, as an angle of view (0.056 deg = 1 px at 1080p, 108 deg FOV),
    #     so far away it does not vanish. A pure angle width made the far end, seen nearly end-on, pile up
    #     into a bright "star" when shooting into the sky.
    # The look along the trail (point, rear width/fade, colour) lives in M_TracerRibbon / MI_TracerRibbon_<X>.
    # Set by code / constants: TrailSpeed (cm/s, per shot from C++; cleanup lifetime only), DegToRad.
    "Bullet": {"TrailLength": 10000.0, "TrailSpeed": 73660.0, "TrailWidth": 2.0, "TrailMinAngleDeg": 0.06, "mi": {"Intensity": 40.0, "CoreBoost": 2.0}},
    "Sniper": {"TrailLength": 20000.0, "TrailSpeed": 78740.0, "TrailWidth": 3.0, "TrailMinAngleDeg": 0.08, "mi": {"Intensity": 60.0, "CoreBoost": 2.5}},
    "Pellet": {"TrailLength": 3000.0, "TrailSpeed": 40640.0, "TrailWidth": 1.5, "TrailMinAngleDeg": 0.04, "mi": {"Intensity": 15.0, "CoreBoost": 1.5}},
}
DEG_TO_RAD = 0.0174533


def ref(path, script="", module="", stack=None):
    return {"system": nl.sysref(path), "emitterName": EM, "scriptName": script, "moduleName": module,
            "rendererIndex": -1, "inputNameStack": stack or []}


def put(path, script, module, stack, data, label):
    err, _ = nl.call("SetStackInputData", {"stackInputRef": ref(path, script, module, stack), "inputData": data})
    print("MODIFIED:", label, "| err:", err or "none")


def enum(path, module, name, enum_path, value, label):
    put(path, "EmitterUpdateScript", module, [name],
        {"struct": {"refPath": ENUM}, "value": {"enum": {"refPath": enum_path}, "enumName": value}}, label)


def link_user(path, script, module, name, user):
    put(path, script, module, [name],
        {"struct": {"refPath": LINKED}, "value": {"linkedVariable": {"name": user, "type": {"classStructOrEnum": FLOAT}}}},
        "%s <- %s" % (name, user))


def topology(path):
    err, d = nl.call("GetEmitterTopology", {"emitterRef": ref(path)})
    rv = d["returnValue"]
    return {k: [m["moduleName"] for m in v["modules"]] for k, v in rv.items() if isinstance(v, dict) and "modules" in v}


def ensure_mi(name, params):
    path = FOLDER + "/MI_TracerRibbon_" + name
    mi = unreal.load_asset(path)
    if mi is None:
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "MI_TracerRibbon_" + name, FOLDER, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mi.set_editor_property("parent", unreal.load_asset(FOLDER + "/M_TracerRibbon"))
        print("CREATED:", path)
    for k, v in params.items():
        unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi, k, v)
    unreal.MaterialEditingLibrary.update_material_instance(mi)
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    return path


def build(name, cfg):
    path = FOLDER + "/NS_Tracer_" + name
    names = nl.emitter_names(path)
    if "Tracer" in names:  # the old sprite streak
        err, _ = nl.call("RemoveEmitter", {"emitterToRemove": {
            "system": nl.sysref(path), "emitterName": "Tracer", "scriptName": "", "moduleName": "",
            "rendererIndex": -1, "inputNameStack": []}})
        print("DELETED: sprite emitter | err:", err or "none")
    if EM not in names:
        err, _ = nl.call("AddEmitter", {"system": nl.sysref(path), "templateEmitter": {"refPath": TEMPLATE}, "emitterName": EM})
        print("ADDED: ribbon emitter | err:", err or "none")

    # No setter for a user default in the toolset: remove and add again (links are redone below).
    values = dict(cfg, DegToRad=DEG_TO_RAD)
    for user in ("TrailLife", "TrailPixels", "PixelAngle", "TrailAngleDeg", "TrailTailWidth", "TrailLength", "TrailSpeed", "TrailWidth", "TrailMinAngleDeg", "DegToRad"):
        err, _ = nl.call("RemoveUserVariables", {"system": nl.sysref(path), "variablesToRemove": [
            {"name": user, "type": {"classStructOrEnum": FLOAT}}]})
        if user in values:
            nl.add_user_float(path, user, values[user])

    # NO interpolated spawn. Switching it on through SetEmitterData does not rebuild the spawn script
    # as ParticleSpawnScriptInterpolated ("spawn script Usage is ParticleSpawnScript" in the log) and the
    # emitter stops spawning altogether. The first point at the muzzle comes from C++ instead:
    # AShooterWeapon::SpawnProjectileAtTransform advances the fresh trail one step before the round moves.
    err, d = nl.call("GetEmitterData", {"emitterRef": ref(path)})
    eprops = json.loads(d["returnValue"]["propertyValues"])
    eprops["InterpolatedSpawnMode"] = "NoInterpolation"
    err, _ = nl.call("SetEmitterData", {"emitter": ref(path), "emitterData": {"propertyValues": json.dumps(eprops)}})
    print("MODIFIED: InterpolatedSpawnMode = NoInterpolation | err:", err or "none")

    topo = topology(path)
    if "SpawnRate" not in topo["emitterUpdateScript"]:
        err, _ = nl.call("AddModule", {"moduleLocationRef": ref(path, "EmitterUpdateScript", "EmitterState"),
                                       "moduleAsset": {"refPath": SPAWN_RATE}})
        print("ADDED: SpawnRate | err:", err or "none")
    if "ScaleColor" not in topo["particleUpdateScript"]:
        err, _ = nl.call("AddModule", {"moduleLocationRef": ref(path, "ParticleUpdateScript", "ParticleState"),
                                       "moduleAsset": {"refPath": SCALE_COLOR}})
        print("ADDED: ScaleColor | err:", err or "none")

    # Runs while the round flies, dies at once when the round goes back to the pool.
    enum(path, "EmitterState", "Life Cycle Mode", "/Niagara/Enums/ENiagaraEmitterLifeCycleMode.ENiagaraEmitterLifeCycleMode",
         "ENiagaraEmitterLifeCycleMode::NewEnumerator1", "Life Cycle = Self")
    enum(path, "EmitterState", "Loop Behavior", "/Niagara/Enums/ENiagara_EmitterStateOptions.ENiagara_EmitterStateOptions",
         "ENiagara_EmitterStateOptions::NewEnumerator0", "Loop = Infinite")
    enum(path, "EmitterState", "Inactive Response", "/Niagara/Enums/ENiagaraInactiveMode.ENiagaraInactiveMode",
         "ENiagaraInactiveMode::NewEnumerator0", "Inactive = Complete (trail fades out)")
    put(path, "EmitterUpdateScript", "SpawnRate", ["SpawnRate"], {"struct": FLOAT, "value": {"value": 240.0}}, "SpawnRate = 240")

    # Lifetime of each trail point = TrailLength / TrailSpeed, so the trail is TrailLength long.
    put(path, "ParticleSpawnScript", "InitializeParticle", ["Lifetime"],
        {"struct": {"refPath": DYNAMIC}, "value": {"dynamicInputAsset": {"refPath": DIVIDE}}}, "Lifetime = Divide_Float")
    err, d = nl.call("GetDynamicInputChain", {"stackInputRef": ref(path, "ParticleSpawnScript", "InitializeParticle", ["Lifetime"])})
    dargs = [i["value"]["name"] for i in d["returnValue"]["value"]["inputs"]]
    print("  Divide_Float args:", dargs)
    for arg, user in zip(dargs[:2], ("User.TrailLength", "User.TrailSpeed")):
        put(path, "ParticleSpawnScript", "InitializeParticle", ["Lifetime", arg],
            {"struct": {"refPath": LINKED}, "value": {"linkedVariable": {"name": user, "type": {"classStructOrEnum": FLOAT}}}},
            "  %s <- %s" % (arg, user))
    # Width is written every frame in the update script (screen pixels), so nothing at spawn.
    put(path, "ParticleSpawnScript", "InitializeParticle", ["Ribbon Width Mode"],
        {"struct": {"refPath": ENUM}, "value": {"enum": {"refPath": "/Niagara/Enums/Ribbons/ENiagara_UnsetDirectSet.ENiagara_UnsetDirectSet"},
                                                "enumName": "ENiagara_UnsetDirectSet::NewEnumerator0"}}, "Ribbon Width Mode = Unset")

    # Alpha along the trail is the material's (RearFadeStart/RearFadePower); the system keeps 1.
    put(path, "ParticleUpdateScript", "ScaleColor", ["Scale Alpha"], {"struct": FLOAT, "value": {"value": 1.0}}, "Scale Alpha = 1")

    # Width = Max(TrailWidth, distance-to-camera * min angle). The rear profile is drawn by the material
    # inside this width (RearWidthStart/End/Power), so the ribbon itself stays one width.
    err, d = nl.call("GetEmitterInputValues", {"emitterRef": {"system": nl.sysref(path), "emitterName": EM}})
    mods = {m["moduleName"]: [i.get("name") for i in m.get("inputs", [])] for m in d["returnValue"]}
    found = [m for m, ins in mods.items() if m.startswith("SetVariables_") and "Particles.RibbonWidth" in ins]
    if found:
        wmod = found[0]
    else:
        err, d = nl.call("AddSetParametersModule", {
            "moduleLocationRef": ref(path, "ParticleUpdateScript", "ScaleColor"),
            "parameters": [{"variable": {"name": "Particles.RibbonWidth", "type": {"classStructOrEnum": FLOAT}},
                            "defaultValue": {"struct": FLOAT, "value": {"value": 1.0}}}]})
        wmod = d["returnValue"]["moduleName"]
        print("ADDED: width module", wmod, "| err:", err or "none")
    w = ["Particles.RibbonWidth"]

    def dyn(stack, asset, label):
        put(path, "ParticleUpdateScript", wmod, w + stack,
            {"struct": {"refPath": DYNAMIC}, "value": {"dynamicInputAsset": {"refPath": asset}}}, label)

    def link(stack, var, vtype=FLOAT):
        put(path, "ParticleUpdateScript", wmod, w + stack,
            {"struct": {"refPath": LINKED}, "value": {"linkedVariable": {"name": var, "type": {"classStructOrEnum": vtype}}}},
            "%s <- %s" % ("/".join(stack), var))

    dyn([], MAX_FLOAT, "RibbonWidth = Max")
    err, d = nl.call("GetDynamicInputChain", {"stackInputRef": ref(path, "ParticleUpdateScript", wmod, w)})
    ma, mb = [i["value"]["name"] for i in d["returnValue"]["value"]["inputs"]][:2]
    link([ma], "User.TrailWidth")
    dyn([mb], MULTIPLY, "  max.%s = Multiply" % mb)
    dyn([mb, "A"], DIST_BETWEEN, "    A = DistanceBetweenPositions")
    link([mb, "A", "A"], "Particles.Position", {"refPath": "/Script/Niagara.NiagaraPosition"})
    dyn([mb, "A", "B"], CAMERA_POS, "      B = ReturnCameraPosition")
    dyn([mb, "B"], MULTIPLY, "    B = Multiply")
    link([mb, "B", "A"], "User.TrailMinAngleDeg")
    link([mb, "B", "B"], "User.DegToRad")

    # Shape data for M_TracerRibbon, per point: x = distance to the head (comet front, FrontLength),
    # y = distance to where the trail started (soft end near the shooter, TailFadeLength). The path is
    # straight, so both interpolate exactly along each segment: the material knows every pixel in cm.
    # User.TrailStart is written by UTracerSubsystem::StartTracer.
    POSV = {"refPath": "/Script/Niagara.NiagaraPosition"}
    V4 = {"refPath": "/Script/CoreUObject.Vector4f"}
    nl.call("RemoveUserVariables", {"system": nl.sysref(path), "variablesToRemove": [{"name": "TrailStart", "type": {"classStructOrEnum": POSV}}]})
    err, _ = nl.call("AddUserVariables", {"system": nl.sysref(path), "variablesToAdd": [{
        "name": "TrailStart", "type": {"classStructOrEnum": POSV}, "description": "where the trail starts (C++)",
        "defaultValue": {"struct": POSV, "value": {"x": 0.0, "y": 0.0, "z": 0.0}}}]})
    print("ADDED: User.TrailStart | err:", err or "none")
    err, d = nl.call("GetEmitterInputValues", {"emitterRef": {"system": nl.sysref(path), "emitterName": EM}})
    mods = {m["moduleName"]: [i.get("name") for i in m.get("inputs", [])] for m in d["returnValue"]}
    found = [m for m, ins in mods.items() if m.startswith("SetVariables_") and "Particles.DynamicMaterialParameter" in ins]
    if found:
        dmod = found[0]
    else:
        err, d = nl.call("AddSetParametersModule", {
            "moduleLocationRef": ref(path, "ParticleUpdateScript", "ScaleColor"),
            "parameters": [{"variable": {"name": "Particles.DynamicMaterialParameter", "type": {"classStructOrEnum": V4}},
                            "defaultValue": {"struct": V4, "value": {"x": 0.0, "y": 0.0, "z": 0.0, "w": 0.0}}}]})
        dmod = d["returnValue"]["moduleName"]
        print("ADDED: shape data module", dmod, "| err:", err or "none")
    dp = ["Particles.DynamicMaterialParameter"]
    put(path, "ParticleUpdateScript", dmod, dp,
        {"struct": {"refPath": DYNAMIC}, "value": {"dynamicInputAsset": {"refPath": MAKE_VEC4}}}, "DynamicMaterialParameter = MakeVector4")
    err, d = nl.call("GetDynamicInputChain", {"stackInputRef": ref(path, "ParticleUpdateScript", dmod, dp)})
    v4args = [i["value"]["name"] for i in d["returnValue"]["value"]["inputs"]]
    print("  MakeVector4 args:", v4args)
    put(path, "ParticleUpdateScript", dmod, dp + [v4args[2]],
        {"struct": {"refPath": LINKED}, "value": {"linkedVariable": {"name": "User.TrailLength", "type": {"classStructOrEnum": FLOAT}}}},
        "  %s <- User.TrailLength" % v4args[2])
    for arg, other in ((v4args[0], ("Engine.Owner.Position", POSV)), (v4args[1], ("User.TrailStart", POSV))):
        put(path, "ParticleUpdateScript", dmod, dp + [arg],
            {"struct": {"refPath": DYNAMIC}, "value": {"dynamicInputAsset": {"refPath": DIST_BETWEEN}}}, "  %s = DistanceBetweenPositions" % arg)
        put(path, "ParticleUpdateScript", dmod, dp + [arg, "A"],
            {"struct": {"refPath": LINKED}, "value": {"linkedVariable": {"name": "Particles.Position", "type": {"classStructOrEnum": POSV}}}}, "    A <- Particles.Position")
        put(path, "ParticleUpdateScript", dmod, dp + [arg, "B"],
            {"struct": {"refPath": LINKED}, "value": {"linkedVariable": {"name": other[0], "type": {"classStructOrEnum": other[1]}}}}, "    B <- " + other[0])

    # The head dot (build_head.py) is gone: the author found it spoiled the look.
    if "Head" in nl.emitter_names(path):
        err, _ = nl.call("RemoveEmitter", {"emitterToRemove": {"system": nl.sysref(path), "emitterName": "Head", "scriptName": "",
                                                               "moduleName": "", "rendererIndex": -1, "inputNameStack": []}})
        print("DELETED: Head emitter | err:", err or "none")
    for user in ("HeadSize", "HeadMinAngleDeg"):
        nl.call("RemoveUserVariables", {"system": nl.sysref(path), "variablesToRemove": [{"name": user, "type": {"classStructOrEnum": FLOAT}}]})

    mi = ensure_mi(name, cfg["mi"])
    rref = {"system": nl.sysref(path), "emitterName": EM, "rendererIndex": 0}
    err, d = nl.call("GetRendererData", {"rendererRef": rref})
    props = json.loads(d["returnValue"]["propertyValues"])
    props["Material"] = {"refPath": mi + "." + mi.split("/")[-1]}
    err, _ = nl.call("SetRendererData", {"renderer": rref, "rendererData": {"propertyValues": json.dumps(props)}})
    print("MODIFIED: renderer material | err:", err or "none")

    print("TOPOLOGY:", topology(path))
    err, d = nl.call("GetStackIssues", {"system": nl.sysref(path)})
    print("ISSUES:", err or "", str(d)[:300])
    print("SAVED:", unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False))


if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
    raise RuntimeError("PIE is running: editing a live Niagara system crashed the editor once (Docs/Gotchas/Niagara.md)")

ONLY = globals().get("ONLY")
for n, c in TRAILS.items():
    if not ONLY or n == ONLY:
        print("=====", n)
        build(n, c)
