# Applies the Biome1 island ART PASS authored by make_biome1_weightmaps.py
# to the OPEN Lvl_Biome1_Island: Synty landscape material + 4 painted layers
# (imported weight masks) + the deterministic foliage plan.
#
# THREE PHASES - run each as its own console command (the game thread blocks
# for the whole run; one monolith would risk the HTTP client timeout):
#   py apply_biome1_art_pass.py --phase=1   material + layer infos (+verify)
#   py apply_biome1_art_pass.py --phase=2   weight mask import (+probe verify)
#   py apply_biome1_art_pass.py --phase=3   foliage replay (+verify) + save
#
# Idempotent: phase 1/2 are absolute writes; phase 3 first removes ONLY the
# foliage types listed in the plan, then re-adds them (author's hand-painted
# types of OTHER assets are never touched). The island map must already be
# the open editor world - this script NEVER loads levels (crash classes
# 2026-06-11). Log tag: [ART_PASS]

import json
import os
import sys

import unreal

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.append(TOOLS_DIR)
import build_arena as ba  # noqa: E402

LEVEL_PATH = "/Game/Variant_Shooter/Arenas/Biome1/Island/Lvl_Biome1_Island"
LS_LABEL = "Biome1Island"
MAT_INST = ("/Game/PolygonNatureBiomes/PNB_Tropical_Jungle/LandscapeMaterial/"
            "M_Landscape_base_Inst")
LI_DIR = "/Game/Variant_Shooter/Arenas/Biome1/Island"
# 'Base' (LB_AlphaBlend in the Synty master) MUST be allocated, or every
# all-HeightBlend pixel renders BLACK (caught live 2026-06-12). Import order
# in phase 2: Base FIRST (its import renormalizes by dumping the deficit
# into the FIRST landscape layer - importing it last corrupts the real
# masks), then the four real masks overwrite per-pixel weights exactly.
LAYERS = ["Base", "Sand_01", "Rockwall", "Grass", "Grass_Clovers"]
ISLAND_DIR = os.path.join(TOOLS_DIR, "Island")
PLAN_PATH = os.path.join(ISLAND_DIR, "biome1_foliage_plan.json")
AUTHOR_SUBLEVELS = ["/Game/Variant_Shooter/Arenas/ArenaDebug/ArenaLightingDebug3",
                    "/Game/Variant_Shooter/Arenas/Biome1/RunLogic/Lvl_RunLogic"]
BATCH = 600


def log(msg):
    unreal.log("[ART_PASS] {}".format(msg))


def warn(msg):
    unreal.log_warning("[ART_PASS] {}".format(msg))


def guards():
    if hasattr(ba, "ensure_safe_to_build"):
        ba.ensure_safe_to_build()
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    pkg = world.get_package().get_name() if world else "?"
    if pkg != LEVEL_PATH:
        raise RuntimeError("Open world is {} - open {} in the editor first; "
                           "this script never loads levels".format(pkg, LEVEL_PATH))
    if not unreal.LandscapeService.landscape_exists(LS_LABEL):
        raise RuntimeError("Landscape '{}' missing - run build_biome_island.py"
                           .format(LS_LABEL))
    # stale-import guard: the plan carries probes of the PNG it was baked
    # from; if the LIVE landscape disagrees (e.g. the heightmap was reworked
    # but not re-imported, or vice versa), painting would misalign
    if os.path.isfile(PLAN_PATH):
        with open(PLAN_PATH, encoding="utf-8") as f:
            probes = json.load(f).get("height_probes", [])
        bad = 0
        for p in probes:
            s = unreal.LandscapeService.get_height_at_location(
                LS_LABEL, float(p["x"]), float(p["y"]))
            tol = max(90.0, abs(float(p["want"])) * 0.05)
            if not s.valid or abs(s.height - float(p["want"])) > tol:
                warn("probe {}: live {} vs plan {}".format(
                    p["label"], s.height if s.valid else "INVALID", p["want"]))
                bad += 1
        if bad:
            raise RuntimeError("{} height probe(s) mismatch - live landscape "
                               "!= plan heightmap, resync before painting"
                               .format(bad))
        log("live terrain matches the plan heightmap ({} probes)".format(
            len(probes)))
    return world


