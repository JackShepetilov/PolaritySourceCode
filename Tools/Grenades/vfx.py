# vfx.py: the flashbang's burst, built from NiagaraExamples pieces (Docs/Grenades_Plan_2026-10-14.md).
# Run step by step: Tools/ue_step.sh Grenades vfx <step>. One tool shape per call (Docs/Gotchas/Niagara.md).
import json
import unreal

LIB = r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/DamagePlayground/niagara_lib.py"
nl = {"__name__": "nl"}
exec(compile(open(LIB, encoding="utf-8").read(), LIB, "exec"), nl)
EAL = unreal.EditorAssetLibrary

REF = "/Game/NiagaraExamples/FX_Explosions/NS_Explosion_Small"
OUT = "/Game/Variant_Shooter/Blueprints/Grenades/NS_FlashbangBurst"


def _pie():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None


def survey(path=REF):
    print("pie:", _pie())
    for e in nl["emitter_names"](path):
        mods = nl["module_names"](path, e)
        print("EMITTER", e, "|", ", ".join(mods)[:600])
        for i in range(3):
            err, d = nl["call"]("GetRendererData", {"rendererRef": {"system": nl["sysref"](path), "emitterName": e, "rendererIndex": i}})
            if err or not d:
                break
            props = json.loads(d["returnValue"]["propertyValues"])
            keep = {k: props[k] for k in ("Material", "SubImageSize", "RadiusScale", "ColorBinding") if k in props}
            print("   renderer", i, d["returnValue"].get("rendererClass", ""), json.dumps(keep)[:300])


def summary(path=REF):
    print("pie:", _pie())
    err, d = nl["call"]("GetSystemSummary", {"system": nl["sysref"](path)})
    print("err:", err)
    print(json.dumps(d)[:3500])


def topology(emitter="Decal_Light_Flash", path=REF):
    err, d = nl["call"]("GetEmitterTopology", {"emitterRef": {"system": nl["sysref"](path), "emitterName": emitter}})
    print("err:", err)
    print(json.dumps(d)[:4000])


def renderer(emitter="NE_PostProcess", index=0, path=REF):
    err, d = nl["call"]("GetRendererData", {"rendererRef": {"system": nl["sysref"](path), "emitterName": emitter, "rendererIndex": index}})
    print("err:", err)
    print(json.dumps(d)[:3000])


EX = "/Game/NiagaraExamples/Materials/"


def create():
    """A copy of the example system, emptied: only its shell is wanted (CPU, bounds, the system
    settings); every part of the flash is added below."""
    if _pie():
        print("PIE is running, stop it first"); return
    if not EAL.does_asset_exist(OUT):
        print("CREATED:", OUT, bool(EAL.duplicate_asset(REF, OUT)))
        EAL.save_asset(OUT, only_if_is_dirty=False)
    for e in nl["emitter_names"](OUT):
        if e in ("Explosion", "GroundDust", "NE_PostProcess", "Decal_Light_Flash", "Debris", "SparkDebris"):
            nl["remove_emitter"](OUT, e)
    print("EMITTERS:", nl["emitter_names"](OUT))
    EAL.save_asset(OUT, only_if_is_dirty=False)


def flare():
    nl["add_burst"](OUT, "Flare", 1, 0.22, 650.0, EX + "MI_ExplosionFlare")
    nl["finish"](OUT)


def core():
    nl["add_burst"](OUT, "Core", 1, 0.09, 240.0, EX + "MI_ImpactFlash")
    nl["finish"](OUT)


def sparks():
    nl["add_burst"](OUT, "Sparks", 20, 0.35, 5.0, EX + "MI_Sparks", speed=1100.0, cone=180.0, gravity=True)
    nl["set_random"](OUT, "Sparks", "ParticleSpawnScript", "AddVelocityInCone", "Velocity Strength", 500.0, 1300.0)
    nl["finish"](OUT)


def smoke():
    nl["add_burst"](OUT, "Smoke", 5, 2.5, 140.0, "/Game/Effects/Materials/M_SmokeCloud", grid=8, speed=50.0, cone=90.0)
    nl["set_random"](OUT, "Smoke", "ParticleSpawnScript", "InitializeParticle", "Uniform Sprite Size", 100.0, 170.0)
    nl["finish"](OUT)


