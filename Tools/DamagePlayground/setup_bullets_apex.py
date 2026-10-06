import unreal, json
# Bullets by weapon class, Apex numbers (2026-10-05). Idempotent: creates what is missing, rewrites
# the values every run. Run with the editor idle and PIE off: Tools/mcp.sh py <this file>.
#
# Apex units: their scripts say 1968.5 units = 50 m, so 1 unit = 2.54 cm.
# Speeds (projectile_launch_speed, Mauler125/scripts_r5 weapons/*.txt):
#   AR      R-301 29000                      -> 73660 cm/s
#   Marksman Sentinel 31000 (G7 31500, Longbow 30500) -> 78740
#   MG      Devotion 33500 / Rampage 26500, middle 30000 -> 76200
#   Pistol  P2020 18500 (RE-45 19500, Wingman 18000)   -> 46990
#   SMG     Alternator 19500 (R-99 21000, Prowler 18000) -> 49530
#   Shotgun _base_shotgun 16000, bolt_hitsize 1.0      -> 40640, radius 2.54
# Hit size: Apex rifles/SMGs/pistols bolt_hitsize 0 (a point); ours 1 cm (a zero sphere is not a body).
# Marksman grows: 0.33 in at ~100 m, 1.25 at ~200 m, 2.0 at ~300 m -> keys in metres / cm below.

ROOT = "/Game/Variant_Shooter/Blueprints/Pickups/Projectiles"
BULLET = ROOT + "/BP_ShooterProjectile_Bullet"
PELLET = ROOT + "/BP_ShooterProjectile_Pellet"
ROCKET = ROOT + "/BP_ShooterProjectile_Rocket"
ATT_DIR = "/Game/Variant_Shooter/Audio/Attenuation"
WEAPONS = "/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation"
WHIZ = "/Game/Audio/MetaSounds/sfx_WhizBys_nl_meta"

# class: (parent, speed cm/s, radius cm, growth keys [(m, cm)], fire falloff cm, weapons)
CLASSES = {
    "AR":       (BULLET, 73660.0, 1.0, [], 15000.0, ["ARs/BP_AKX203", "ARs/BP_G3", "ARs/BP_MX16A4"]),
    "Marksman": (BULLET, 78740.0, 1.0, [(0.0, 1.0), (100.0, 1.0), (200.0, 3.2), (300.0, 5.1)], 25000.0,
                 ["Marksman/BP_L96X", "Marksman/BP_SVD"]),
    "MG":       (BULLET, 76200.0, 1.0, [], 17000.0, ["MG/BP_MGX5"]),
    "Pistol":   (BULLET, 46990.0, 1.0, [], 8000.0, ["Pistols/BP_M1911", "Pistols/BP_Viper-357", "Pistols/BP_X18"]),
    "SMG":      (BULLET, 49530.0, 1.0, [], 10000.0, ["SMGs/BP_MPS5", "SMGs/BP_Striker-V"]),
    "Shotgun":  (PELLET, 40640.0, 2.54, [], 12000.0, ["Shotgun/BP_Drake-12", "Shotgun/BP_KXG12"]),
}

tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary

def compile_bp(path):
    name = path.rsplit("/", 1)[1]
    r = unreal.ToolsetRegistry.execute_tool("editor_toolset.toolsets.blueprint.BlueprintTools", "compile_blueprint",
                                            json.dumps({"blueprint": {"refPath": f"{path}.{name}"}}))
    return str(unreal.load_asset(path).get_editor_property("status"))

