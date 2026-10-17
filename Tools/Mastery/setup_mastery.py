"""Weapon mastery test assets (Docs/GameDesign_Tactical_To_PowerFantasy_2026-10-11.md section 11).

Steps, run with Tools/ue_step.sh Mastery setup_mastery <step>:
  probe        - print the python names of the new weapon fields on one gun (no writes)
  make_assets  - one DA_Mastery_<Class> per movement profile class, a test ladder (skips existing)
  assign       - each gun gets the mastery of its movement profile class (only where empty), and
                 ADS fatigue seeded by class (only where the fields still hold the C++ defaults)
Numbers are starting points for testing, not balance.
"""
import unreal

EAL = unreal.EditorAssetLibrary
OUT = "/Game/Variant_Shooter/Blueprints/Progression/Mastery"
WEAPON_DIRS = [
    "/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation",
    "/Game/Variant_Shooter/Blueprints/Pickups/Weapons/ClassBaseWeapons",
]
CLASSES = ["Pistol", "SMG", "AR", "Shotgun", "Marksman", "MG", "Launcher"]

# delay s, ramp s, sway deg: a pistol tires late and little, a machine gun early and much.
FATIGUE = {
    "Pistol":   (5.0, 6.0, 0.30),
    "SMG":      (4.0, 5.0, 0.40),
    "AR":       (3.0, 5.0, 0.50),
    "Shotgun":  (3.5, 5.0, 0.50),
    "Marksman": (2.5, 4.0, 0.70),
    "MG":       (1.5, 3.0, 0.90),
    "Launcher": (2.0, 3.0, 0.80),
}
CPP_DEFAULT = (3.0, 5.0, 0.5)


def _cdo(bp_path):
    cls = EAL.load_blueprint_class(bp_path)
    return unreal.get_default_object(cls) if cls else None


def _weapon_bps():
    out = []
    for d in WEAPON_DIRS:
        for p in EAL.list_assets(d, recursive=True, include_folder=False):
            name = p.split("/")[-1].split(".")[0]
            if name.startswith("BP_"):
                out.append(p.split(".")[0])
    return out


def probe():
    path = _weapon_bps()[0]
    cdo = _cdo(path)
    print("probe", path, cdo)
    for n in ("mastery_definition", "movement_profile", "ads_fatigue_delay", "ads_fatigue_ramp_time",
              "ads_fatigue_sway_degrees", "ads_fatigue_recovery_time", "ads_fatigue_sway_frequency"):
        try:
            print("  ", n, "=", cdo.get_editor_property(n))
        except Exception as e:
            print("  ", n, "ERROR", e)


def _handling(**kw):
    h = unreal.WeaponMasteryHandling()
    for k, v in kw.items():
        h.set_editor_property(k, v)
    return h


def _step(xp, handling=None, **flags):
    s = unreal.WeaponMasteryStep()
    s.set_editor_property("xp_to_reach", xp)
    if handling:
        s.set_editor_property("handling", handling)
    for k, v in flags.items():
        s.set_editor_property(k, v)
    return s


def make_assets():
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    for c in CLASSES:
        path = OUT + "/DA_Mastery_" + c
        if EAL.does_asset_exist(path):
            print("EXISTS:", path)
            continue
        da = tools.create_asset("DA_Mastery_" + c, OUT, unreal.WeaponMasteryDefinition, unreal.DataAssetFactory())
        da.set_editor_property("display_name", unreal.Text(c))
        da.set_editor_property("xp_per_hit", 1)
        da.set_editor_property("xp_per_kill", 5)
        quick = dict(ads_blend_in_time_multiplier=0.9, draw_duration_multiplier=0.9, holster_duration_multiplier=0.9,
                     sprint_to_fire_time_multiplier=0.9, reload_duration_multiplier=0.9, secondary_reload_time_multiplier=0.9,
                     ads_fatigue_rate_multiplier=0.85)
        faster = dict(ads_blend_in_time_multiplier=0.8, draw_duration_multiplier=0.8, holster_duration_multiplier=0.8,
                      sprint_to_fire_time_multiplier=0.8, reload_duration_multiplier=0.8, secondary_reload_time_multiplier=0.8,
                      ads_fatigue_rate_multiplier=0.7, ads_fatigue_sway_multiplier=0.7)
        da.set_editor_property("steps", [
            _step(10, _handling(**quick)),
            _step(30, None, allow_ads_while_sliding=True, allow_reload_while_ads=True, fast_swap_after_kill=True),
            _step(60, None, allow_ads_while_airborne=True, allow_reload_while_sliding=True, allow_tactical_sprint=True),
            _step(100, _handling(**faster), ignore_ads_move_speed_penalty=True, can_throw_weapon=True),
        ])
        EAL.save_loaded_asset(da)
        print("CREATED:", path)


def _class_of(cdo):
    prof = cdo.get_editor_property("movement_profile")
    if not prof:
        return None
    name = prof.get_name().replace("DA_Move_", "")
    return name if name in CLASSES else None


def assign():
    for path in _weapon_bps():
        cdo = _cdo(path)
        if not cdo:
            continue
        c = _class_of(cdo)
        if not c:
            print("SKIP (no class profile):", path)
            continue
        changed = False
        if not cdo.get_editor_property("mastery_definition"):
            cdo.set_editor_property("mastery_definition", unreal.load_asset(OUT + "/DA_Mastery_" + c))
            changed = True
        now = (cdo.get_editor_property("ads_fatigue_delay"), cdo.get_editor_property("ads_fatigue_ramp_time"),
               cdo.get_editor_property("ads_fatigue_sway_degrees"))
        if all(abs(a - b) < 1e-4 for a, b in zip(now, CPP_DEFAULT)):
            d, r, s = FATIGUE[c]
            cdo.set_editor_property("ads_fatigue_delay", d)
            cdo.set_editor_property("ads_fatigue_ramp_time", r)
            cdo.set_editor_property("ads_fatigue_sway_degrees", s)
            changed = True
        if changed:
            EAL.save_asset(path, only_if_is_dirty=False)
            print("MODIFIED:", path, c)
        else:
            print("UNCHANGED:", path, c)


BT = "editor_toolset.toolsets.blueprint.BlueprintTools"


def compile_and_check():
    """Compile every gun the way setup_muzzle_assets.py does, save, and read the fields back."""
    import json
    for path in _weapon_bps():
        if not EAL.load_blueprint_class(path):
            continue
        res = unreal.ToolsetRegistry.execute_tool(BT, "compile_blueprint",
            json.dumps({"blueprint": {"refPath": path + "." + path.split("/")[-1]}}))
        EAL.save_asset(path, only_if_is_dirty=False)
        cdo = _cdo(path)
        print("CHECK:", path.split("/")[-1], "compile", res.error if res.error else "ok",
              "mastery", cdo.get_editor_property("mastery_definition").get_name() if cdo.get_editor_property("mastery_definition") else None,
              "fatigue", cdo.get_editor_property("ads_fatigue_delay"), cdo.get_editor_property("ads_fatigue_sway_degrees"))