def layer_info_paths():
    paths = {}
    for layer in LAYERS:
        candidate = "{}/LI_{}".format(LI_DIR, layer)
        if unreal.LandscapeMaterialService.layer_info_exists(candidate):
            paths[layer] = candidate
            continue
        res = unreal.LandscapeMaterialService.create_layer_info_object(layer, LI_DIR)
        if not getattr(res, "success", False):
            raise RuntimeError("create_layer_info_object({}) failed: {}".format(
                layer, getattr(res, "error_message", "?")))
        paths[layer] = res.asset_path
        log("LayerInfo created: {}".format(res.asset_path))
    return paths


def phase1():
    guards()
    paths = layer_info_paths()
    ok = unreal.LandscapeMaterialService.assign_material_to_landscape(
        LS_LABEL, MAT_INST, paths)
    log("assign_material_to_landscape -> {}".format(ok))
    info = unreal.LandscapeService.get_landscape_info(LS_LABEL)
    mat = getattr(info, "material_path", "?")
    names = []
    for l in (getattr(info, "layers", None) or []):
        names.append(str(getattr(l, "layer_name", l)))
    log("VERIFY material: {}".format(mat))
    log("VERIFY layers: {}".format(names))
    if MAT_INST.split("/")[-1] not in str(mat):
        raise RuntimeError("material did not stick (still {})".format(mat))
    missing = [l for l in LAYERS if l not in " ".join(names)]
    if missing:
        raise RuntimeError("layers missing after assign: {}".format(missing))
    for p in paths.values():
        unreal.EditorAssetLibrary.save_asset(p)
    log("PHASE 1 OK - material + {} layer infos (saved)".format(len(paths)))
    # exact return shape of the weight probe, for phase 2 parsing
    fn = unreal.LandscapeService.get_layer_weights_at_location
    log("probe doc: {}".format((fn.__doc__ or "?").replace("\n", " | ")[:300]))


def phase2():
    guards()
    for layer in LAYERS:
        png = os.path.join(ISLAND_DIR, "biome1_weight_{}.png".format(layer))
        if not os.path.isfile(png):
            raise RuntimeError("mask missing: {} - run make_biome1_weightmaps.py"
                               .format(png))
        res = unreal.LandscapeService.import_weight_map(LS_LABEL, layer, png)
        if not getattr(res, "success", False):
            raise RuntimeError("import_weight_map({}) failed: {}".format(
                layer, getattr(res, "error_message", "?")))
        log("imported {} <- {}".format(layer, os.path.basename(png)))
    probes = [("G1 road/apron (sand)", -4840.0, -14970.0, "Sand_01", 0.45),
              ("island mid (grass)", 6000.0, 16000.0, "Grass", 0.45),
              ("open sea bed (sand)", 70000.0, -70000.0, "Sand_01", 0.9),
              ("cape cliff (rock?)", 44000.0, -600.0, "Rockwall", None)]
    bad = 0
    for label, x, y, want_layer, want_min in probes:
        try:
            w = unreal.LandscapeService.get_layer_weights_at_location(LS_LABEL, x, y)
        except Exception as e:
            warn("probe {} call failed: {}".format(label, e))
            bad += 1
            continue
        log("probe {} raw: {}".format(label, str(w)[:400]))
        got = None
        try:
            for entry in w:
                nm = str(getattr(entry, "layer_name", getattr(entry, "name", entry)))
                wt = float(getattr(entry, "weight", getattr(entry, "value", -1.0)))
                # exact match: "Grass" is a substring of "Grass_Clovers" and a
                # contains-check let the later 0.0 entry overwrite the real hit
                if nm == want_layer:
                    got = wt
        except Exception as e:
            warn("probe {} parse failed: {}".format(label, e))
        if want_min is not None:
            if got is None or got < want_min:
                warn("probe {}: {} weight {} (want >= {})".format(
                    label, want_layer, got, want_min))
                bad += 1
            else:
                log("probe {}: {} = {:.2f} OK".format(label, want_layer, got))
    if bad:
        warn("{} weight probe(s) off - inspect before phase 3".format(bad))
    else:
        log("PHASE 2 OK - 4 masks imported, probes match")


