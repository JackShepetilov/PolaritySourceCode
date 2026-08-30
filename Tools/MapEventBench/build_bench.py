import unreal  # first line on purpose: execute_python_code only accepts code that starts with it

# Map event bench for Polarity.
#
# Builds the smallest level on which the whole middle of the core loop can be watched running:
# the run director, points of interest, mission windows, two headquarters that send squads at each
# other, sabotage, the final, and three extraction routes to pick from.
#
#                          [X_North]
#                              |
#                        [P_Mission_North]
#                              |
#   [HQ_A] --- [P_Mission_West] -- [P_Final] -- [P_Mission_East] --- [HQ_B]
#                              |
#              [P_Plain_South]  [Launch: the team starts here]
#                    /                        \
#              [X_West]                    [X_East]
#
# Nothing here is art. Distances are real (60-120 m between places) because the things being tested
# are timings: how long a squad takes to walk somewhere, how long a capture runs, whether a mission
# window is still open when the team arrives.
#
# Usage (editor running):
#   Tools/mcp.sh py Source/Tools/MapEventBench/build_bench.py
#
# Idempotent: everything it makes carries the tag below and is removed before a rebuild. Existing
# untagged actors, and every other level, are left alone.
#
# Log filter tag: [BENCH]

import os

import unreal

LEVEL_PATH = "/Game/MapEventBench/L_MapEventBench"
TAG = "MapEventBench"
CUBE_MESH = "/Engine/BasicShapes/Cube.Cube"

GROUND = dict(size=(32000, 32000, 100), z=-50)

# Navmesh tiles. Tiles are area over TileSizeUU squared, and a dynamic navmesh builds a few of them
# per frame: at the default 1000 uu a 640 m floor is 4096 tiles and takes minutes, during which
# every squad honestly logs "MoveTo FAILED (no path?)" and the bench looks broken. 2000 uu tiles on
# a 320 m floor is 16x16 = 256, which is up before the first sortie leaves. Flat ground loses
# nothing to the coarser tile.
NAV_TILE_UU = 2000.0

CURRENCY_BP = "/Game/Variant_Shooter/Blueprints/Pickups/Currency/BP_CurrencyPickup"
AMMO_BP = "/Game/Variant_Shooter/Blueprints/Pickups/Ammo/BP_AmmoPickup"

# The game's own mode. Without this override the level runs the engine template's
# BP_FirstPersonGameMode, which spawns the wrong pawn and none of the run wiring.
GAME_MODE_BP = "/Game/Variant_Shooter/Blueprints/BP_ShooterGameMode"

CYLINDER_MESH = "/Engine/BasicShapes/Cylinder.Cylinder"
SPHERE_MESH = "/Engine/BasicShapes/Sphere.Sphere"
CONE_MESH = "/Engine/BasicShapes/Cone.Cone"
MATERIAL_DIR = "/Game/MapEventBench/Materials"

# Every place gets a coloured disc the size of its influence radius and a marker in the middle.
# The C++ actors are invisible by design - a sphere gizmo and nothing else - which is correct for
# shipping and useless for a bench: the first run of this level looked like an empty floor.
COLORS = dict(
    mission=(1.00, 0.55, 0.10),
    plain=(0.45, 0.55, 0.65),
    final=(1.00, 0.85, 0.35),
    hq_a=(0.10, 0.60, 0.75),
    hq_b=(0.85, 0.20, 0.20),
    exitpad=(0.20, 0.80, 0.40),
    sabotage=(0.95, 0.80, 0.15),
    launch=(0.60, 0.40, 0.90),
)

GARRISON_A = "/Game/Squads/DA_Bench_Garrison_A"
GARRISON_B = "/Game/Squads/DA_Bench_Garrison_B"
SORTIE_A = "/Game/Squads/DA_Bench_Sortie_A"
SORTIE_B = "/Game/Squads/DA_Bench_Sortie_B"

TEAM_PLAYERS = 0
TEAM_A = 1
TEAM_B = 2
TEAM_NEUTRAL = 255

