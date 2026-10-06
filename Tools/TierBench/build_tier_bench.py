"""Isolated three-tier benchmark. Mutates only TierBenchGen actors on its own saved map."""
import unreal

ROOT = r"C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8"
path = ROOT + "/Source/Tools/HomeBase/build_homebase.py"
hb = {"__name__": "tier_bench_lib"}
exec(compile(open(path, encoding="utf-8").read(), path, "exec"), hb)
LEVEL = "/Game/Prototype/TierBench/L_TierBench"
TAG = "TierBenchGen"
hb["LEVEL"] = LEVEL
hb["TAG_GEO"] = TAG


def main():
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
        raise RuntimeError("Stop PIE before building the benchmark")
    dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    if dirty and not hb["current_level_path"]().startswith(LEVEL + "."):
        raise RuntimeError("Save maps first: " + str([p.get_name() for p in dirty]))
    hb["step_level"]()
    hb["guard"]()
    hb["_clear"](TAG)
    box = hb["box"]
    box("TierFloor", 1500, 0, -50, 7000, 5000, 50, folder="TierBench/Ground")
    for index, y in enumerate((-650, 200, 650)):
        box("TierPillar" + str(index), 1700, y, 0, 200, 200, 320,
            mat="structure", folder="TierBench/Cover")
    for index, y in enumerate((-350, 350)):
        box("TierLowWall" + str(index), 1200, y, 0, 60, 400, 110,
            mat="cover", folder="TierBench/Cover")
    box("TierCornerA", 2050, 1100, 0, 60, 500, 300, mat="fence")
    box("TierCornerB", 2200, 1350, 0, 300, 60, 300, mat="fence")
    eas = hb["_eas"]()
    player = eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 110), unreal.Rotator())
    hb["_tag"](player, TAG, "TierPlayerStart", "TierBench/Gameplay")
    marker = eas.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(3000, 0, 110), unreal.Rotator(yaw=180))
    hb["_tag"](marker, TAG, "TierSpawn", "TierBench/Gameplay")
    marker.set_editor_property("tags", [unreal.Name(TAG), unreal.Name("TierBenchSpawn")])
    nav = eas.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector(1500, 0, 300))
    nav.set_actor_scale3d(unreal.Vector(36, 26, 6))
    hb["_tag"](nav, TAG, "TierNavBounds", "TierBench/Gameplay")
    # Reuse the author's lighting sublevel; do not create or edit lights.
    world = hb["_world"]()
    light = "/Game/Variant_Shooter/Arenas/ArenaDebug/ArenaLightingDebug3"
    if unreal.EditorAssetLibrary.does_asset_exist(light):
        if not any(str(level.get_outer().get_path_name()) == light
                   for level in unreal.EditorLevelUtils.get_levels(world)):
            unreal.EditorLevelUtils.add_level_to_world(world, light, unreal.LevelStreamingAlwaysLoaded)
    hb["_les"]().set_current_level_by_name("L_TierBench")
    hb["_les"]().save_current_level()
    step_navdata()
    print("[TIER_SETUP] saved", LEVEL)


def step_navdata():
    hb["guard"]()
    for actor in hb["_eas"]().get_all_level_actors():
        if isinstance(actor, unreal.RecastNavMesh):
            actor.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)
    hb["_les"]().save_current_level()
