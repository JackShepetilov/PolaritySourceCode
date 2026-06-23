# SURGICAL prop dressing for the A2 art dupe: replaces my old amphora-spam decor with a curated
# set of SMALL (~1m cube; statues/banners <= human ~2m) props from EXISTING packs. Small props =
# EMF props (BP_EMFProp_1 with the prop mesh, throwable); human-height statues = static. Each mesh
# is AUTO-SCALED to its target world size. Operates ONLY on the open dupe, clears ONLY my own
# ARTPASS decor (label 'decor'), touches nothing else, and does NOT save (review + Ctrl+S).
# Log: [ADD_PROPS].

import math
import unreal

TAG = "[ADD_PROPS]"
DST = "/Game/Variant_Shooter/Arenas/Biome1/A2_Courtyard/Lvl_A2_Courtyard_Art"
ART_TAG = "ARTPASS_A2_Courtyard"
EMF_PROP = "/Game/Variant_Shooter/Blueprints/Objects/BP_EMFProp_1"
PACKS = ["/Game/Greek_island", "/Game/PolygonCasino", "/Game/City_of_Brass_Enviroment",
         "/Game/BigTriplexHouseVilla", "/Game/TropicalFoliage", "/Game/Kobo_Nature"]

# name, kind (emf|static), target max-dim (uu), x, y, face-center
# half=3300; balconies at +-2750; floor rim ~+-2400; north entry gap at -Y.
PLAN = [
    # entry flanking statues (<= human) + braziers
    ("SM_Athena",      "static", 190, -460, -2900, True),
    ("SM_Zeus",        "static", 190,  460, -2900, True),
    ("SM_Brazier",     "emf",    120, -760, -2900, False),
    ("SM_Brazier",     "emf",    120,  760, -2900, False),
    # corner clusters: planter + potted plant
    ("Corridor_Gardens_Planter01", "emf", 120, -2300, -2300, False),
    ("SM_Prop_Pot_Plants_01",      "emf", 120, -2050, -2300, False),
    ("Corridor_Gardens_Planter01", "emf", 120,  2300, -2300, False),
    ("SM_Prop_Pot_Plants_02",      "emf", 120,  2050, -2300, False),
    ("Corridor_Gardens_Planter01", "emf", 120, -2300,  2300, False),
    ("SM_Prop_Pot_Plants_03",      "emf", 120, -2050,  2300, False),
    ("Corridor_Gardens_Planter01", "emf", 120,  2300,  2300, False),
    ("SM_Prop_Pot_Plants_04",      "emf", 120,  2050,  2300, False),
    # benches against W / E / S perimeter, facing in
    ("SM_Prop_Bench_Pew_01", "emf", 165, -2350, 0,     True),
    ("SM_Prop_Bench_Pew_01", "emf", 165,  2350, 0,     True),
    ("SM_Prop_Bench_Pew_01", "emf", 165,  0,    2350,  True),
    # podium skirt: braziers + a single vase accent
    ("SM_Brazier",    "emf", 120,  1050, 0,    False),
    ("SM_Brazier",    "emf", 120, -1050, 0,    False),
    ("SM_Brazier",    "emf", 120,  0,    1050, False),
    ("SM_Big_Jar_01", "emf", 110,  0,   -1050, False),
]


def log(m):
    unreal.log("{} {}".format(TAG, m))


def warn(m):
    unreal.log_warning("{} {}".format(TAG, m))


def resolve(ar, name, cache):
    if name in cache:
        return cache[name]
    for r in PACKS:
        try:
            for d in ar.get_assets_by_path(r, True, False):
                if str(d.asset_name) == name:
                    p = "{}.{}".format(str(d.package_name), name)
                    cache[name] = p
                    return p
        except Exception:
            pass
    cache[name] = None
    return None


def fit_scale(mesh, target):
    try:
        e = mesh.get_bounds().box_extent
        m = max(e.x * 2.0, e.y * 2.0, e.z * 2.0)
        return (target / m) if m > 1.0 else 1.0
    except Exception:
        return 1.0


def main():
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    if ues.get_game_world() is not None:
        warn("PIE running - abort")
        return
    w = ues.get_editor_world()
    if (w.get_package().get_name() if w else "") != DST:
        warn("Open {} first - not loading (your edits stay safe)".format(DST))
        return
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    eal = unreal.EditorAssetLibrary
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    try:
        ar.wait_for_completion()
    except Exception:
        pass

    # clear ONLY my own decor sublayer (the amphora spam) - nothing else
    removed = 0
    for a in list(eas.get_all_level_actors()):
        try:
            if ART_TAG in [str(t) for t in a.tags] and "decor" in a.get_actor_label():
                eas.destroy_actor(a)
                removed += 1
        except Exception:
            pass
    log("Cleared {} old decor actors (mine)".format(removed))
    # remove any UNTAGGED EMF props (orphans from a prior failed set_static_mesh)
    strays = 0
    for a in list(eas.get_all_level_actors()):
        try:
            if "EMFProp" in a.get_class().get_name() and len(list(a.tags)) == 0:
                eas.destroy_actor(a)
                strays += 1
        except Exception:
            pass
    if strays:
        log("Removed {} untagged EMF strays".format(strays))

    bp = eal.load_blueprint_class(EMF_PROP)
    if bp is None:
        warn("EMF prop class missing: {}".format(EMF_PROP))
    cache = {}
    placed = 0
    misses = []
    for i, (name, kind, target, x, y, face) in enumerate(PLAN):
        path = resolve(ar, name, cache)
        if not path:
            misses.append(name)
            continue
        mesh = eal.load_asset(path)
        if mesh is None:
            misses.append(name)
            continue
        s = fit_scale(mesh, target)
        yaw = math.degrees(math.atan2(-y, -x)) if face else float((i * 47) % 360)
        try:
            if kind == "emf" and bp is not None:
                a = eas.spawn_actor_from_class(bp, unreal.Vector(x, y, 30.0),
                                               unreal.Rotator(0.0, 0.0, yaw))
                smc = a.get_component_by_class(unreal.StaticMeshComponent)
                ok = False
                if smc is not None:
                    try:
                        smc.set_static_mesh(mesh)
                        ok = True
                    except Exception as e:
                        warn("set_mesh {} ({}): {}".format(name, path, e))
                if not ok:
                    eas.destroy_actor(a)   # don't leave a default-mesh yellow box
                    misses.append(name + "(not-a-mesh)")
                    continue
            else:
                a = eas.spawn_actor_from_object(mesh, unreal.Vector(x, y, 0.0),
                                                unreal.Rotator(0.0, 0.0, yaw))
            a.set_actor_scale3d(unreal.Vector(s, s, s))
            tags = list(a.tags)
            tags.append(unreal.Name(ART_TAG))
            a.set_editor_property("tags", tags)
            a.set_actor_label("ART_A2_Courtyard_decor_{}_{}".format(name, i))
            a.set_folder_path("A2_Courtyard_Art/Decor")
            placed += 1
        except Exception as e:
            warn("place {} failed: {}".format(name, e))
    log("Placed {} props (scaled to ~target). Misses: {}".format(placed, misses))
    log("ADD_PROPS DONE (NOT saved - review and Ctrl+S to keep)")


main()