# Places. Y grows south, so the team starts south and the final sits in the middle.
#
# 120 m from a headquarters to the final, 60-90 m between neighbouring points. Short enough that a
# squad walking at 5 m/s crosses in under half a minute, which is what makes the bench watchable,
# and long enough that arriving late is a real thing that happens.
FINAL = (0.0, 0.0)
MISSION_WEST = (-6000.0, -3000.0)
MISSION_EAST = (6000.0, -3000.0)
MISSION_NORTH = (0.0, -8000.0)
PLAIN_SOUTH = (-4500.0, 4500.0)
HQ_A_POS = (-12000.0, 0.0)
HQ_B_POS = (12000.0, 0.0)
LAUNCH = (0.0, 10000.0)


def log(msg):
    unreal.log("[BENCH] {}".format(msg))


def vec(x, y, z):
    return unreal.Vector(x, y, z)


def level_disk_path(path):
    root = unreal.Paths.project_content_dir()
    return os.path.join(root, path.replace("/Game/", "", 1) + ".umap")


def open_or_create_level(les):
    # A scripted load silently discards dirty maps. Refuse loudly rather than eat the author's work.
    dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    foreign = [p for p in dirty if p.get_name() != LEVEL_PATH]
    if foreign:
        raise RuntimeError("UNSAVED map changes ({}) - save everything in the editor first".format(
            ", ".join(p.get_name() for p in foreign)))

    try:
        unreal.AssetRegistryHelpers.get_asset_registry().wait_for_completion()
    except Exception:
        pass

    # Standing in a freshly made, never saved bench: creating it a second time asserts the editor
    # dead with "World Memory Leaks", because the world being replaced is the one being built.
    current = les.get_current_level()
    current_name = current.get_outer().get_name() if current else ""
    if current_name.endswith(LEVEL_PATH.rsplit("/", 1)[-1]):
        log("Already in {} - reusing the open world".format(current_name))
        return

    if os.path.isfile(level_disk_path(LEVEL_PATH)) or unreal.EditorAssetLibrary.does_asset_exist(LEVEL_PATH):
        if not les.load_level(LEVEL_PATH):
            raise RuntimeError("Failed to load " + LEVEL_PATH)
        log("Loaded existing level")
    else:
        if not les.new_level(LEVEL_PATH):
            raise RuntimeError("Failed to create " + LEVEL_PATH)
        log("Created new level")


def clear_tagged(eas):
    removed = 0
    for actor in list(eas.get_all_level_actors()):
        try:
            if TAG in [str(t) for t in actor.tags]:
                eas.destroy_actor(actor)
                removed += 1
        except Exception:
            pass
    log("Removed {} previously generated actors".format(removed))


def color_material(key):
    """One material instance per colour, made once and reused. Discs and markers are the only way
    the layout is readable from inside the game."""
    path = "{}/MI_Bench_{}".format(MATERIAL_DIR, key)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path)

    parent = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/BasicShapeMaterial")
    factory = unreal.MaterialInstanceConstantFactoryNew()
    mic = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        "MI_Bench_{}".format(key), MATERIAL_DIR, unreal.MaterialInstanceConstant, factory)
    unreal.MaterialEditingLibrary.set_material_instance_parent(mic, parent)
    r, g, b = COLORS[key]
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(
        mic, "Color", unreal.LinearColor(r, g, b, 1.0))
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    log("created {}".format(path))
    return mic


def marker(eas, mesh_path, pos, scale, color_key, label):
    """A visible lump of geometry with no collision. No collision on purpose: markers are there to
    be looked at, and a bench whose landmarks also block navmesh generation tests the wrong thing."""
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    a = eas.spawn_actor_from_class(unreal.StaticMeshActor, vec(pos[0], pos[1], pos[2]))
    comp = a.static_mesh_component
    comp.set_static_mesh(mesh)
    comp.set_material(0, color_material(color_key))
    # The profile, not just SetCollisionEnabled: the runtime call does not survive being saved into
    # the level, and a marker that keeps BlockAll is a wall. One of these cones stood on the player
    # start and made every spawn fail with "SpawnActor failed because of collision".
    comp.set_collision_profile_name("NoCollision")
    comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    a.set_actor_scale3d(vec(scale[0], scale[1], scale[2]))
    return finish(a, label, "MapEvents/Markers")


