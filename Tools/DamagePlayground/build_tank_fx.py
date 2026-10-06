"""New tank effects from generated atlases, using the verified Niagara helpers. Call one stage at a time."""
import unreal

path = r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8/Source/Tools/DamagePlayground/niagara_lib.py"
nl = {"__name__": "tank_niagara"}
exec(compile(open(path, encoding="utf-8").read(), path, "exec"), nl)
FX = "/Game/Effects/Impacts/"
MAT = FX + "Materials/"
EAL = unreal.EditorAssetLibrary


def ensure(name):
    target = FX + name
    if not EAL.does_asset_exist(target):
        assert EAL.duplicate_asset(FX + "NS_Impact_RobotBody", target)
        for emitter in nl["emitter_names"](target):
            nl["remove_emitter"](target, emitter)
        EAL.save_asset(target, only_if_is_dirty=False)
    return target


def burst(system, emitter, count, life, size, material, grid, speed=0, cone=60, gravity=False):
    target = ensure(system)
    nl["add_burst"](target, emitter, count, life, size, MAT + material, grid, speed, cone, gravity)
    if speed:
        nl["set_random"](target, emitter, "ParticleSpawnScript", "AddVelocityInCone", "Velocity Strength", speed * .4, speed * 1.3)
    nl["set_random"](target, emitter, "ParticleSpawnScript", "InitializeParticle", "Lifetime", life * .6, life)
    nl["set_random"](target, emitter, "ParticleSpawnScript", "InitializeParticle", "Uniform Sprite Size", size * .65, size * 1.2)
    nl["finish"](target)
    return target


def ricochet():
    burst("NS_Impact_TankPlate", "Flash", 1, .12, 15, "M_Tank_Ricochet", 2)
    burst("NS_Impact_TankPlate", "Sparks", 7, .28, 9, "M_Tank_Sparks", 4, 260)


def track_impact():
    burst("NS_Impact_TankTrack", "Chips", 3, .3, 7, "M_Tank_Tracks", 4, 150, gravity=True)
    burst("NS_Impact_TankTrack", "Sparks", 5, .23, 8, "M_Tank_Sparks", 4, 240)


def optics_impact():
    burst("NS_Impact_TankOptics", "Glass", 4, .3, 6, "M_Tank_Glass", 4, 150, gravity=True)
    burst("NS_Impact_TankOptics", "Sparks", 3, .2, 6, "M_Tank_Sparks", 4, 200)


def dead_impact():
    burst("NS_Impact_TankPartDead", "Dust", 2, .4, 18, "M_Tank_Smoke", 2, 35)
    burst("NS_Impact_TankPartDead", "Flakes", 3, .35, 4, "M_Tank_Tracks", 4, 90, gravity=True)


def track_break():
    burst("NS_TankTrackBreak", "Links", 10, .65, 14, "M_Tank_Tracks", 4, 220, cone=140, gravity=True)
    burst("NS_TankTrackBreak", "Sparks", 8, .35, 12, "M_Tank_Sparks", 4, 260, cone=140)
    burst("NS_TankTrackBreak", "Smoke", 4, 2, 45, "M_Tank_Smoke", 2, 18, cone=180)


def optics_break():
    burst("NS_TankOpticsBreak", "Glass", 12, .65, 12, "M_Tank_Glass", 4, 230, cone=140, gravity=True)
    burst("NS_TankOpticsBreak", "Sparks", 6, .3, 8, "M_Tank_Sparks", 4, 180, cone=140)


def wreck():
    burst("NS_TankWreckBurn", "Flames", 3, 1.2, 100, "M_Tank_Fire", 2, 20, cone=180)
    burst("NS_TankWreckBurn", "Smoke", 4, 2.4, 150, "M_Tank_Smoke", 2, 30, cone=180)


def typed(path, emitter, script, module, name, struct, value):
    error, result = nl["call"]("SetStackInputData", {
        "stackInputRef": nl["input_ref"](path, emitter, script, module, [name]),
        "inputData": {"struct": {"refPath": struct}, "value": value}})
    if error:
        raise RuntimeError(error)
    return result


def loop(system, emitter, seconds, count):
    path = FX + system
    typed(path, emitter, "EmitterUpdateScript", "EmitterState", "Loop Behavior",
        "/Script/NiagaraEditor.NiagaraExt_StackInputData_Enum", {
            "enum": {"refPath": "/Niagara/Enums/ENiagara_EmitterStateOptions.ENiagara_EmitterStateOptions"},
            "enumName": "ENiagara_EmitterStateOptions::NewEnumerator0", "displayName": "Infinite"})
    nl["set_number"](path, emitter, "EmitterUpdateScript", "EmitterState", "Loop Duration", seconds)
    nl["set_number"](path, emitter, "EmitterUpdateScript", "SpawnBurst_Instantaneous", "Spawn Count", count, nl["INT"])