def attenuation(cls_name, falloff):
    name = f"ATT_Gun_{cls_name}"
    path = f"{ATT_DIR}/{name}"
    a = unreal.load_asset(path) if eal.does_asset_exist(path) else \
        tools.create_asset(name, ATT_DIR, unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
    s = a.get_editor_property("attenuation")
    s.set_editor_property("attenuate", True)
    s.set_editor_property("spatialize", True)
    s.set_editor_property("attenuation_shape", unreal.AttenuationShape.SPHERE)
    s.set_editor_property("attenuation_shape_extents", unreal.Vector(300.0, 0.0, 0.0))
    s.set_editor_property("falloff_distance", falloff)
    s.set_editor_property("distance_algorithm", unreal.AttenuationDistanceModel.NATURAL_SOUND)
    s.set_editor_property("d_b_attenuation_at_max", -60.0)
    # Far gunfire loses its top end: a muffled thump at range, the crack up close.
    s.set_editor_property("attenuate_with_lpf", True)
    s.set_editor_property("lpf_radius_min", 2000.0)
    s.set_editor_property("lpf_radius_max", falloff)
    s.set_editor_property("lpf_frequency_at_min", 20000.0)
    s.set_editor_property("lpf_frequency_at_max", 2500.0)
    # Through a wall: quieter and duller (Apex runs occlusion too, miles_occlusion 1).
    s.set_editor_property("enable_occlusion", True)
    s.set_editor_property("occlusion_low_pass_filter_frequency", 1500.0)
    s.set_editor_property("occlusion_volume_attenuation", 0.5)
    s.set_editor_property("occlusion_interpolation_time", 0.1)
    a.set_editor_property("attenuation", s)
    eal.save_loaded_asset(a, False)
    print("ATT", name, falloff)
    return a

def bullet(cls_name, parent, speed, radius, keys, whiz):
    name = f"BP_Bullet_{cls_name}"
    path = f"{ROOT}/{name}"
    if not eal.does_asset_exist(path):
        fac = unreal.BlueprintFactory()
        fac.set_editor_property("parent_class", eal.load_blueprint_class(parent))
        tools.create_asset(name, ROOT, unreal.Blueprint, fac)
        print("CREATED", path)
    bp_cls = eal.load_blueprint_class(path)
    cdo = unreal.get_default_object(bp_cls)
    pm = cdo.get_editor_property("projectile_movement")
    pm.set_editor_property("initial_speed", speed)
    pm.set_editor_property("max_speed", speed)
    col = cdo.get_editor_property("collision_component")
    col.set_editor_property("sphere_radius", radius)
    cdo.set_editor_property("hit_radius_over_flight", [unreal.Vector2D(x, y) for x, y in keys])
    cdo.set_editor_property("whiz_by_sound", whiz)
    status = compile_bp(path)
    eal.save_asset(path, False)
    print("BULLET", name, speed, radius, len(keys), status)
    return bp_cls

ONLY = globals().get("ONLY")  # set before exec to try one class first
whiz = unreal.load_asset(WHIZ)
for cls_name, (parent, speed, radius, keys, falloff, weapons) in CLASSES.items():
    if ONLY and cls_name not in ONLY:
        continue
    att = attenuation(cls_name, falloff)
    bp_cls = bullet(cls_name, parent, speed, radius, keys, whiz)
    for w in weapons:
        wpath = f"{WEAPONS}/{w}"
        wcdo = unreal.get_default_object(eal.load_blueprint_class(wpath))
        wcdo.set_editor_property("projectile_class", bp_cls)
        wcdo.set_editor_property("fire_sound_attenuation", att)
        status = compile_bp(wpath)
        eal.save_asset(wpath, False)
        print("WEAPON", w, "->", bp_cls.get_name(), att.get_name(), status)

if ONLY:
    raise SystemExit("ONLY run, rocket skipped")
# The rocket whizzes too, until it gets a flight sound of its own.
rcdo = unreal.get_default_object(eal.load_blueprint_class(ROCKET))
rcdo.set_editor_property("whiz_by_sound", whiz)
rcdo.set_editor_property("whiz_by_max_distance", 2000.0)
print("ROCKET", compile_bp(ROCKET))
eal.save_asset(ROCKET, False)
print("DONE")