def radius_disc(eas, pos, radius, color_key, label):
    # The base cylinder is 100 uu across, so the scale is diameter over 100.
    s = (radius * 2.0) / 100.0
    return marker(eas, CYLINDER_MESH, (pos[0], pos[1], 5.0), (s, s, 0.06), color_key, label)


def finish(actor, label, folder):
    actor.set_actor_label(label)
    actor.set_folder_path(folder)
    tags = list(actor.tags)
    tags.append(unreal.Name(TAG))
    actor.set_editor_property("tags", tags)
    return actor


# ==================== loadouts ====================

def make_loadout(name, team, task, npc_path, count):
    """One squad composition. Reused between runs: the bench should not grow a new data asset
    every time it is rebuilt."""
    path = "/Game/Squads/{}".format(name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        asset = unreal.load_asset(path)
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.SquadLoadout)
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, "/Game/Squads", unreal.SquadLoadout, factory)
        log("created {}".format(path))

    npc = unreal.EditorAssetLibrary.load_blueprint_class(npc_path)
    if not npc:
        raise RuntimeError("NPC class missing: " + npc_path)

    entry = unreal.SquadLoadoutEntry()
    entry.set_editor_property("npc_class", npc)
    entry.set_editor_property("count", count)
    entry.set_editor_property("provides_commander", True)

    asset.set_editor_property("members", [entry])
    asset.set_editor_property("faction_team_id", team)
    asset.set_editor_property("initial_task", task)
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    return asset


# ==================== points ====================

def loot_entry(bp_path, count, is_money, scatter=0.0):
    cls = unreal.EditorAssetLibrary.load_blueprint_class(bp_path)
    if not cls:
        raise RuntimeError("Pickup class missing: " + bp_path)
    e = unreal.PoiLootEntry()
    e.set_editor_property("pickup_class", cls)
    e.set_editor_property("count", count)
    e.set_editor_property("is_money", is_money)
    e.set_editor_property("scatter_radius", scatter)
    return e


def spawn_poi(eas, tag, pos, role, team, garrison=None, loot=None, radius=2000.0,
              mission_kind=None, reward=None, prize=None, prize_life=0.0):
    poi = eas.spawn_actor_from_class(unreal.PoiActor, vec(pos[0], pos[1], 100.0))
    poi.set_editor_property("poi_tag", unreal.Name(tag))
    poi.set_editor_property("poi_role", role)
    poi.set_editor_property("starting_team", team)
    poi.set_editor_property("influence_radius", radius)
    if garrison:
        poi.set_editor_property("garrison_loadout", garrison)
        poi.set_editor_property("garrison_scatter_radius", 800.0)
    if loot:
        poi.set_editor_property("loot", loot)
    if mission_kind is not None:
        poi.set_editor_property("mission_kind", mission_kind)
    if reward is not None:
        poi.set_editor_property("mission_reward", reward)
    if prize is not None:
        poi.set_editor_property("prize", prize)
        poi.set_editor_property("prize_lifetime_seconds", prize_life)

    key = {unreal.PoiRole.MISSION: "mission", unreal.PoiRole.PLAIN: "plain",
           unreal.PoiRole.FINAL: "final"}.get(role, "plain")
    radius_disc(eas, pos, radius, key, "DISC_{}".format(tag))
    # A cone for a mission, a squat cylinder for anything else: readable from across the map, which
    # is where the decision about going there is actually made.
    if role == unreal.PoiRole.MISSION:
        marker(eas, CONE_MESH, (pos[0], pos[1], 0.0), (4.0, 4.0, 12.0), key, "MARK_{}".format(tag))
    else:
        marker(eas, CYLINDER_MESH, (pos[0], pos[1], 0.0), (6.0, 6.0, 8.0), key, "MARK_{}".format(tag))

    return finish(poi, "POI_{}".format(tag), "MapEvents/Points")


