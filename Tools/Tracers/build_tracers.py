"""Three tracer systems + material instances. Idempotent: structure is added only when missing,
numbers are written every run. Material math: build_material.py. Plan: Docs/Tracers_Plan_2026-10-05.md

Each system: one CPU emitter, local space (rides the bullet), one particle for the whole flight,
Particles.Velocity = bullet velocity in local axes (the sprite aligns to it, the material reads
direction and speed from it), killed at once on Deactivate (pool return).
"""

import json

import unreal

TS = "NiagaraToolsets.NiagaraToolset_System"
FOLDER = "/Game/Effects/Tracers"
TEMPLATE = "/Niagara/DefaultAssets/Templates/Emitters/SingleLoopingParticle.SingleLoopingParticle"
DYNAMIC = "/Script/NiagaraEditor.NiagaraExt_StackInputData_DynamicInput"
LINKED = "/Script/NiagaraEditor.NiagaraExt_StackInputData_Linked"
ENUM = "/Script/NiagaraEditor.NiagaraExt_StackInputData_Enum"
V3 = {"refPath": "/Script/CoreUObject.Vector3f"}
FLOAT = {"refPath": "/Script/Niagara.NiagaraFloat"}
EM = "Tracer"
LIFETIME = 12.0  # > AShooterProjectile::MaxFlightSeconds (10); M_Tracer "Lifetime" must match

# name -> material instance parameters
TRACERS = {
    # TailTime ~ one 60 fps frame: the streak covers the whole gap since the last frame, so frame to
    # frame it reads as one continuous line instead of 4 m dashes hopping 12 m apart.
    "Bullet": {"TailTime": 0.016, "BaseWidth": 4.0, "MinPixels": 2.5, "Intensity": 20.0, "CoreBoost": 1.5, "FarDim": 0.4},
    "Sniper": {"TailTime": 0.020, "BaseWidth": 6.0, "MinPixels": 3.0, "Intensity": 30.0, "CoreBoost": 2.0, "FarDim": 0.5},
    "Pellet": {"TailTime": 0.008, "BaseWidth": 2.5, "MinPixels": 1.5, "Intensity": 8.0, "CoreBoost": 1.0, "FarDim": 0.3},
}


# cm/s ceiling, a little above the fastest round using the system (MG 76200, Marksman 78740, Shotgun 40640)
MAX_SPEED = {"Bullet": 73660.0, "Sniper": 78740.0, "Pellet": 40640.0}  # default TracerVelocity; C++ overrides per shot


def call(tool, args):
    r = unreal.ToolsetRegistry.execute_tool(TS, tool, json.dumps(args))
    s = r.get_value_as_json_string() if not r.error else ""
    d = json.loads(s) if s else None
    while isinstance(d, str):
        d = json.loads(d)
    return r.error, d


def sysref(path):
    return {"refPath": path + "." + path.split("/")[-1]}


def ref(path, script="", module="", stack=None, renderer=-1):
    return {"system": sysref(path), "emitterName": EM, "scriptName": script, "moduleName": module,
            "rendererIndex": renderer, "inputNameStack": stack or []}


def put(path, script, module, stack, data, label):
    err, _ = call("SetStackInputData", {"stackInputRef": ref(path, script, module, stack), "inputData": data})
    print("MODIFIED:", label, "| err:", err or "none")


def modules(path):
    err, d = call("GetEmitterInputValues", {"emitterRef": {"system": sysref(path), "emitterName": EM}})
    return [m["moduleName"] for m in d["returnValue"]]


def ensure_mi(name, params):
    path = FOLDER + "/MI_Tracer_" + name
    # load_asset, not does_asset_exist: the latter once answered False for an existing MI (2026-10-05).
    if unreal.load_asset(path) is None:
        mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            "MI_Tracer_" + name, FOLDER, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mi.set_editor_property("parent", unreal.load_asset(FOLDER + "/M_Tracer"))
        print("CREATED:", path)
    mi = unreal.load_asset(path)
    mel = unreal.MaterialEditingLibrary
    for k, v in dict(params, Lifetime=LIFETIME).items():
        mel.set_material_instance_scalar_parameter_value(mi, k, v)
    mel.update_material_instance(mi)
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    print("MODIFIED:", path, params)
    return path


