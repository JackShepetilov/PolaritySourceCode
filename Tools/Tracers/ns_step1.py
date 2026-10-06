import sys, json, unreal
sys.path.insert(0, "C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/DamagePlayground")
import niagara_lib as nl
FOLDER = "/Game/Effects/Tracers"
TEMPLATE = "/Niagara/DefaultAssets/Templates/Emitters/SingleLoopingParticle.SingleLoopingParticle"
path = FOLDER + "/NS_Tracer_Bullet"
if not unreal.EditorAssetLibrary.does_asset_exist(path):
    unreal.AssetToolsHelpers.get_asset_tools().create_asset("NS_Tracer_Bullet", FOLDER, unreal.NiagaraSystem, unreal.NiagaraSystemFactoryNew())
    print("CREATED:", path)
names = nl.emitter_names(path)
print("emitters:", names)
if "Tracer" not in names:
    err, d = nl.call("AddEmitter", {"system": nl.sysref(path), "templateEmitter": {"refPath": TEMPLATE}, "emitterName": "Tracer"})
    print("ADDED emitter Tracer | err:", err or "none")
print("emitters:", nl.emitter_names(path))
err, d = nl.call("GetEmitterInputValues", {"emitterRef": {"system": nl.sysref(path), "emitterName": "Tracer"}})
for m in d["returnValue"]:
    print("MODULE", m.get("scriptName"), m["moduleName"], [i.get("inputName") or i.get("name") for i in m.get("inputs", [])][:12])
err, d = nl.call("GetRendererData", {"rendererRef": {"system": nl.sysref(path), "emitterName": "Tracer", "rendererIndex": 0}})
props = json.loads(d["returnValue"]["propertyValues"])
print("RENDERER keys:", sorted(props.keys()))
for k in ["Alignment", "FacingMode", "Material", "SortMode", "PixelCoverageMode"]:
    print(" ", k, props.get(k))
print("SAVED:", unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False))