def phase3():
    world = guards()
    if not os.path.isfile(PLAN_PATH):
        raise RuntimeError("plan missing: {}".format(PLAN_PATH))
    with open(PLAN_PATH, encoding="utf-8") as f:
        plan = json.load(f)
    # foliage lands in the CURRENT level - force the persistent island level.
    # EditorLevelUtils.make_level_current wants a LevelStreaming (caught live
    # 2026-06-12: passing the ULevel left RunLogic current and 3775 instances
    # moved into the author's logic sublevel) - LevelEditorSubsystem takes the
    # ULevel directly. HARD ABORT if the switch cannot be verified.
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    les.set_current_level_by_name("Lvl_Biome1_Island")   # probed 2026-06-12
    cur_pkg = "?"
    try:
        cur_pkg = les.get_current_level().get_package().get_name()
    except Exception as e:
        warn("get_current_level failed: {}".format(e))
    if cur_pkg != LEVEL_PATH:
        raise RuntimeError("current level is {} (want {}) - foliage would "
                           "land in a sublevel, aborting".format(cur_pkg, LEVEL_PATH))
    log("current level -> {}".format(cur_pkg))

    # clean stray IFAs BEFORE adding: FoliageService appends to the first
    # existing InstancedFoliageActor in the world, NOT the current level's
    # (caught live 2026-06-12: with a leftover RunLogic IFA present, all 3775
    # fresh instances landed in it again). RunLogic never legitimately holds
    # foliage; with no IFA anywhere the service creates one in the CURRENT
    # (= island) level.
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for a in list(eas.get_all_level_actors()):
        if a and a.get_class().get_name() == "InstancedFoliageActor" and \
                a.get_package().get_name().endswith("/Lvl_RunLogic"):
            eas.destroy_actor(a)
            log("destroyed stray IFA in Lvl_RunLogic (pre-add cleanup)")

    params_by_type = plan.get("type_params", {})
    grand_add = grand_rej = 0
    for ft, pts in sorted(plan.get("instances", {}).items()):
        if not unreal.FoliageService.foliage_type_exists(ft):
            warn("foliage type missing, skipped: {}".format(ft))
            continue
        rem = unreal.FoliageService.remove_all_foliage_of_type(ft)
        removed = getattr(rem, "instances_removed", 0)
        p = params_by_type.get(ft, {})
        mn = float(p.get("min_scale", 0.9))
        mx = float(p.get("max_scale", 1.2))
        align = bool(p.get("align_to_normal", True))
        added = rejected = 0
        for i in range(0, len(pts), BATCH):
            locs = [unreal.Vector(q[0], q[1], q[2]) for q in pts[i:i + BATCH]]
            res = unreal.FoliageService.add_foliage_instances(
                ft, locs, mn, mx, align, True, True)
            if not getattr(res, "success", False):
                warn("add_foliage_instances({}) batch failed: {}".format(
                    ft, getattr(res, "error_message", "?")))
                continue
            added += res.instances_added
            rejected += res.instances_rejected
        grand_add += added
        grand_rej += rejected
        log("{}: planned {}, removed {}, added {}, rejected {}".format(
            ft.rsplit("/", 1)[-1], len(pts), removed, added, rejected))
    log("FOLIAGE TOTAL: added {}, rejected {} (plan {})".format(
        grand_add, grand_rej, sum(len(v) for v in plan.get("instances", {}).values())))

    ifa_pkgs = []
    for a in eas.get_all_level_actors():
        if a and a.get_class().get_name() == "InstancedFoliageActor":
            ifa_pkgs.append(a.get_package().get_name())
    log("VERIFY InstancedFoliageActor packages: {}".format(ifa_pkgs))
    live_total = 0
    for ft in unreal.FoliageService.list_foliage_types():
        live_total += ft.instance_count
    log("VERIFY live foliage instances: {}".format(live_total))
    if LEVEL_PATH not in ifa_pkgs or live_total < grand_add:
        raise RuntimeError("foliage did not stick to the island level "
                           "(IFAs {}, live {} < added {}) - NOT saving"
                           .format(ifa_pkgs, live_total, grand_add))

    dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    keep = [p for p in dirty
            if p.get_name() == LEVEL_PATH or p.get_name() in AUTHOR_SUBLEVELS]
    other = [p.get_name() for p in dirty if p not in keep]
    if keep:
        unreal.EditorLoadingAndSavingUtils.save_packages(keep, True)
        log("saved: {}".format([p.get_name() for p in keep]))
    if other:
        warn("left dirty (not ours to save): {}".format(other))
    log("PHASE 3 OK")


def main():
    phase = "all"
    for a in sys.argv[1:]:
        if a.startswith("--phase="):
            phase = a.split("=", 1)[1]
    if phase in ("1", "all"):
        phase1()
    if phase in ("2", "all"):
        phase2()
    if phase in ("3", "all"):
        phase3()
    log("RESULT: SUCCESS (phase {})".format(phase))


main()
