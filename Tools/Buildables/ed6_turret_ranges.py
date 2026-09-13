# Turret: starting MountedRangeCm on every gun a turret can be fed, plus a wider floor and ceiling
# on the turret itself. Run AFTER the rebuild that adds AShooterWeapon::MountedRangeCm.
# Numbers are a first guess by role (cm); the author tunes them on each gun's Blueprint.
import unreal, json

EAL = unreal.EditorAssetLibrary
W = "/Game/Variant_Shooter/Blueprints/Pickups/Weapons"
TURRET_BP = "/Game/Variant_Shooter/Buildables/BP_Buildable_Turret"

RANGES = {
    # class weapons
    W + "/ClassBaseWeapons/BP_ShooterWavePistol": 1500,
    W + "/ClassBaseWeapons/BP_Marksman": 3500,
    W + "/ClassBaseWeapons/BP_ShooterWeapon_Shotgun": 800,
    W + "/BP_WaveRifle": 2000,
    # what the drops grant
    W + "/Kinemation/BP_AKX203": 2000,
    W + "/Kinemation/BP_MGX5": 2200,
    W + "/Kinemation/BP_RPG7": 2500,
    W + "/Kinemation/BP_MPS5": 1500,
    W + "/Kinemation/BP_M1911": 1200,
    W + "/Kinemation/BP_SVD": 3500,
    W + "/Kinemation/BP_Kar98K": 3500,
    W + "/Kinemation/BP_MX16A4": 2000,
    W + "/Kinemation/BP_PDW90": 1500,
    W + "/Kinemation/BP_KXG12": 800,
    W + "/Kinemation/BP_DGL50": 1800,
}
# Guns that live under other names: found by the drop that grants them.
BY_CLASS_NAME = {
    "BP_ShooterFunPistol_C": 1200,
    "BP_ShooterWeapon_RocketLauncher_C": 2500,
    "BP_BossWeapon_C": 1800,
    "BP_ShooterWeapon_ChargeLauncher_C": 2000,
    "BP_ShooterWeapon_GrenadeLauncher_C": 1800,
    "BP_ShooterWaveRifle_C": 2000,
}

def log(*a):
    print(" ".join(str(x) for x in a))

def compile_bp(path):
    res = unreal.ToolsetRegistry.execute_tool(
        "editor_toolset.toolsets.blueprint.BlueprintTools", "compile_blueprint",
        json.dumps({"blueprint": {"refPath": path + "." + path.split("/")[-1]}}))
    if res.error:
        log("COMPILE ERROR", path, res.error)

def set_range(cls, path, value):
    cdo = unreal.get_default_object(cls)
    before = cdo.get_editor_property("mounted_range_cm")
    cdo.set_editor_property("mounted_range_cm", float(value))
    compile_bp(path)
    EAL.save_asset(path)
    log("MODIFIED:", path.split("/")[-1], before, "->", value)

def main():
    done = set()
    for path, value in RANGES.items():
        if not EAL.does_asset_exist(path):
            log("missing", path)
            continue
        cls = EAL.load_blueprint_class(path)
        if cls:
            set_range(cls, path, value)
            done.add(cls.get_name())
    # The drops name their gun classes; read the class off each drop, no folder scan.
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    for d in reg.get_assets_by_path(W + "/Drops", False):
        if str(d.asset_class_path.asset_name) != "Blueprint":
            continue
        dcls = EAL.load_blueprint_class(str(d.package_name))
        dcdo = unreal.get_default_object(dcls) if dcls else None
        try:
            wcls = dcdo.get_editor_property("weapon_class") if dcdo else None
        except Exception:
            continue
        if not wcls or wcls.get_name() in done or wcls.get_name() not in BY_CLASS_NAME:
            continue
        # The class's package is the Blueprint asset path.
        pkg = wcls.get_outermost().get_name()
        set_range(wcls, pkg, BY_CLASS_NAME[wcls.get_name()])
        done.add(wcls.get_name())
    # The turret's floor and ceiling, so an authored 35 m sniper range is not clamped to 30.
    tcls = EAL.load_blueprint_class(TURRET_BP)
    tcdo = unreal.get_default_object(tcls)
    tcdo.set_editor_property("min_range_cm", 600.0)
    tcdo.set_editor_property("max_range_cm", 4000.0)
    compile_bp(TURRET_BP)
    EAL.save_asset(TURRET_BP)
    log("MODIFIED:", TURRET_BP, "range clamp 600..4000")
    log("DONE, set on:", sorted(done))

main()