def light_probe():
    err, d = nl["call"]("AddRenderer", {})
    print("err:", str(err)[:1500])


def light_add():
    if "Flare" in nl["emitter_names"](OUT):
        err, d = nl["call"]("GetRendererData", {"rendererRef": {"system": nl["sysref"](OUT), "emitterName": "Flare", "rendererIndex": 1}})
        if not err and d:
            print("EXISTS: second renderer on Flare"); return
    err, d = nl["call"]("AddRenderer", {"newRendererLocation": {"system": nl["sysref"](OUT), "emitterName": "Flare",
                                        "scriptName": "", "moduleName": "", "rendererIndex": -1, "inputNameStack": []},
                                        "rendererClass": {"refPath": "/Script/Niagara.NiagaraLightRendererProperties"}})
    print("err:", str(err)[:800], "| ret:", json.dumps(d)[:400])
    EAL.save_asset(OUT, only_if_is_dirty=False)


def light_read():
    err, d = nl["call"]("GetRendererData", {"rendererRef": {"system": nl["sysref"](OUT), "emitterName": "Flare", "rendererIndex": 1}})
    print("err:", err)
    props = json.loads(d["returnValue"]["propertyValues"])
    print({k: v for k, v in props.items() if "Binding" not in k})


def smoke_axis_read():
    err, d = nl["get_input"](OUT, "Smoke", "ParticleSpawnScript", "AddVelocityInCone", ["Cone Axis"])
    print("err:", err, "| data:", json.dumps(d)[:600])


def smoke_axis_up():
    """The smoke rises: the cone points up, not along the system's X (the grenade lies unrotated)."""
    err, d = nl["get_input"](OUT, "Smoke", "ParticleSpawnScript", "AddVelocityInCone", ["Cone Axis"])
    data = d["returnValue"]
    if isinstance(data.get("value"), dict):
        data["value"].update({"x": 0.0, "y": 0.0, "z": 1.0})
    err2, _ = nl["call"]("SetStackInputData", {
        "stackInputRef": nl["input_ref"](OUT, "Smoke", "ParticleSpawnScript", "AddVelocityInCone", ["Cone Axis"]),
        "inputData": data})
    print("MODIFIED: Smoke cone axis up | err:", err2 or "none")
    nl["finish"](OUT)


def wire():
    da = unreal.load_asset("/Game/Variant_Shooter/Blueprints/Grenades/DA_Grenade_Flash")
    da.set_editor_property("detonation_fx", unreal.load_asset(OUT))
    print("MODIFIED: DA_Grenade_Flash detonation_fx", da.get_editor_property("detonation_fx"))
    print("SAVED:", EAL.save_asset("/Game/Variant_Shooter/Blueprints/Grenades/DA_Grenade_Flash", only_if_is_dirty=False))


def light_set():
    """A blinding flash for a fifth of a second: wide, very bright, slightly warm, fading with the flare."""
    nl["set_renderer"](OUT, "Flare", {"RadiusScale": 3.0, "ColorAdd": {"x": 60.0, "y": 58.0, "z": 52.0},
                                      "bAlphaScalesBrightness": True, "bAffectsTranslucency": True}, index=1)
    nl["finish"](OUT)


# ==================== Look at it (spawn, shot, cleanup: separate calls) ====================

FXP = r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/DamagePlayground/fx_preview.py"
fp = {"__name__": "fp"}
exec(compile(open(FXP, encoding="utf-8").read(), FXP, "exec"), fp)


def where():
    w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    print("level:", w.get_package().get_name() if w else None, "| pie:", _pie(),
          "| dirty:", [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()])