def spawn_hq(eas, tag, pos, team, sortie_loadout, garrison_loadout, loot):
    hq = eas.spawn_actor_from_class(unreal.FactionHq, vec(pos[0], pos[1], 100.0))
    hq.set_editor_property("poi_tag", unreal.Name(tag))
    hq.set_editor_property("faction_team_id", team)
    hq.set_editor_property("influence_radius", 3000.0)

    # A headquarters is a place, so it has the two things every place has. Without a garrison it is
    # a pair of breakable boxes standing in a field, and the sabotage costs the team nothing.
    hq.set_editor_property("garrison_loadout", garrison_loadout)
    hq.set_editor_property("garrison_scatter_radius", 1100.0)
    hq.set_editor_property("loot", loot)

    sortie = unreal.SortieEntry()
    sortie.set_editor_property("loadout", sortie_loadout)
    sortie.set_editor_property("is_vehicle", False)
    sortie.set_editor_property("weight", 1.0)
    hq.set_editor_property("sorties", [sortie])
    hq.set_editor_property("first_sortie_delay_seconds", 20.0)
    hq.set_editor_property("sortie_interval_seconds", 60.0)
    hq.set_editor_property("sortie_scatter_radius", 600.0)
    # A cap, because the population brake belongs to the faction director and that does not exist
    # yet: an uncapped bench had 56 pawns on the map inside four minutes.
    hq.set_editor_property("max_sorties", 3)

    # Two things to break, one per consequence worth watching on a bench.
    targets = []
    for i, (kind, dx) in enumerate(((unreal.SabotageKind.REINFORCEMENTS, -900.0),
                                    (unreal.SabotageKind.VEHICLES, 900.0))):
        t = eas.spawn_actor_from_class(unreal.SabotageTarget, vec(pos[0] + dx, pos[1] + 900.0, 100.0))
        t.set_editor_property("kind", kind)
        t.set_editor_property("health", 200.0)
        finish(t, "SAB_{}_{}".format(tag, i), "MapEvents/Sabotage")
        targets.append(t)
        marker(eas, CUBE_MESH, (pos[0] + dx, pos[1] + 900.0, 150.0),
               (3.0, 3.0, 3.0), "sabotage", "MARK_SAB_{}_{}".format(tag, i))

    hq.set_editor_property("sabotage_targets", targets)

    key = "hq_a" if team == TEAM_A else "hq_b"
    radius_disc(eas, pos, 3000.0, key, "DISC_{}".format(tag))
    marker(eas, CUBE_MESH, (pos[0], pos[1], 500.0), (10.0, 10.0, 10.0), key, "MARK_{}".format(tag))

    return finish(hq, "HQ_{}".format(tag), "MapEvents/Points")


def spawn_route(eas, tag, waypoints, exit_pos):
    exit_actor = eas.spawn_actor_from_class(unreal.ExtractionPoint, vec(exit_pos[0], exit_pos[1], 100.0))
    exit_actor.set_editor_property("exit_tag", unreal.Name("X_" + tag))
    exit_actor.set_editor_property("board_radius", 600.0)
    exit_actor.set_editor_property("board_seconds", 8.0)
    finish(exit_actor, "EXIT_{}".format(tag), "MapEvents/Extraction")

    route = eas.spawn_actor_from_class(unreal.ExtractionRoute, vec(0.0, 0.0, 100.0))
    route.set_editor_property("route_tag", unreal.Name("R_" + tag))
    route.set_editor_property("exit", exit_actor)
    route.set_editor_property("weight", 1.0)
    route.set_editor_property("chase_lead_seconds", 5.0)

    spline = route.get_editor_property("path")
    points = [vec(x, y, 100.0) for (x, y) in waypoints] + [vec(exit_pos[0], exit_pos[1], 100.0)]
    spline.set_spline_points(points, unreal.SplineCoordinateSpace.WORLD, True)

    radius_disc(eas, exit_pos, 700.0, "exitpad", "DISC_EXIT_{}".format(tag))
    marker(eas, SPHERE_MESH, (exit_pos[0], exit_pos[1], 500.0), (7.0, 7.0, 7.0), "exitpad",
           "MARK_EXIT_{}".format(tag))

    # Beads along the spline: the route is an editor-only line otherwise, and the whole point of it
    # is being able to see where the run out goes.
    for i in range(1, 11):
        p = route.get_point_along_route(i / 10.0)
        marker(eas, SPHERE_MESH, (p.x, p.y, 150.0), (1.6, 1.6, 1.6), "exitpad",
               "BEAD_{}_{}".format(tag, i))

    return finish(route, "ROUTE_{}".format(tag), "MapEvents/Extraction")


