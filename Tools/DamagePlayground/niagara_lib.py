"""Мелкие обёртки над NiagaraToolsets.NiagaraToolset_System для сборки эффектов попадания.

Формы вызовов проверены 2026-10-04 на NS_Impact_ArmorPlate. Непроверенную форму слать по одной
за скрипт (Docs/Gotchas/Niagara.md): пачка роняла редактор.
"""

import json

import unreal

TS = "NiagaraToolsets.NiagaraToolset_System"
FLOAT = "/Script/Niagara.NiagaraFloat"
INT = "/Script/Niagara.NiagaraInt32"


def call(tool, args):
    r = unreal.ToolsetRegistry.execute_tool(TS, tool, json.dumps(args))
    txt = r.get_value_as_json_string() if not r.error else ""
    d = None
    if txt:
        d = json.loads(txt)
        while isinstance(d, str):
            d = json.loads(d)
    return r.error, d


def sysref(path):
    return {"refPath": path + "." + path.split("/")[-1]}


def input_ref(path, emitter, script, module, stack):
    return {"system": sysref(path), "emitterName": emitter, "scriptName": script, "moduleName": module,
            "rendererIndex": -1, "inputNameStack": stack}


def set_number(path, emitter, script, module, name, value, struct=FLOAT):
    err, _ = call("SetStackInputData", {
        "stackInputRef": input_ref(path, emitter, script, module, [name]),
        "inputData": {"struct": {"refPath": struct}, "value": {"value": value}}})
    print("MODIFIED:", emitter, module, name, "=", value, "| err:", err or "none")


def get_input(path, emitter, script, module, stack):
    err, d = call("GetStackInputData", {"stackInputRef": input_ref(path, emitter, script, module, stack)})
    return err, d


def set_renderer(path, emitter, changes, index=0):
    ref = {"system": sysref(path), "emitterName": emitter, "rendererIndex": index}
    err, d = call("GetRendererData", {"rendererRef": ref})
    props = json.loads(d["returnValue"]["propertyValues"])
    props.update(changes)
    err, _ = call("SetRendererData", {"renderer": ref, "rendererData": {"propertyValues": json.dumps(props)}})
    print("MODIFIED:", emitter, "renderer", sorted(changes.keys()), "| err:", err or "none")


BURST_TEMPLATE = "/Niagara/DefaultAssets/Templates/Emitters/SimpleSpriteBurst.SimpleSpriteBurst"
RANDOM_FLOAT = "/Niagara/DynamicInputs/UniformRange/V2/RandomRangeFloat.RandomRangeFloat"
VELOCITY_CONE = "/Niagara/Modules/Spawn/Velocity/AddVelocityInCone.AddVelocityInCone"
GRAVITY = "/Niagara/Modules/Update/Forces/GravityForce.GravityForce"


def emitter_names(path):
    err, d = call("GetSystemSummary", {"system": sysref(path)})
    return [e["emitterName"] for e in d["returnValue"]["emitters"]]


def module_names(path, emitter):
    err, d = call("GetEmitterInputValues", {"emitterRef": {"system": sysref(path), "emitterName": emitter}})
    return [m["moduleName"] for m in d["returnValue"]]


def add_module(path, emitter, script, after, asset):
    err, d = call("AddModule", {"moduleLocationRef": input_ref(path, emitter, script, after, []),
                                "moduleAsset": {"refPath": asset}})
    print("ADDED:", emitter, asset.split(".")[-1], "| err:", err or "none")


def random_subimage(path, emitter, frames):
    """Один случайный кадр атласа на частицу, выбранный при рождении (не в апдейте: там он мигает)."""
    f = {"refPath": FLOAT}
    err, d = call("AddSetParametersModule", {
        "moduleLocationRef": input_ref(path, emitter, "ParticleSpawnScript", "InitializeParticle", []),
        "parameters": [{"variable": {"name": "Particles.SubImageIndex", "type": {"classStructOrEnum": f}},
                        "defaultValue": {"struct": f, "value": {"value": 0.0}}}]})
    mod = d["returnValue"]["moduleName"]
    ref = input_ref(path, emitter, "ParticleSpawnScript", mod, ["Particles.SubImageIndex"])
    err, _ = call("SetStackInputData", {"stackInputRef": ref, "inputData": {
        "struct": {"refPath": "/Script/NiagaraEditor.NiagaraExt_StackInputData_DynamicInput"},
        "value": {"dynamicInputAsset": {"refPath": RANDOM_FLOAT}}}})
    ref["inputNameStack"] = ["Particles.SubImageIndex", "Maximum"]
    err2, _ = call("SetStackInputData", {"stackInputRef": ref, "inputData": {
        "struct": {"refPath": FLOAT}, "value": {"value": float(frames)}}})
    print("ADDED:", emitter, "random sub-image 0..{}".format(frames), "| err:", err or err2 or "none")