def ensure_structure(path, max_speed):
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        name = path.split("/")[-1]
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, FOLDER, unreal.NiagaraSystem, unreal.NiagaraSystemFactoryNew())
        print("CREATED:", path)
    err, d = call("GetSystemSummary", {"system": sysref(path)})
    if EM not in [e["emitterName"] for e in d["returnValue"]["emitters"]]:
        err, _ = call("AddEmitter", {"system": sysref(path), "templateEmitter": {"refPath": TEMPLATE}, "emitterName": EM})
        print("ADDED: emitter", EM, "| err:", err or "none")

    # Emitter: local space, CPU, fixed bounds long enough for the tail behind the bullet (-X).
    eref = ref(path)
    err, d = call("GetEmitterData", {"emitterRef": eref})
    props = json.loads(d["returnValue"]["propertyValues"])
    props.update({"bLocalSpace": True, "SimTarget": "CPUSim", "CalculateBoundsMode": "Fixed",
                  "FixedBounds": {"min": {"x": -2500, "y": -200, "z": -200}, "max": {"x": 200, "y": 200, "z": 200}, "isValid": True}})
    err, _ = call("SetEmitterData", {"emitter": eref, "emitterData": {"propertyValues": json.dumps(props)}})
    print("MODIFIED: emitter data | err:", err or "none")

    # User parameters, written by AShooterWeapon::SpawnProjectileAtTransform on every shot:
    #   TracerVelocity (local, (InitialSpeed, 0, 0)): the round flies along its own +X.
    #     Engine.Owner.Velocity is NOT used: it is a position delta and reads the pooled round's jump
    #     to the muzzle as a huge speed in a random direction.
    #   TracerData (xyz = offset from the round to the muzzle the player sees, w = 1 for own shot).
    #     Goes to the material as Particles.DynamicMaterialParameter.
    V4 = {"refPath": "/Script/CoreUObject.Vector4f"}
    err, _ = call("AddUserVariables", {"system": sysref(path), "variablesToAdd": [
        {"name": "TracerVelocity", "type": {"classStructOrEnum": V3}, "description": "local velocity of the round",
         "defaultValue": {"struct": V3, "value": {"x": max_speed, "y": 0.0, "z": 0.0}}},
        {"name": "TracerData", "type": {"classStructOrEnum": V4}, "description": "xyz muzzle offset, w own shot",
         "defaultValue": {"struct": V4, "value": {"x": 0.0, "y": 0.0, "z": 0.0, "w": 0.0}}}]})
    print("ADDED: user vars | err:", err or "none")

    err, d = call("GetEmitterInputValues", {"emitterRef": {"system": sysref(path), "emitterName": EM}})
    mods = {m["moduleName"]: [i.get("name") for i in m.get("inputs", [])] for m in d["returnValue"]}
    if "SolveForcesAndVelocity" in mods:
        err, _ = call("RemoveModule", {"moduleToRemove": ref(path, "ParticleUpdateScript", "SolveForcesAndVelocity")})
        print("DELETED: SolveForcesAndVelocity | err:", err or "none")

    def setvar(var, vtype, zero, user):
        found = [m for m, ins in mods.items() if m.startswith("SetVariables_") and var in ins]
        if found:
            mod = found[0]
        else:
            err, d = call("AddSetParametersModule", {
                "moduleLocationRef": ref(path, "ParticleUpdateScript", "ParticleState"),
                "parameters": [{"variable": {"name": var, "type": {"classStructOrEnum": vtype}},
                                "defaultValue": {"struct": vtype, "value": zero}}]})
            mod = d["returnValue"]["moduleName"]
            print("ADDED: set", var, mod, "| err:", err or "none")
        put(path, "ParticleUpdateScript", mod, [var],
            {"struct": {"refPath": LINKED}, "value": {"linkedVariable": {"name": user, "type": {"classStructOrEnum": vtype}}}},
            "%s <- %s" % (var, user))

    setvar("Particles.Velocity", V3, {"x": 0.0, "y": 0.0, "z": 0.0}, "User.TracerVelocity")
    setvar("Particles.DynamicMaterialParameter", V4, {"x": 0.0, "y": 0.0, "z": 0.0, "w": 0.0}, "User.TracerData")


def configure(path, mi_path):
    # "System" life cycle hides the emitter's own loop and inactive inputs (they come from SystemState).
    put(path, "EmitterUpdateScript", "EmitterState", ["Life Cycle Mode"],
        {"struct": {"refPath": ENUM}, "value": {"enum": {"refPath": "/Niagara/Enums/ENiagaraEmitterLifeCycleMode.ENiagaraEmitterLifeCycleMode"},
                                                "enumName": "ENiagaraEmitterLifeCycleMode::NewEnumerator1"}}, "Life Cycle Mode = Self")
    put(path, "EmitterUpdateScript", "EmitterState", ["Loop Duration"],
        {"struct": FLOAT, "value": {"value": LIFETIME}}, "Loop Duration")
    put(path, "EmitterUpdateScript", "EmitterState", ["Inactive Response"],
        {"struct": {"refPath": ENUM}, "value": {"enum": {"refPath": "/Niagara/Enums/ENiagaraInactiveMode.ENiagaraInactiveMode"},
                                                "enumName": "ENiagaraInactiveMode::NewEnumerator1"}}, "Inactive Response = Kill")
    rref = {"system": sysref(path), "emitterName": EM, "rendererIndex": 0}
    err, d = call("GetRendererData", {"rendererRef": rref})
    props = json.loads(d["returnValue"]["propertyValues"])
    props.update({"Material": {"refPath": mi_path + "." + mi_path.split("/")[-1]},
                  "Alignment": "VelocityAligned", "FacingMode": "FaceCameraPosition"})
    err, _ = call("SetRendererData", {"renderer": rref, "rendererData": {"propertyValues": json.dumps(props)}})
    print("MODIFIED: renderer | err:", err or "none")


def report(path):
    err, d = call("GetStackInputData", {"stackInputRef": ref(path, "EmitterUpdateScript", "EmitterState", ["Inactive Response"])})
    print("CHECK inactive:", d["returnValue"]["value"].get("displayName") if d else err)
    err, d = call("GetRendererData", {"rendererRef": {"system": sysref(path), "emitterName": EM, "rendererIndex": 0}})
    p = json.loads(d["returnValue"]["propertyValues"])
    print("CHECK renderer:", p["Alignment"], p["FacingMode"], p["Material"])
    err, d = call("GetStackIssues", {"system": sysref(path)})
    print("ISSUES:", err or "", str(d)[:400])
    print("SAVED:", unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False))


def main(only=None):
    for name, params in TRACERS.items():
        if only and name != only:
            continue
        print("=====", name)
        mi = ensure_mi(name, params)
        path = FOLDER + "/NS_Tracer_" + name
        ensure_structure(path, MAX_SPEED[name])
        configure(path, mi)
        report(path)


if __name__ == "__main__":
    main(globals().get("ONLY"))