# ==================== world ====================

def ground_and_lights(eas):
    mesh = unreal.EditorAssetLibrary.load_asset(CUBE_MESH)
    floor = eas.spawn_actor_from_class(unreal.StaticMeshActor, vec(0, 0, GROUND["z"]))
    floor.static_mesh_component.set_static_mesh(mesh)
    floor.set_actor_scale3d(vec(GROUND["size"][0] / 100.0, GROUND["size"][1] / 100.0,
                                GROUND["size"][2] / 100.0))
    finish(floor, "BENCH_Ground", "Ground")

    sun = eas.spawn_actor_from_class(unreal.DirectionalLight, vec(0, 0, 4000.0),
                                     unreal.Rotator(roll=0.0, pitch=-45.0, yaw=30.0))
    finish(sun, "BENCH_Sun", "Lighting")
    finish(eas.spawn_actor_from_class(unreal.SkyLight, vec(0, 0, 4000.0)), "BENCH_SkyLight", "Lighting")

    nav = eas.spawn_actor_from_class(unreal.NavMeshBoundsVolume, vec(0, 0, GROUND["z"] + 1000.0))
    nav.set_actor_scale3d(vec(GROUND["size"][0] / 200.0, GROUND["size"][1] / 200.0, 20.0))
    finish(nav, "BENCH_NavBounds", "Nav")
    recast = eas.spawn_actor_from_class(unreal.RecastNavMesh, vec(0, 0, 0))
    # Dynamic, not static: a bench that needs somebody to press Build Paths before it means anything
    # will be run once without it, and the whole afternoon goes into "why does nobody walk". The
    # first run of this level produced exactly that: every squad logged MoveTo FAILED (no path?).
    recast.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)
    recast.set_editor_property("tile_size_uu", NAV_TILE_UU)
    finish(recast, "BENCH_RecastNavMesh", "Nav")
    log("Navmesh: DYNAMIC, {:.0f} uu tiles, {:.0f}x{:.0f} grid".format(
        NAV_TILE_UU, GROUND["size"][0] / NAV_TILE_UU, GROUND["size"][1] / NAV_TILE_UU))


def world_settings(les):
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    ws = world.get_world_settings()
    gm = unreal.EditorAssetLibrary.load_blueprint_class(GAME_MODE_BP)
    if not gm:
        raise RuntimeError("Game mode blueprint missing: " + GAME_MODE_BP)
    ws.set_editor_property("default_game_mode", gm)
    log("GameMode override -> {}".format(GAME_MODE_BP))


def run_entry(eas):
    # The launch point is what tells the game this map is a run, and the director reads the team's
    # insertion from it. No second marker for the player spawn: two that can disagree is a bug.
    # Tossed out of the sea, like the first map of a real run. Not a flourish: the starting weapon
    # is handed over in AShooterCharacter::Landed, and only while the run launch is in progress. A
    # bench with the toss switched off spawns an unarmed player who cannot join the war at all.
    #
    # Aimed north, at the final, with a short arc: v=2500 at 25 degrees carries about 49 m, which
    # puts the landing between the start and the middle of the map rather than inside somebody's
    # garrison.
    launch = eas.spawn_actor_from_class(
        unreal.RunLaunchPoint, vec(LAUNCH[0], LAUNCH[1], 200.0),
        unreal.Rotator(roll=0.0, pitch=25.0, yaw=-90.0))
    launch.set_editor_property("launch_from_sea", True)
    launch.set_editor_property("launch_speed", 2500.0)
    launch.set_editor_property("boss_intro", False)
    launch.set_editor_property("arena_index", 0)
    finish(launch, "BENCH_RunLaunchPoint", "MapEvents")

    finish(eas.spawn_actor_from_class(unreal.PlayerStart, vec(LAUNCH[0], LAUNCH[1], 200.0)),
           "BENCH_PlayerStart", "MapEvents")

    radius_disc(eas, LAUNCH, 900.0, "launch", "DISC_LAUNCH")
    # Beside the start, never on it. Belt and braces after the collision bug above: nothing tall
    # stands where a pawn has to appear.
    marker(eas, CONE_MESH, (LAUNCH[0] + 1200.0, LAUNCH[1], 0.0), (4.0, 4.0, 11.0), "launch", "MARK_LAUNCH")