def add_burst(path, name, count, life, size, material, grid=1, speed=0.0, cone=45.0, gravity=False):
    """Разовый выброс спрайтов: CPU, без света и коллизий. Ось конуса это локальный X системы,
    а эффект попадания ставится поворотом по нормали, то есть X смотрит от поверхности."""
    if name in emitter_names(path):
        print("EXISTS:", name, "in", path.split("/")[-1])
        return
    err, _ = call("AddEmitter", {"system": sysref(path), "templateEmitter": {"refPath": BURST_TEMPLATE},
                                 "emitterName": name})
    print("ADDED: emitter", name, "| err:", err or "none")
    set_number(path, name, "EmitterUpdateScript", "SpawnBurst_Instantaneous", "Spawn Count", count, INT)
    set_number(path, name, "EmitterUpdateScript", "EmitterState", "Loop Duration", max(life, 0.1) + 0.1)
    set_number(path, name, "ParticleSpawnScript", "InitializeParticle", "Lifetime", life)
    set_number(path, name, "ParticleSpawnScript", "InitializeParticle", "Uniform Sprite Size", size)
    changes = {"Material": {"refPath": material + "." + material.split("/")[-1]}}
    if grid > 1:
        changes["SubImageSize"] = {"x": grid, "y": grid}
    set_renderer(path, name, changes)
    if grid > 1:
        random_subimage(path, name, grid * grid)
    if speed > 0.0:
        add_module(path, name, "ParticleSpawnScript", "InitializeParticle", VELOCITY_CONE)
        set_number(path, name, "ParticleSpawnScript", "AddVelocityInCone", "Velocity Strength", speed)
        set_number(path, name, "ParticleSpawnScript", "AddVelocityInCone", "Cone Angle", cone)
    if gravity:
        add_module(path, name, "ParticleUpdateScript", "ScaleColor", GRAVITY)


def finish(path):
    err, d = call("GetStackIssues", {"system": sysref(path)})
    print("ISSUES:", path.split("/")[-1], err or "", str(d)[:260])
    print("SAVED:", unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False))


def set_random(path, emitter, script, module, name, lo, hi):
    """Число из диапазона на каждую частицу: без этого все куски летят с одной скоростью и идут кольцом."""
    ref = input_ref(path, emitter, script, module, [name])
    err, _ = call("SetStackInputData", {"stackInputRef": ref, "inputData": {
        "struct": {"refPath": "/Script/NiagaraEditor.NiagaraExt_StackInputData_DynamicInput"},
        "value": {"dynamicInputAsset": {"refPath": RANDOM_FLOAT}}}})
    errs = [err]
    for arg, val in (("Minimum", lo), ("Maximum", hi)):
        ref["inputNameStack"] = [name, arg]
        e, _ = call("SetStackInputData", {"stackInputRef": ref, "inputData": {
            "struct": {"refPath": FLOAT}, "value": {"value": float(val)}}})
        errs.append(e)
    print("MODIFIED:", emitter, module, name, "= random", lo, "..", hi, "| err:", [e for e in errs if e] or "none")


MULTIPLY_FLOAT = "/Niagara/DynamicInputs/Multiply/Multiply_Float.Multiply_Float"
DYNAMIC = "/Script/NiagaraEditor.NiagaraExt_StackInputData_DynamicInput"
LINKED = "/Script/NiagaraEditor.NiagaraExt_StackInputData_Linked"


def remove_emitter(path, emitter):
    err, _ = call("RemoveEmitter", {"emitterToRemove": {
        "system": sysref(path), "emitterName": emitter, "scriptName": "", "moduleName": "",
        "rendererIndex": -1, "inputNameStack": []}})
    print("DELETED: emitter", emitter, "from", path.split("/")[-1], "| err:", err or "none")


def add_user_float(path, name, default=1.0, description=""):
    f = {"refPath": FLOAT}
    err, _ = call("AddUserVariables", {"system": sysref(path), "variablesToAdd": [{
        "name": name, "type": {"classStructOrEnum": f}, "description": description,
        "defaultValue": {"struct": f, "value": {"value": float(default)}}}]})
    print("ADDED: user variable", name, "on", path.split("/")[-1], "| err:", err or "none")


def scale_by_user(path, emitter, script, module, name, lo, hi, user="User.ImpactScale"):
    """name = random(lo..hi) * user. Так размер от урона (AShooterWeapon::ResolveImpactScale) доходит
    до частицы: оружие пишет user-параметр ImpactScale, а размер спрайта на него умножается."""
    ref = input_ref(path, emitter, script, module, [name])
    errs = []

    def put(stack, data):
        ref["inputNameStack"] = stack
        e, _ = call("SetStackInputData", {"stackInputRef": ref, "inputData": data})
        errs.append(e)

    put([name], {"struct": {"refPath": DYNAMIC}, "value": {"dynamicInputAsset": {"refPath": MULTIPLY_FLOAT}}})
    put([name, "A"], {"struct": {"refPath": DYNAMIC}, "value": {"dynamicInputAsset": {"refPath": RANDOM_FLOAT}}})
    put([name, "A", "Minimum"], {"struct": {"refPath": FLOAT}, "value": {"value": float(lo)}})
    put([name, "A", "Maximum"], {"struct": {"refPath": FLOAT}, "value": {"value": float(hi)}})
    put([name, "B"], {"struct": {"refPath": LINKED}, "value": {
        "linkedVariable": {"name": user, "type": {"classStructOrEnum": {"refPath": FLOAT}}}}})
    print("MODIFIED:", emitter, name, "= random {}..{} x {}".format(lo, hi, user), "| err:", [e for e in errs if e] or "none")