def color(system, emitter, rgb):
    typed(FX + system, emitter, "ParticleSpawnScript", "InitializeParticle", "Color",
          "/Script/CoreUObject.LinearColor", dict(zip(("r", "g", "b", "a"), (*rgb, 1))))


def up(system, emitter):
    typed(FX + system, emitter, "ParticleSpawnScript", "AddVelocityInCone", "Cone Axis",
          "/Script/CoreUObject.Vector3f", {"x": 0, "y": 0, "z": 1})
    nl["set_number"](FX + system, emitter, "ParticleSpawnScript", "AddVelocityInCone", "Cone Angle", 20)


def tails():
    color("NS_Impact_TankPartDead", "Dust", (.09, .075, .06))
    color("NS_Impact_TankPartDead", "Flakes", (.15, .12, .10))
    color("NS_TankTrackBreak", "Smoke", (.065, .06, .055))
    color("NS_TankWreckBurn", "Smoke", (.055, .045, .038))
    loop("NS_TankTrackBreak", "Smoke", .75, 4)
    loop("NS_TankOpticsBreak", "Sparks", 1.4, 2)
    loop("NS_TankWreckBurn", "Smoke", .75, 4)
    loop("NS_TankWreckBurn", "Flames", .35, 1)
    up("NS_TankTrackBreak", "Smoke")
    up("NS_TankWreckBurn", "Smoke")
    up("NS_TankWreckBurn", "Flames")
    for name in ("NS_Impact_TankPartDead", "NS_TankTrackBreak", "NS_TankOpticsBreak", "NS_TankWreckBurn"):
        nl["finish"](FX + name)


def hull_sources():
    for emitter, position in (("Flames", (0, 0, 0)), ("FlameFront", (95, 30, -20)),
                              ("FlameRear", (-100, -30, -20))):
        if emitter != "Flames":
            burst("NS_TankWreckBurn", emitter, 1, 1.2, 85, "M_Tank_Fire", 2, 20, cone=20)
        loop("NS_TankWreckBurn", emitter, .35, 1)
        up("NS_TankWreckBurn", emitter)
        typed(FX + "NS_TankWreckBurn", emitter, "ParticleSpawnScript", "InitializeParticle", "UsePositionOffset",
              "/Script/Niagara.NiagaraBool", {"value": -1})
        typed(FX + "NS_TankWreckBurn", emitter, "ParticleSpawnScript", "InitializeParticle", "Position Offset",
              "/Script/CoreUObject.Vector3f", dict(zip(("x", "y", "z"), position)))
    nl["finish"](FX + "NS_TankWreckBurn")


def readable_wreck_smoke():
    path = FX + "NS_TankWreckBurn"
    nl["set_random"](path, "Smoke", "ParticleSpawnScript", "InitializeParticle", "Uniform Sprite Size", 190, 250)
    nl["set_random"](path, "Smoke", "ParticleSpawnScript", "AddVelocityInCone", "Velocity Strength", 65, 90)
    nl["finish"](path)


def wire():
    for name in ("DA_HitFeedbackSet", "DA_HitFeedbackSetNoShield"):
        target = "/Game/Variant_Shooter/Blueprints/" + name
        asset = unreal.load_asset(target)
        impacts = dict(asset.get_editor_property("impacts"))
        for index, suffix in ((11, "TankPlate"), (12, "TankTrack"), (13, "TankOptics"), (14, "TankPartDead")):
            key = getattr(unreal.PhysicalSurface, "SURFACE_TYPE" + str(index))
            item = impacts[key]
            item.set_editor_property("vfx", unreal.load_asset(FX + "NS_Impact_" + suffix))
            impacts[key] = item
        asset.set_editor_property("impacts", impacts)
        EAL.save_asset(target, only_if_is_dirty=False)
    bp = "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_TrackedTank"
    cdo = unreal.get_default_object(EAL.load_blueprint_class(bp))
    component = cdo.get_components_by_class(unreal.HitZoneComponent)[0]
    zones = list(component.get_editor_property("zones"))
    for zone in zones:
        name = str(zone.get_editor_property("name"))
        if name in ("TrackLeft", "TrackRight", "Optics"):
            zone.set_editor_property("broken_effect", unreal.load_asset(FX + ("NS_TankOpticsBreak" if name == "Optics" else "NS_TankTrackBreak")))
    component.set_editor_property("zones", zones)
    cdo.set_editor_property("critical_hull_fx", unreal.load_asset(FX + "NS_TankWreckBurn"))
    import json
    result = unreal.ToolsetRegistry.execute_tool("editor_toolset.toolsets.blueprint.BlueprintTools", "compile_blueprint",
        json.dumps({"blueprint": {"refPath": bp + ".BP_TrackedTank"}}))
    assert result.is_complete and not result.error
    EAL.save_asset(bp, only_if_is_dirty=False)
    print("[TANK_FX] wired all tank assets")
