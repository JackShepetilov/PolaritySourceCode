# Движение «бойца на старте» (2026-10-08):
#   1. профили движения по классам оружия (UWeaponMovementProfile) и раздача их стволам;
#   2. стартовые выключатели в общем ассете MovementSettings: без рывка, бега по стене, нырка,
#      отскока, слайда с приземления, прибавки от прыжка из слайда, высокого прыжка удержанием.
# Прежние значения ассета печатаются перед правкой и записаны в Docs/Movement_Start_2026-10-08.md:
# их вернут методички. Идемпотентно. Запуск из редактора: main().
import unreal, json

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
W = "/Game/Variant_Shooter/Blueprints/Pickups/Weapons"
PROFILES = W + "/MovementProfiles"
SETTINGS = "/Game/Variant_Shooter/Blueprints/MovementSettings/MovementSettings"

# класс: (шаг, спринт, прицел, пинок слайда, торможение слайда)
CLASSES = {
    "Pistol":   (1.00, 1.00, 1.00, 0.0, 1.0),
    "SMG":      (1.00, 0.97, 1.00, 0.0, 1.0),
    "AR":       (0.95, 0.93, 0.90, 0.0, 1.1),
    "Shotgun":  (0.95, 0.93, 0.95, 0.0, 1.1),
    "Marksman": (0.92, 0.90, 0.75, 0.0, 1.2),
    "MG":       (0.85, 0.82, 0.70, 0.0, 1.2),
    "Launcher": (0.85, 0.82, 0.80, 0.0, 1.2),
}

WEAPONS = {
    "Pistol": ["/Kinemation/Pistols/BP_M1911", "/Kinemation/Pistols/BP_Viper-357", "/Kinemation/Pistols/BP_X18",
               "/Kinemation/BP_DGL50", "/Kinemation/BP_Colibri", "/ClassBaseWeapons/BP_ShooterWavePistol"],
    "SMG": ["/Kinemation/SMGs/BP_MPS5", "/Kinemation/SMGs/BP_Striker-V", "/Kinemation/BP_PDW90"],
    "AR": ["/Kinemation/ARs/BP_AKX203", "/Kinemation/ARs/BP_G3", "/Kinemation/ARs/BP_MX16A4", "/Kinemation/BP_AKX12",
           "/Kinemation/BP_Val", "/EnemyWeapons/BP_AR", "/EnemyWeapons/BP_AR_CheatsInfAmmo"],
    "Shotgun": ["/Kinemation/Shotgun/BP_Drake-12", "/Kinemation/Shotgun/BP_KXG12", "/ClassBaseWeapons/BP_ShooterWeapon_Shotgun"],
    "Marksman": ["/Kinemation/Marksman/BP_SVD", "/Kinemation/Marksman/BP_L96X", "/Kinemation/BP_Kar98K", "/Kinemation/BP_Mk14EBR",
                 "/Kinemation/BP_M14", "/ClassBaseWeapons/BP_Marksman", "/EnemyWeapons/BP_SniperKraber"],
    "MG": ["/Kinemation/MG/BP_MGX5", "/EnemyWeapons/BP_LMG"],
    "Launcher": ["/Kinemation/BP_RPG7", "/EnemyWeapons/BP_ShooterWeapon_GrenadeLauncher"],
}

# поле ассета настроек: стартовое значение
START = {
    "slide_jump_boost": 0.0,
    "enable_slide_on_land": False,
    "enable_ground_dash": False,
    "enable_wall_run": False,
    "enable_air_dive": False,
    "enable_wall_bounce": False,
    "jump_hold_time": 0.0,
}


def log(*a):
    msg = " ".join(str(x) for x in a)
    print(msg)
    unreal.log("[MOVE_SETUP] " + msg)


def step_profiles():
    made = {}
    for name, (walk, sprint, ads, boost, friction) in CLASSES.items():
        p = PROFILES + "/DA_Move_" + name
        if not EAL.does_asset_exist(p):
            fac = unreal.DataAssetFactory()
            fac.set_editor_property("data_asset_class", unreal.WeaponMovementProfile)
            AT.create_asset("DA_Move_" + name, PROFILES, unreal.WeaponMovementProfile, fac)
            d = unreal.load_asset(p)
            d.set_editor_property("walk_speed_scale", walk)
            d.set_editor_property("sprint_speed_scale", sprint)
            d.set_editor_property("ads_speed_scale", ads)
            d.set_editor_property("slide_boost_scale", boost)
            d.set_editor_property("slide_friction_scale", friction)
            log("CREATED:", p, walk, sprint, ads, boost, friction, EAL.save_asset(p, False))
        made[name] = unreal.load_asset(p)
    return made


def step_weapons(made):
    for name, paths in WEAPONS.items():
        for rel in paths:
            p = W + rel
            cls = EAL.load_blueprint_class(p) if EAL.does_asset_exist(p) else None
            if cls is None:
                log("MISSING weapon", p)
                continue
            cdo = unreal.get_default_object(cls)
            if cdo.get_editor_property("movement_profile") == made[name]:
                continue
            cdo.set_editor_property("movement_profile", made[name])
            r = unreal.ToolsetRegistry.execute_tool(
                "editor_toolset.toolsets.blueprint.BlueprintTools", "compile_blueprint",
                json.dumps({"blueprint": {"refPath": p + "." + p.split("/")[-1]}}))
            log("MODIFIED", rel, "->", name, "compile err:", r.error, "saved", EAL.save_asset(p, False))


def step_settings():
    ms = unreal.load_asset(SETTINGS)
    for field, value in START.items():
        old = ms.get_editor_property(field)
        if old == value:
            continue
        ms.set_editor_property(field, value)
        log("MODIFIED settings", field, old, "->", value)
    log("air_strafe_multiplier", ms.get_editor_property("air_strafe_multiplier"), "jump_hold_force", ms.get_editor_property("jump_hold_force"),
        "air_acceleration", ms.get_editor_property("air_acceleration"), "air_speed_cap", ms.get_editor_property("air_speed_cap"))
    log("settings saved", EAL.save_asset(SETTINGS, False))


def main():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if les.is_in_play_in_editor():
        log("PIE is running, nothing done")
        return
    step_weapons(step_profiles())
    step_settings()
    log("DONE")