def main():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    open_or_create_level(les)
    clear_tagged(eas)

    world_settings(les)
    ground_and_lights(eas)
    run_entry(eas)

    npc_a = "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_ShooterNPC"
    npc_b = npc_a

    gar_a = make_loadout("DA_Bench_Garrison_A", TEAM_A, unreal.SquadInitialTask.DEFEND, npc_a, 3)
    gar_b = make_loadout("DA_Bench_Garrison_B", TEAM_B, unreal.SquadInitialTask.DEFEND, npc_b, 3)
    sor_a = make_loadout("DA_Bench_Sortie_A", TEAM_A, unreal.SquadInitialTask.ATTACK, npc_a, 4)
    sor_b = make_loadout("DA_Bench_Sortie_B", TEAM_B, unreal.SquadInitialTask.ATTACK, npc_b, 4)

    # Money budget: five stacks on the whole map, which is the quarter-to-a-third of sixteen cells
    # the inventory contract is tuned around. The director prints the total, so this is checkable.
    reward = unreal.FinalConditions()
    reward.set_editor_property("wave_delta", -1)
    reward.set_editor_property("arrival_delay_seconds", 20.0)
    reward.set_editor_property("entry_quality", 1)

    spawn_poi(eas, "P_Mission_West", MISSION_WEST, unreal.PoiRole.MISSION, TEAM_A,
              garrison=gar_a, loot=[loot_entry(CURRENCY_BP, 1, True), loot_entry(AMMO_BP, 2, False)],
              mission_kind=unreal.MissionKind.ELIMINATION, reward=reward,
              prize=unreal.PoiPrize.TROPHY_WEAPON, prize_life=20.0)

    spawn_poi(eas, "P_Mission_East", MISSION_EAST, unreal.PoiRole.MISSION, TEAM_B,
              garrison=gar_b, loot=[loot_entry(CURRENCY_BP, 1, True), loot_entry(AMMO_BP, 2, False)],
              mission_kind=unreal.MissionKind.SABOTAGE, reward=reward,
              prize=unreal.PoiPrize.WRECKED_VEHICLE, prize_life=60.0)

    spawn_poi(eas, "P_Mission_North", MISSION_NORTH, unreal.PoiRole.MISSION, TEAM_A,
              garrison=gar_a, loot=[loot_entry(CURRENCY_BP, 1, True), loot_entry(AMMO_BP, 2, False)],
              mission_kind=unreal.MissionKind.DELIVERY, reward=reward,
              prize=unreal.PoiPrize.POINT_POWER, prize_life=0.0)

    spawn_poi(eas, "P_Plain_South", PLAIN_SOUTH, unreal.PoiRole.PLAIN, TEAM_NEUTRAL,
              loot=[loot_entry(CURRENCY_BP, 2, True), loot_entry(AMMO_BP, 2, False)])

    spawn_poi(eas, "P_Final", FINAL, unreal.PoiRole.FINAL, TEAM_NEUTRAL, radius=2500.0)

    # Ammo only: the money budget is spent, and a headquarters that also paid the best money would
    # make the other five points decorative.
    spawn_hq(eas, "HQ_A", HQ_A_POS, TEAM_A, sor_a, gar_a, [loot_entry(AMMO_BP, 3, False)])
    spawn_hq(eas, "HQ_B", HQ_B_POS, TEAM_B, sor_b, gar_b, [loot_entry(AMMO_BP, 3, False)])

    # Three ways out, drawn at random after the hold. Different lengths and directions on purpose:
    # a team that always leaves the same way has not been asked anything.
    spawn_route(eas, "North", [FINAL, (0.0, -4000.0), (0.0, -10000.0)], (0.0, -14000.0))
    spawn_route(eas, "West", [FINAL, (-4000.0, 3000.0), (-10000.0, 5500.0)], (-14000.0, 6500.0))
    spawn_route(eas, "East", [FINAL, (4000.0, 3000.0), (10000.0, 5500.0)], (14000.0, 6500.0))

    saved = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("Saved: {}".format(saved))
    log("Done. Level: {}".format(LEVEL_PATH))


if __name__ == "__main__":
    main()