def spawn():
    if _pie():
        print("PIE_RUNNING"); return
    fp["cleanup"]()
    act = fp["_eas"]().spawn_actor_from_class(unreal.NiagaraActor, fp["AT"], unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    act.set_editor_property("tags", [unreal.Name(fp["TAG"])])
    act.set_actor_label(fp["TAG"])
    act.niagara_component.set_asset(unreal.load_asset(OUT))
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_set_viewport_realtime(True)
    print("SPAWNED: NS_FlashbangBurst")


def shot_t005():
    fp["_shot"]("flash_t005", 0.05, 1.0, 900.0)


def shot_t030():
    fp["_shot"]("flash_t030", 0.30, 1.0, 900.0)


def shot_t150():
    fp["_shot"]("flash_t150", 1.5, 1.0, 900.0)


def cleanup():
    fp["cleanup"]()


def color_read(emitter="Flare"):
    err, d = nl["get_input"](OUT, emitter, "ParticleSpawnScript", "InitializeParticle", ["Color"])
    print("err:", err, "| data:", json.dumps(d)[:500])


SOFT = "/Niagara/DefaultAssets/DefaultSpriteMaterial"


def _color(emitter, r, g, b, a=1.0):
    err, _ = nl["call"]("SetStackInputData", {
        "stackInputRef": nl["input_ref"](OUT, emitter, "ParticleSpawnScript", "InitializeParticle", ["Color"]),
        "inputData": {"struct": {"refPath": "/Script/CoreUObject.LinearColor"}, "value": {"r": r, "g": g, "b": b, "a": a}}})
    print("MODIFIED:", emitter, "color", (r, g, b, a), "| err:", err or "none")


def bright():
    """Flare and core: the soft default dot, overdriven so bloom makes the glare. The example flare
    materials drew nothing on the first try (cause not found); this one is proven in the project."""
    for name, (r, g, b) in (("Flare", (25.0, 24.0, 21.0)), ("Core", (60.0, 60.0, 58.0)), ("Sparks", (12.0, 11.0, 9.0))):
        nl["set_renderer"](OUT, name, {"Material": {"refPath": SOFT + "." + SOFT.split("/")[-1]}})
        _color(name, r, g, b)
    nl["finish"](OUT)


def compare():
    err, d = nl["call"]("GetSystemSummary", {"system": nl["sysref"](OUT)})
    for e in d["returnValue"]["emitters"]:
        print("EMITTER", e["emitterName"], "enabled", e["bEnabled"], e["simTarget"], [r["refPath"].split(".")[-1] for r in e["rendererClasses"]])
    for name in ("Flare", "Smoke"):
        err, t = nl["call"]("GetEmitterTopology", {"emitterRef": {"system": nl["sysref"](OUT), "emitterName": name}})
        r = t["returnValue"]
        for k in ("emitterSpawnScript", "emitterUpdateScript", "particleSpawnScript", "particleUpdateScript"):
            print(" ", name, k, [m.get("moduleName") for m in r[k]["modules"]])
        for stack in (["Lifetime"], ["Uniform Sprite Size"], ["Sprite Size Mode"]):
            e2, v = nl["get_input"](OUT, name, "ParticleSpawnScript", "InitializeParticle", stack)
            print("   ", name, stack[0], json.dumps(v)[:200] if v else e2)


def count(t=0.05):
    act = fp["_actor"]()
    if not act:
        print("no preview actor"); return
    nc = act.niagara_component
    nc.deactivate()
    nc.activate(True)
    nc.advance_simulation_by_time(t, 1.0 / 120.0)
    for e in ("Flare", "Core", "Sparks", "Smoke"):
        fn = [n for n in dir(nc) if "particle_positions" in n]
        pos = getattr(nc, fn[0])(e) if fn else []
        if not fn:
            print("no debug fn; candidates:", [n for n in dir(nc) if "particle" in n])
        print(e, "particles:", len(pos), [str(p)[:60] for p in pos[:1]])


def flare_life(life=1.0):
    nl["set_number"](OUT, "Flare", "ParticleSpawnScript", "InitializeParticle", "Lifetime", life)
    nl["set_number"](OUT, "Flare", "EmitterUpdateScript", "EmitterState", "Loop Duration", life + 0.1)
    nl["finish"](OUT)


def flare_life_back():
    flare_life(0.22)


def shot_t050():
    fp["_shot"]("flash_t050", 0.5, 1.0, 900.0)


def example_mats():
    """Back to the NiagaraExamples materials: they were never broken, the short flare only did not live
    long enough to reach the editor capture. Colour back to white x a little, they carry their own glow."""
    for name, mat, (r, g, b) in (("Flare", "MI_ExplosionFlare", (2.0, 2.0, 1.9)), ("Core", "MI_ImpactFlash", (3.0, 3.0, 3.0)),
                                 ("Sparks", "MI_Sparks", (4.0, 3.8, 3.4))):
        nl["set_renderer"](OUT, name, {"Material": {"refPath": EX + mat + "." + mat}})
        _color(name, r, g, b)
    nl["finish"](OUT)


def rot_read():
    for stack in (["Sprite Rotation Mode"], ["Sprite Rotation Angle"], ["Sprite Rotation"]):
        err, d = nl["get_input"](OUT, "Flare", "ParticleSpawnScript", "InitializeParticle", stack)
        print(stack[0], "| err:", str(err)[:120], "| data:", json.dumps(d)[:300] if d else None)


def enum_info(path="/Niagara/Enums/ENiagara_SpriteRotationMode.ENiagara_SpriteRotationMode"):
    r = unreal.ToolsetRegistry.execute_tool("NiagaraToolsets.NiagaraToolset_Info", "UEnum_Info", json.dumps({"enum": {"refPath": path}}))
    print("err:", str(r.error)[:2500])
    print(str(r.get_value_as_json_string())[:1200] if not r.error else "")


ROT_RATE = "/Niagara/Modules/Update/Orientation/SpriteRotationRate.SpriteRotationRate"


def _random_rotation(emitter):
    err, _ = nl["call"]("SetStackInputData", {
        "stackInputRef": nl["input_ref"](OUT, emitter, "ParticleSpawnScript", "InitializeParticle", ["Sprite Rotation Mode"]),
        "inputData": {"struct": {"refPath": "/Script/NiagaraEditor.NiagaraExt_StackInputData_Enum"},
                      "value": {"enum": {"refPath": "/Niagara/Enums/ENiagara_SpriteRotationMode.ENiagara_SpriteRotationMode"},
                                "enumName": "ENiagara_SpriteRotationMode::NewEnumerator2"}}})
    print("MODIFIED:", emitter, "sprite rotation random | err:", err or "none")


def animate():
    """Not one still flare but a few, laid over each other: each its own size, angle of the rays and
    life, turning slowly, going out at different moments. Reads as a flicker, not a picture."""
    nl["set_number"](OUT, "Flare", "EmitterUpdateScript", "SpawnBurst_Instantaneous", "Spawn Count", 3, nl["INT"])
    nl["set_number"](OUT, "Flare", "EmitterUpdateScript", "EmitterState", "Loop Duration", 0.45)
    nl["set_random"](OUT, "Flare", "ParticleSpawnScript", "InitializeParticle", "Lifetime", 0.1, 0.32)
    nl["set_random"](OUT, "Flare", "ParticleSpawnScript", "InitializeParticle", "Uniform Sprite Size", 380.0, 760.0)
    _random_rotation("Flare")
    nl["set_number"](OUT, "Core", "EmitterUpdateScript", "SpawnBurst_Instantaneous", "Spawn Count", 2, nl["INT"])
    nl["set_random"](OUT, "Core", "ParticleSpawnScript", "InitializeParticle", "Lifetime", 0.05, 0.13)
    nl["set_random"](OUT, "Core", "ParticleSpawnScript", "InitializeParticle", "Uniform Sprite Size", 150.0, 290.0)
    _random_rotation("Core")
    nl["finish"](OUT)


def spin():
    """The rays turn, each flare its own way and speed. A separate call: an untried module shape."""
    nl["add_module"](OUT, "Flare", "ParticleUpdateScript", "ScaleColor", ROT_RATE)
    print("MODULES:", nl["module_names"](OUT, "Flare") if False else "")
    nl["finish"](OUT)


def spin_inputs():
    err, t = nl["call"]("GetEmitterTopology", {"emitterRef": {"system": nl["sysref"](OUT), "emitterName": "Flare"}})
    print([m.get("moduleName") for m in t["returnValue"]["particleUpdateScript"]["modules"]])
    for stack in (["Rotation Rate"], ["Sprite Rotation Rate"]):
        err, d = nl["get_input"](OUT, "Flare", "ParticleUpdateScript", "SpriteRotationRate", stack)
        print(stack[0], "| err:", str(err)[:100], "| data:", json.dumps(d)[:200] if d else None)


def spin_set():
    nl["set_random"](OUT, "Flare", "ParticleUpdateScript", "SpriteRotationRate", "Rotation Rate", -140.0, 140.0)
    nl["finish"](OUT)
