import unreal  # first line on purpose: execute_python_code only accepts code that starts with it

# Map event bench for Polarity.
#
# Builds the smallest level on which the whole middle of the core loop can be watched running:
# the run director, points of interest, mission windows, two headquarters that send squads at each
# other, banners that can be broken, the final, and three extraction routes to pick from.
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

import math
import os
import random

import unreal

LEVEL_PATH = "/Game/MapEventBench/L_MapEventBench"
TAG = "MapEventBench"
CUBE_MESH = "/Engine/BasicShapes/Cube.Cube"

# 63000, not the old 64000: 63 quads x 10 components gives 631 vertices, and 63000 uu across them
# lands the landscape scale on a round 100, one vertex per metre, with no fractional scale to
# chase every time the grid changes.
GROUND = dict(size=(63000, 63000), plates=16)

LANDSCAPE_LABEL = "MapTerrain"
# __file__ is missing when the script is pasted into the editor console rather than imported, and a
# NameError at import time would take the whole build down before it printed anything.
_HERE = os.path.dirname(os.path.abspath(__file__)) if "__file__" in globals() else \
    r"C:\Users\Professional\Documents\Unreal Projects\Polarity_Main5_8\Source\Tools\MapEventBench"
HEIGHTMAP_PNG = os.path.join(_HERE, "heightmap.png")

# Terrain, in three flat steps with ramps between them.
#
# Not noise and not a smooth heightfield: the points have to sit on LEVEL ground, because a fight
# on a slope is a fight about the slope. So the world is a few plateaus, every point is pinned to
# one and the ground around it flattened to match, and the height only happens on the way between
# them. That is where it earns its keep: a ridge between two points breaks line of sight, which is
# the one thing a flat map cannot do and the reason nothing here has ever lost sight of the player.
TERRAIN = dict(step=400.0, levels=3, lobe=26000.0, flat_margin=7000.0)

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

# The level actors are placed as BLUEPRINTS, not as the C++ classes. Everything tunable then lives
# in an asset a designer can open - banner hit points, throw strength, sortie tables - instead of
# behind a rebuild. The bench and a hand-authored level place the same things.
BP_DIR = "/Game/Variant_Shooter/Blueprints/LevelBPs"
BP_POI = BP_DIR + "/BP_PoiActor"
BP_HQ = BP_DIR + "/BP_FactionHq"
BP_BANNER = BP_DIR + "/BP_Banner"
BP_EXIT = BP_DIR + "/BP_ExtractionPoint"
BP_ROUTE = BP_DIR + "/BP_ExtractionRoute"
BP_ANCHOR = BP_DIR + "/BP_LootAnchor"
BP_SHEET = BP_DIR + "/BP_LootSheet"

# Sheet kinds the bench makes for itself. They are bench DATA, not assets the author tunes, so the
# script creates them when they are missing and never touches them again: editing what is on a
# sheet in the editor has to stick across a rebuild of the bench.
SHEET_MONEY = BP_DIR + "/BP_Sheet_Money"
SHEET_AMMO = BP_DIR + "/BP_Sheet_Ammo"

# What a banner is made of when it comes apart. The mesh is a copy of the engine cube living in
# /Game so the fracture tool has somewhere to put GC_SM_BenchBanner next to it; both are made by
# make_banner_assets() below and then just sit there.
BANNER_MESH = "/Game/MapEventBench/SM_BenchBanner"
BANNER_GC = "/Game/MapEventBench/GC_SM_BenchBanner"
BREAK_VFX = "/Game/Effects/Particles/Explosion/NS_Grenade_Explosion"
BREAK_SOUND = "/Game/Audio/MetaSounds/sfx_Weapon_GrenadeExplosion_nl_meta"

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

# Where the factions come from. These are the AUTHOR's squads, not the bench's: A is people
# (riflemen behind a juggernaut, with a grenadier), B is machines (a tracked tank escorted by
# drones). The bench copies these compositions and only changes the task, so the two sides on the
# ground are the two sides that were designed.
SQUAD_A_SOURCE = "/Game/Squads/DA_SquadA_Line"
SQUAD_B_SOURCE = "/Game/Squads/DA_SquadB_Assault"
DRONE_BP = "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_FlyingDrone"
SHOOTER_BP = "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_ShooterNPC"

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
MISSION_WEST = (-12000.0, -6000.0)
MISSION_EAST = (12000.0, -6000.0)
MISSION_NORTH = (0.0, -16000.0)
PLAIN_SOUTH = (-9000.0, 9000.0)
# How far the headquarters block stands from the banner in front of it (cm).
HQ_LANDMARK_OFFSET = 4400.0

HQ_A_POS = (-24000.0, 0.0)
HQ_B_POS = (24000.0, 0.0)
LAUNCH = (0.0, 20000.0)


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


def marker(eas, mesh_path, pos, scale, color_key, label, solid=True):
    """A visible lump of geometry.

    Solid by default: a landmark you can walk through is a landmark that lies about the space, and
    the navmesh should route around it like it routes around anything else. The exceptions are the
    flat radius discs, which are floor paint - giving a six-centimetre plate collision only makes a
    lip to trip over.

    Whatever is chosen, it goes through the PROFILE and not just SetCollisionEnabled: the runtime
    call does not survive being saved into the level. That is how a decorative cone once kept
    BlockAll, stood on the player start, and made every spawn fail with "SpawnActor failed because
    of collision"."""
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    a = eas.spawn_actor_from_class(unreal.StaticMeshActor, vec(pos[0], pos[1], ground_z(pos[0], pos[1]) + pos[2]))
    comp = a.static_mesh_component
    comp.set_static_mesh(mesh)
    comp.set_material(0, color_material(color_key))
    if solid:
        comp.set_collision_profile_name("BlockAll")
        comp.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
    else:
        comp.set_collision_profile_name("NoCollision")
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    a.set_actor_scale3d(vec(scale[0], scale[1], scale[2]))
    return finish(a, label, "MapEvents/Markers")


def radius_disc(eas, pos, radius, color_key, label):
    # The base cylinder is 100 uu across, so the scale is diameter over 100.
    s = (radius * 2.0) / 100.0
    return marker(eas, CYLINDER_MESH, (pos[0], pos[1], 5.0), (s, s, 0.06), color_key, label,
                  solid=False)


def finish(actor, label, folder):
    actor.set_actor_label(label)
    actor.set_folder_path(folder)
    tags = list(actor.tags)
    tags.append(unreal.Name(TAG))
    actor.set_editor_property("tags", tags)
    return actor


# ==================== loadouts ====================

def loadout_asset(name):
    """Get or make the data asset. Reused between runs: the bench should not grow a new asset every
    time it is rebuilt."""
    path = "/Game/Squads/{}".format(name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path), path

    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.SquadLoadout)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, "/Game/Squads", unreal.SquadLoadout, factory)
    log("created {}".format(path))
    return asset, path


def entry(npc_path, count, commander=False):
    npc = unreal.EditorAssetLibrary.load_blueprint_class(npc_path)
    if not npc:
        raise RuntimeError("NPC class missing: " + npc_path)
    e = unreal.SquadLoadoutEntry()
    e.set_editor_property("npc_class", npc)
    e.set_editor_property("count", count)
    e.set_editor_property("provides_commander", commander)
    return e


def make_loadout(name, team, task, members):
    """A squad whose COMPOSITION is the author's, not the bench's.

    The first version of this file invented its own squads out of BP_ShooterNPC for both sides,
    which quietly deleted the only thing the whole layer exists to show: the two factions play
    differently. Faction A is people - riflemen behind a juggernaut with a grenadier - and faction B
    is machines - a tracked tank escorted by drones. Those two shapes come from DA_SquadA_Line and
    DA_SquadB_Assault, and anything here that disagrees with them is a bug."""
    asset, path = loadout_asset(name)
    asset.set_editor_property("members", members)
    asset.set_editor_property("faction_team_id", team)
    asset.set_editor_property("initial_task", task)
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    return asset


def copy_members(source_path):
    """The author's own composition, read straight out of their asset."""
    src = unreal.EditorAssetLibrary.load_asset(source_path)
    if not src:
        raise RuntimeError("Source loadout missing: " + source_path)
    return list(src.get_editor_property("members"))


# ==================== points ====================

def loot_entry(bp_path, count, is_money, value=None, amount_scale=1.0):
    """One line of a loot sheet.

    Rewritten 2026-09-04 to match FPoiLootEntry as it is now. The struct used to be
    (count, is_money, ammo_rounds); it is now a line of a pool ordered by Value, and the old three
    fields do not exist. The bench was aborting on `count` right after clearing the level, which is
    why the map came up with terrain and no points at all.

    Mapping used:
        count        -> CountMin = CountMax (a fixed line, not a range)
        is_money     -> Category Money / Ammo
        ammo_rounds  -> AmountScale, which is now a MULTIPLIER on the pickup's own payload rather
                        than an absolute number of rounds; 1.0 means "what the pickup is worth".

    Value is guessed, not derived: money 0.7, ammo 0.25. It decides where a line sits on the pool's
    single quality axis, which is a designer's opinion and not something a bench script can know.
    Retune it in the sheet assets."""
    cls = unreal.EditorAssetLibrary.load_blueprint_class(bp_path)
    if not cls:
        raise RuntimeError("Pickup class missing: " + bp_path)
    if value is None:
        value = 0.7 if is_money else 0.25

    e = unreal.PoiLootEntry()
    e.set_editor_property("pickup_class", cls)
    e.set_editor_property("category",
                          unreal.LootCategory.MONEY if is_money else unreal.LootCategory.AMMO)
    e.set_editor_property("value", float(value))
    e.set_editor_property("weight", 1.0)
    e.set_editor_property("count_min", int(count))
    e.set_editor_property("count_max", int(count))
    e.set_editor_property("amount_scale_min", float(amount_scale))
    e.set_editor_property("amount_scale_max", float(amount_scale))
    return e


def ensure_sheet_bp(path, items, layout_extent=90.0):
    """A kind of sheet: a blueprint of ALootSheet carrying what lies on it.

    Created only when missing. What is on a sheet is exactly the sort of thing the author opens the
    asset to retune, and a script that rewrote it on every rebuild would throw that away."""
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return path

    folder, name = path.rsplit("/", 1)
    factory = unreal.BlueprintFactory()
    # Off the base blueprint, not off the C++ class: the mat mesh and material are set once on
    # BP_LootSheet and every kind of sheet inherits them.
    factory.set_editor_property("parent_class", bp_class(BP_SHEET))
    bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, None, factory)
    if not bp:
        raise RuntimeError("Could not create sheet blueprint: " + path)

    cdo = unreal.get_default_object(bp.generated_class())
    cdo.set_editor_property("items", items)
    cdo.set_editor_property("layout_extent", layout_extent)
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    log("CREATED: {} ({} lines)".format(path, len(items)))
    return path


def sheet_option(path, weight=1.0):
    """One entry in an anchor's table: which sheet, and how often against the others."""
    cls = unreal.EditorAssetLibrary.load_blueprint_class(path)
    if not cls:
        raise RuntimeError("Sheet blueprint missing: " + path)
    o = unreal.LootSheetOption()
    o.set_editor_property("sheet_class", cls)
    o.set_editor_property("weight", weight)
    return o


def spawn_anchors(eas, tag, pos, radius, count, chance, mix):
    """Places where a sheet might be. The roll is the anchor's, once per run.

    Spread on the same golden-angle spiral the garrisons use, inside the inner half of the point:
    loot at the edge of an influence radius is loot nobody has to enter the fight for."""
    made = []
    for i in range(count):
        angle = i * 2.39996323
        r = radius * 0.5 * math.sqrt((i + 0.5) / max(1, count))
        x = pos[0] + r * math.cos(angle)
        y = pos[1] + r * math.sin(angle)
        a = eas.spawn_actor_from_class(bp_class(BP_ANCHOR), vec(x, y, ground_z(x, y) + 20.0))
        a.set_editor_property("anchor_tag", unreal.Name("{}_A{}".format(tag, i)))
        a.set_editor_property("chance", chance)
        a.set_editor_property("sheets", mix)
        made.append(finish(a, "ANCHOR_{}_{}".format(tag, i), "MapEvents/LootAnchors"))
    return made


def bp_class(path):
    """The blueprint's class, or a loud failure. A missing blueprint silently falling back to the
    C++ class would build a bench that ignores every value the author edits in the asset."""
    cls = unreal.EditorAssetLibrary.load_blueprint_class(path)
    if not cls:
        raise RuntimeError("Blueprint missing: {} (make it from the C++ class first)".format(path))
    return cls


def make_banner_assets():
    """The mesh a banner is, and the fractured version of it.

    Both are made once and then reused. The mesh is a copy of the engine cube because
    CreateGCFromStaticMesh writes GC_{Name} next to its source, and the source must not be in
    /Engine. Fracturing takes a couple of minutes the first time and no time ever after."""
    if not unreal.EditorAssetLibrary.does_asset_exist(BANNER_MESH):
        unreal.EditorAssetLibrary.duplicate_asset(CUBE_MESH.split(".")[0], BANNER_MESH)
        unreal.EditorAssetLibrary.save_asset(BANNER_MESH, only_if_is_dirty=False)
        log("created {}".format(BANNER_MESH))

    if not unreal.EditorAssetLibrary.does_asset_exist(BANNER_GC):
        mesh = unreal.EditorAssetLibrary.load_asset(BANNER_MESH)
        result = unreal.GCBatchCreatorLibrary.create_gc_from_static_mesh(mesh, 0, False)
        log("fracture: {}".format(result.get_editor_property("message")))
        unreal.EditorAssetLibrary.save_asset(BANNER_GC, only_if_is_dirty=False)


def spawn_banner(eas, tag, pos, color_key):
    """The thing standing in the middle of a place that can be broken.

    One per point and one per headquarters. Breaking it spills that point's loot, or drops that
    headquarters to its weakened squads: the banner itself has no opinion, see ABannerActor."""
    b = eas.spawn_actor_from_class(bp_class(BP_BANNER), vec(pos[0], pos[1], ground_z(*pos) + 150.0))
    b.set_editor_property("health", 400.0)
    b.set_editor_property("only_players_can_break", True)

    mesh = b.get_component_by_class(unreal.StaticMeshComponent)
    if mesh:
        # The /Game copy, not the engine cube: the pieces have to wear the same mesh the whole
        # thing was, and the fracture asset was built from this one.
        sm = unreal.EditorAssetLibrary.load_asset(BANNER_MESH)
        if sm:
            mesh.set_static_mesh(sm)
        mesh.set_material(0, color_material(color_key))

    gc = unreal.EditorAssetLibrary.load_asset(BANNER_GC)
    if gc:
        b.set_editor_property("banner_gc", gc)
    vfx = unreal.EditorAssetLibrary.load_asset(BREAK_VFX)
    if vfx:
        b.set_editor_property("break_vfx", vfx)
        b.set_editor_property("break_vfx_scale", 1.5)
    snd = unreal.EditorAssetLibrary.load_asset(BREAK_SOUND)
    if snd:
        b.set_editor_property("break_sound", snd)

    return finish(b, "BANNER_{}".format(tag), "MapEvents/Banners")


def spawn_poi(eas, tag, pos, role, team, garrison=None, radius=4000.0,
              mission_kind=None, reward=None, prize=None, prize_life=0.0,
              banner=False, anchors=0, anchor_chance=0.6, sheet_mix=None):
    poi = eas.spawn_actor_from_class(bp_class(BP_POI), vec(pos[0], pos[1], ground_z(*pos) + 100.0))
    poi.set_editor_property("poi_tag", unreal.Name(tag))
    poi.set_editor_property("poi_role", role)
    poi.set_editor_property("starting_team", team)
    poi.set_editor_property("influence_radius", radius)
    if garrison:
        poi.set_editor_property("garrison_loadout", garrison)
        poi.set_editor_property("garrison_scatter_radius", 1600.0)
    if mission_kind is not None:
        poi.set_editor_property("mission_kind", mission_kind)
    if reward is not None:
        poi.set_editor_property("mission_reward", reward)
    if prize is not None:
        poi.set_editor_property("prize", prize)
        poi.set_editor_property("prize_lifetime_seconds", prize_life)

    key = {unreal.PoiRole.MISSION: "mission", unreal.PoiRole.PLAIN: "plain",
           unreal.PoiRole.FINAL: "final"}.get(role, "plain")

    # Loot no longer hangs off the banner - it lies on sheets, and the sheets are rolled by the
    # anchors below. A banner is now just a breakable landmark on the points that ask for one.
    has_banner = banner
    if has_banner:
        poi.set_editor_property("banner", spawn_banner(eas, tag, pos, key))

    if anchors and sheet_mix:
        spawn_anchors(eas, tag, pos, radius, anchors, anchor_chance, sheet_mix)

    radius_disc(eas, pos, radius, key, "DISC_{}".format(tag))
    compound(eas, tag, pos, radius, key, seed=abs(hash(tag)) % 100000)

    # The centre marker exists only where there is no banner. Where there is one, the banner IS the
    # landmark, and a decorative cone at the same coordinates would be a second solid object
    # standing inside the first - which is exactly what the first version of this did.
    if not has_banner:
        if role == unreal.PoiRole.MISSION:
            marker(eas, CONE_MESH, (pos[0], pos[1], 0.0), (4.0, 4.0, 12.0), key, "MARK_{}".format(tag))
        else:
            marker(eas, CYLINDER_MESH, (pos[0], pos[1], 0.0), (6.0, 6.0, 8.0), key, "MARK_{}".format(tag))

    return finish(poi, "POI_{}".format(tag), "MapEvents/Points")


def spawn_hq(eas, tag, pos, team, sortie_loadout, weak_loadout, garrison_loadout,
             anchors=0, anchor_chance=0.6, sheet_mix=None, relief_loadout=None):
    hq = eas.spawn_actor_from_class(bp_class(BP_HQ), vec(pos[0], pos[1], ground_z(*pos) + 100.0))
    hq.set_editor_property("poi_tag", unreal.Name(tag))
    hq.set_editor_property("faction_team_id", team)
    hq.set_editor_property("influence_radius", 6000.0)

    # A headquarters is a place, so it has the two things every place has. Without a garrison it is
    # a pair of breakable boxes standing in a field, and the sabotage costs the team nothing.
    hq.set_editor_property("garrison_loadout", garrison_loadout)
    hq.set_editor_property("garrison_scatter_radius", 2200.0)

    if anchors and sheet_mix:
        spawn_anchors(eas, tag, pos, 6000.0, anchors, anchor_chance, sheet_mix)

    def sortie_entry(loadout):
        e = unreal.SortieEntry()
        e.set_editor_property("loadout", loadout)
        e.set_editor_property("weight", 1.0)
        return e

    # Two lists, and the banner decides which one this place is still entitled to.
    hq.set_editor_property("sorties", [sortie_entry(sortie_loadout)])
    hq.set_editor_property("weakened_sorties", [sortie_entry(weak_loadout)])
    hq.set_editor_property("first_sortie_delay_seconds", 12.0)
    hq.set_editor_property("sortie_interval_seconds", 40.0)
    hq.set_editor_property("sortie_scatter_radius", 600.0)

    # Reinforcement: what goes to a place this side holds and is losing.
    #
    # NOT the garrison asset, however obvious that looks. USquadSpawnSubsystem only advances squads
    # whose task is Attack, so a Defend loadout handed an objective spawns at the gate and stays
    # there all run. A relief force is the garrison's COMPOSITION with the marching task, which is
    # what relief_loadout is; without one this falls back to the assault list.
    hq.set_editor_property("reinforcement_sorties",
                           [sortie_entry(relief_loadout or sortie_loadout)])

    # No cap on sorties any more. Counting sorties never said anything about how crowded the map is,
    # which is the thing anybody actually cared about, and at three the entire war was over 2:20
    # into a run whose final does not open until minute fifteen.
    hq.set_editor_property("max_sorties", 0)

    # The real brake. 26 per faction, garrisons included: the bench garrisons seven places at
    # roughly three each, so a side carries about twenty standing and has room for two squads in
    # the field before it starts sending short ones.
    hq.set_editor_property("faction_population_cap", 26)
    hq.set_editor_property("min_sortie_members", 2)

    key = "hq_a" if team == TEAM_A else "hq_b"
    hq.set_editor_property("banner", spawn_banner(eas, tag, pos, "sabotage"))
    compound(eas, tag, pos, 6000.0, key, seed=abs(hash(tag)) % 100000)
    radius_disc(eas, pos, 6000.0, key, "DISC_{}".format(tag))
    # Off to one side of the banner, not on top of it: a headquarters still needs a block visible
    # from the other end of the map, and the banner needs to be walked up to and shot.
    marker(eas, CUBE_MESH, (pos[0], pos[1] + HQ_LANDMARK_OFFSET, 500.0), (10.0, 10.0, 10.0),
           key, "MARK_{}".format(tag))

    return finish(hq, "HQ_{}".format(tag), "MapEvents/Points")


def spawn_route(eas, tag, waypoints, exit_pos):
    exit_actor = eas.spawn_actor_from_class(bp_class(BP_EXIT), vec(exit_pos[0], exit_pos[1], ground_z(*exit_pos) + 100.0))
    exit_actor.set_editor_property("exit_tag", unreal.Name("X_" + tag))
    exit_actor.set_editor_property("board_radius", 600.0)
    exit_actor.set_editor_property("board_seconds", 8.0)
    finish(exit_actor, "EXIT_{}".format(tag), "MapEvents/Extraction")

    route = eas.spawn_actor_from_class(bp_class(BP_ROUTE), vec(0.0, 0.0, 100.0))
    route.set_editor_property("route_tag", unreal.Name("R_" + tag))
    route.set_editor_property("exit", exit_actor)
    route.set_editor_property("weight", 1.0)
    route.set_editor_property("chase_lead_seconds", 5.0)

    spline = route.get_editor_property("path")
    points = [vec(x, y, ground_z(x, y) + 100.0) for (x, y) in waypoints]         + [vec(exit_pos[0], exit_pos[1], ground_z(*exit_pos) + 100.0)]
    spline.set_spline_points(points, unreal.SplineCoordinateSpace.WORLD, True)

    radius_disc(eas, exit_pos, 700.0, "exitpad", "DISC_EXIT_{}".format(tag))
    marker(eas, SPHERE_MESH, (exit_pos[0], exit_pos[1], 500.0), (7.0, 7.0, 7.0), "exitpad",
           "MARK_EXIT_{}".format(tag))

    # Beads along the spline: the route is an editor-only line otherwise, and the whole point of it
    # is being able to see where the run out goes.
    for i in range(1, 11):
        p = route.get_point_along_route(i / 10.0)
        marker(eas, SPHERE_MESH, (p.x, p.y, 150.0), (1.6, 1.6, 1.6), "exitpad",
               "BEAD_{}_{}".format(tag, i), solid=False)

    return finish(route, "ROUTE_{}".format(tag), "MapEvents/Extraction")


# ==================== world ====================

# ==================== terrain ====================

# Filled by ground_and_nav and read by everything placed afterwards. Without it every actor in the
# level is put at world zero, and on a map with steps that means half of them are buried and the
# other half float: a point on the 800 plateau would have its banner, its garrison and its loot
# eight metres underground.
TERRAIN_GRID = {}


def ground_z(x, y):
    """Top of the ground at this spot: asked of the landscape, not of a grid of boxes.

    Was a lookup into a 16x16 table of plate heights while the floor was 256 cubes. The floor is a
    landscape now, so the only honest answer is the one the landscape gives; anything cached here
    goes stale the moment the heightmap is regenerated from map_spec.json."""
    s = unreal.LandscapeService.get_height_at_location(LANDSCAPE_LABEL, float(x), float(y))
    h = getattr(s, "height", None)
    if h is None or not getattr(s, "valid", True):
        log("WARNING: no landscape height at ({:.0f}, {:.0f}), using 0".format(x, y))
        return 0.0
    return float(h)


def all_places():
    """Every spot that has to stand on level ground, with the radius that must be flat around it."""
    return [(FINAL, 5000.0), (MISSION_WEST, 4000.0), (MISSION_EAST, 4000.0),
            (MISSION_NORTH, 4000.0), (PLAIN_SOUTH, 4000.0),
            (HQ_A_POS, 6000.0), (HQ_B_POS, 6000.0), (LAUNCH, 3000.0)]


def terrain_level(x, y):
    """Which of the flat steps this spot sits on.

    Two long lobes rather than noise: the shapes have to be big enough that a ridge between two
    points is a thing you walk around, not a bump you walk over."""
    lobe = TERRAIN["lobe"]
    h = math.sin(x / lobe) * math.cos(y / lobe)
    return int(round((h * 0.5 + 0.5) * (TERRAIN["levels"] - 1)))


def height_grid(plates, plate_w, plate_h):
    """The level of every plate, with the ground around every place flattened to that place's level.

    Flattening is the whole reason this is a grid of steps and not a heightfield: a point of
    interest on a slope turns every fight in it into a fight about the slope."""
    grid = {}
    for ix in range(plates):
        for iy in range(plates):
            cx = -GROUND["size"][0] * 0.5 + plate_w * (ix + 0.5)
            cy = -GROUND["size"][1] * 0.5 + plate_h * (iy + 0.5)
            grid[(ix, iy)] = [cx, cy, terrain_level(cx, cy)]

    for (px, py), radius in all_places():
        pinned = terrain_level(px, py)
        reach = radius + TERRAIN["flat_margin"]
        for key, (cx, cy, _) in grid.items():
            if math.hypot(cx - px, cy - py) <= reach:
                grid[key][2] = pinned
    return grid


def ground_and_nav(eas):
    """The floor: one landscape, sculpted from map_spec.json, plus the navigation volumes.

    Was 256 cube plates on three stepped levels with 44 ramp slabs between them. The author rejected
    that on sight (2026-09-01): the ramps read as loose plates stuck into the ground. It is gone, and
    with it the reason it existed. Plates were a workaround for Recast gathering geometry per tile,
    where a single 640 m floor box landed inside every one of them; a landscape is already cut into
    components, so the workaround has no job left.

    The terrain itself is NOT computed here. It is computed outside the editor by terrain.py, which
    also runs the acceptance checks (slope band, pad flatness, sightlines) and writes heightmap.png.
    The editor only imports that file. Reason: the editor's Python has no numpy, and the slope
    limiter is twenty-five passes over four hundred thousand vertices."""
    label = LANDSCAPE_LABEL
    # actor_label, not name: LandscapeInfo_Custom has no "name" field, and the mistake only shows
    # up once a landscape actually exists, because an empty list never evaluates the attribute.
    existing = [l.actor_label for l in (unreal.LandscapeService.list_landscapes() or [])]
    if label not in existing:
        half = GROUND["size"][0] * 0.5
        unreal.LandscapeService.create_landscape(
            vec(-half, -half, 0.0), unreal.Rotator(0, 0, 0), vec(100.0, 100.0, 100.0),
            sections_per_component=1, quads_per_section=63,
            component_count_x=10, component_count_y=10, landscape_label=label)
        log("CREATED: landscape {}".format(label))

    if os.path.isfile(HEIGHTMAP_PNG):
        res = unreal.LandscapeService.import_heightmap(label, HEIGHTMAP_PNG)
        log("terrain: heightmap imported, {}".format(getattr(res, "resolution", "?")))
    else:
        log("WARNING: {} missing - run terrain.py first".format(HEIGHTMAP_PNG))
    unreal.LandscapeService.set_landscape_collision(label, True)

    an = unreal.LandscapeService.analyze_terrain(label, 0.0, 0.0, GROUND["size"][0] * 0.5 - 500.0)
    log("terrain: height {:.0f}..{:.0f}, slope avg {:.1f} max {:.1f}".format(
        an.min_height, an.max_height, an.average_slope_degrees, an.max_slope_degrees))

    nav = eas.spawn_actor_from_class(unreal.NavMeshBoundsVolume, vec(0, 0, 1000.0))
    nav.set_actor_scale3d(vec(GROUND["size"][0] / 200.0, GROUND["size"][1] / 200.0, 30.0))
    finish(nav, "BENCH_NavBounds", "Nav")
    # Настраиваем СУЩЕСТВУЮЩИЙ RecastNavMesh, а не спавним свой.
    #
    # Стоит появиться NavMeshBoundsVolume, как система навигации сама заводит
    # `RecastNavMesh-Default` со STATIC и тайлом 1000 uu, то есть ровно с теми двумя настройками,
    # из-за которых карта 640 м строит 4096 тайлов и расползается минутами от центра наружу.
    # Свой актор рядом с ним это не «наша настройка», а вторые данные навигации, и какие из них
    # возьмёт движок, скрипт не решает. Поэтому правим то, что уже есть.
    recast = None
    for a in eas.get_all_level_actors():
        if isinstance(a, unreal.RecastNavMesh):
            recast = a
            break
    if recast is None:
        recast = eas.spawn_actor_from_class(unreal.RecastNavMesh, vec(0, 0, 0))
    # DYNAMIC, and kicked once by the run director when it arms the run.
    #
    # The chase through this went: bigger tiles, then a plated floor, then a static bake. None of
    # them was the disease. A run map comes up with a navmesh covering PART of itself while the
    # navigation system reports it has finished, so it never repairs itself. One Nav->Build() at the
    # top of the run and the whole map is walkable within a dozen seconds. The editor here does not
    # bake navigation at all, so a static mesh would arrive empty.
    recast.set_editor_property("runtime_generation", unreal.RuntimeGenerationType.DYNAMIC)
    recast.set_editor_property("tile_size_uu", NAV_TILE_UU)
    finish(recast, "BENCH_RecastNavMesh", "Nav")
    log("Navmesh: DYNAMIC, {:.0f} uu tiles, {:.0f}x{:.0f} grid; director builds it at run start".format(
        NAV_TILE_UU, GROUND["size"][0] / NAV_TILE_UU, GROUND["size"][1] / NAV_TILE_UU))


def build_ramp(eas, mesh, here, other, direction, plate_w, plate_h, step, label):
    """A walkable slab bridging two plates at different heights.

    Without it a step is a wall: 400 cm is over four times a character's step height, and the navmesh
    simply stops at the edge. The ramp is long enough that the slope stays under ten degrees, which
    is a walk rather than a climb."""
    cx, cy, level = here
    ocx, ocy, olevel = other
    rise = (olevel - level) * step
    run = plate_w if direction[0] else plate_h

    mid = vec((cx + ocx) * 0.5, (cy + ocy) * 0.5, (level + olevel) * 0.5 * step - 25.0)
    ramp = eas.spawn_actor_from_class(unreal.StaticMeshActor, mid)
    ramp.static_mesh_component.set_static_mesh(mesh)

    angle = math.degrees(math.atan2(rise, run))
    length = math.hypot(rise, run) * 1.15
    if direction[0]:
        ramp.set_actor_scale3d(vec(length / 100.0, plate_h * 0.55 / 100.0, 0.5))
        ramp.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=-angle, yaw=0.0), False)
    else:
        ramp.set_actor_scale3d(vec(plate_w * 0.55 / 100.0, length / 100.0, 0.5))
        ramp.set_actor_rotation(unreal.Rotator(roll=angle, pitch=0.0, yaw=0.0), False)

    ramp.static_mesh_component.set_collision_profile_name("BlockAll")
    ramp.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
    finish(ramp, label, "Ground/Ramps")
    return 1


def bake_nav():
    """Build the navmesh in the editor and bake it into the level.

    Separate from main() because building is asynchronous and a script cannot wait inside one call:
    run this, then check_nav() a few seconds later, then save."""
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    unreal.SystemLibrary.execute_console_command(world, "RebuildNavigation")
    log("navmesh rebuild started - call check_nav() in a few seconds")


def check_nav(save=False):
    """Is the whole playable area walkable? Returns True when every landmark projects onto navmesh.

    The probes are the places squads are actually sent, not a grid: a navmesh that covers the middle
    and misses the headquarters looks fine on screen and stops the war dead."""
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if unreal.NavigationSystemV1.is_navigation_being_built(world):
        log("still building")
        return False

    probes = [FINAL, MISSION_WEST, MISSION_EAST, MISSION_NORTH, PLAIN_SOUTH, HQ_A_POS, HQ_B_POS,
              LAUNCH, (0.0, -14000.0), (-14000.0, 6500.0), (14000.0, 6500.0)]
    ext = unreal.Vector(1000, 1000, 1000)
    missing = []
    for (x, y) in probes:
        if not unreal.NavigationSystemV1.project_point_to_navigation(
                world, vec(x, y, 100.0), nav_data=None, filter_class=None, query_extent=ext):
            missing.append((int(x), int(y)))

    ok = not missing
    log("navmesh covers {}/{} landmarks{}".format(
        len(probes) - len(missing), len(probes), "" if ok else " | missing at {}".format(missing)))

    if ok and save:
        log("saved: {}".format(unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)))
    return ok


# ==================== cover ====================
#
# The numbers come from build_playground.py, which was tuned on this project for exactly this: a
# compound you can fight over. A flat point is a point where the AI never loses sight of anybody,
# where CoverFinder and Push/Peek have nothing to stand behind, and where a banner is shot from a
# hundred metres away for free.
COVER = dict(
    wall_height=300.0,      # blocks sight, so contact can actually break
    wall_segments=16,
    gate_half_deg=24.0,     # two gaps, on opposite sides: a point needs two ways in
    full_height=220.0,      # stand behind it and you are gone
    half_height=100.0,      # crouch and dash cover
    full_count=6,
    half_count=8,
    min_spacing=420.0,
)

EMF_PROP_BP = "/Game/Variant_Shooter/Blueprints/Objects/BP_EMFProp_1"

# Throwables. The bench had none, and a point with no throwables is a point where the game's whole
# verb is missing: the primary weapon does no damage, so a fight is charge-something-and-throw-it.
# Cover made the places defensible and left nothing to play with.
#
# Clusters rather than an even sprinkle, and at the EDGE of the yard rather than its middle: a
# throw is worth making when it goes THROUGH a group, and a prop in the geometric centre is the one
# nobody is ever standing next to. Two clusters sit inside the gates, which is where the fight
# actually happens.
EMF = dict(
    clusters=3,
    per_cluster=(3, 5),
    cluster_spread=260.0,   # how far members scatter from their cluster centre
    # Between props. 170 was measured wrong from the outside and check_layout caught it: two of
    # them birthed inside each other at P_Mission_East. The prop is wider than it looks from above.
    min_spacing=260.0,
    # To a cover CENTRE. Cover reaches 210 from its own centre at most and a prop is about 50
    # across, so 300 clears it. 400 was too greedy: the tightest yard got 6 throwables while the
    # roomiest got 14, and a point where the verb is thin is a point that plays differently for
    # no reason anybody chose.
    cover_clearance=300.0,
    ring_lo=0.45,           # fraction of the cover ring: outer half of the yard
    ring_hi=0.85,
)


def compound(eas, tag, pos, radius, color_key, seed):
    """A wall ring with two gates and cover inside.

    The ring sits well inside the influence radius, so the fight over the point happens at the wall
    and in the yard rather than out in the open where the radius merely ends."""
    rng = random.Random(seed)
    mesh = unreal.EditorAssetLibrary.load_asset(CUBE_MESH)
    ring = radius * 0.62
    placed = []

    def far_enough(x, y):
        return all(math.hypot(x - px, y - py) >= COVER["min_spacing"] for px, py in placed)

    def block(cx, cy, sx, sy, sz, yaw, label):
        a = eas.spawn_actor_from_class(unreal.StaticMeshActor, vec(cx, cy, ground_z(cx, cy) + sz * 0.5))
        a.static_mesh_component.set_static_mesh(mesh)
        a.set_actor_scale3d(vec(sx / 100.0, sy / 100.0, sz / 100.0))
        if yaw:
            a.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw), False)
        a.static_mesh_component.set_material(0, color_material(color_key))
        a.static_mesh_component.set_collision_profile_name("BlockAll")
        a.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
        finish(a, label, "Cover/{}".format(tag))
        placed.append((cx, cy))

    # The wall, minus two gates facing each other. Gates rather than a doorway asset: what matters
    # is that there are exactly two ways in, so choosing one is a decision.
    seg_deg = 360.0 / COVER["wall_segments"]
    seg_len = 2.0 * math.pi * ring / COVER["wall_segments"] * 1.1
    for i in range(COVER["wall_segments"]):
        deg = i * seg_deg
        gate = min(abs((deg - 0.0 + 180) % 360 - 180), abs((deg - 180.0 + 180) % 360 - 180))
        if gate < COVER["gate_half_deg"]:
            continue
        rad = math.radians(deg)
        block(pos[0] + math.cos(rad) * ring, pos[1] + math.sin(rad) * ring,
              seg_len, 120.0, COVER["wall_height"], deg + 90.0,
              "WALL_{}_{}".format(tag, i))

    # Cover inside the yard, jittered on a ring so nothing ends up in the dead centre where the
    # banner stands.
    for kind, count, height in (("FULL", COVER["full_count"], COVER["full_height"]),
                                ("HALF", COVER["half_count"], COVER["half_height"])):
        for i in range(count):
            for _ in range(20):
                deg = rng.uniform(0.0, 360.0)
                dist = rng.uniform(ring * 0.30, ring * 0.80)
                cx = pos[0] + math.cos(math.radians(deg)) * dist
                cy = pos[1] + math.sin(math.radians(deg)) * dist
                if far_enough(cx, cy):
                    block(cx, cy, rng.uniform(260.0, 420.0), rng.uniform(140.0, 220.0),
                          height, rng.uniform(0.0, 180.0), "{}_{}_{}".format(kind, tag, i))
                    break

    props = emf_props(eas, tag, pos, ring, rng, placed)
    log("compound {}: {} solid pieces, {} throwables".format(tag, len(placed) - props, props))
    return len(placed)


def emf_props(eas, tag, pos, ring, rng, placed):
    """Charge-and-throw props, in clusters, around the edge of the yard.

    Spawned as the blueprint rather than as a cube: the prop carries its own charge state, its own
    break, and the curves that turn charge into damage and stun. A grey cube would look the same
    from above and do nothing.

    Placed LAST, so they take the gaps cover left rather than pushing cover around: a throwable that
    displaced a wall would change what the point is."""
    cls = bp_class(EMF_PROP_BP)
    lo, hi = EMF["per_cluster"]
    count = 0
    mine = []

    def clear(px, py):
        # Two different distances on purpose. Cover is up to 420 long and `placed` holds its
        # CENTRE, so a prop 170 from that centre is inside the crate; props are small and only
        # need to not birth into each other.
        if any(math.hypot(px - qx, py - qy) < EMF["cover_clearance"] for qx, qy in placed):
            return False
        return all(math.hypot(px - qx, py - qy) >= EMF["min_spacing"] for qx, qy in mine)

    for c in range(EMF["clusters"]):
        # Cluster centres spread evenly by angle, jittered: three clusters in a row on one side of
        # the yard is the same as one cluster.
        base_deg = (360.0 / EMF["clusters"]) * c + rng.uniform(-40.0, 40.0)
        dist = ring * rng.uniform(EMF["ring_lo"], EMF["ring_hi"])
        ccx = pos[0] + math.cos(math.radians(base_deg)) * dist
        ccy = pos[1] + math.sin(math.radians(base_deg)) * dist

        for i in range(rng.randint(lo, hi)):
            for _ in range(60):
                px = ccx + rng.uniform(-EMF["cluster_spread"], EMF["cluster_spread"])
                py = ccy + rng.uniform(-EMF["cluster_spread"], EMF["cluster_spread"])
                if clear(px, py):
                    a = eas.spawn_actor_from_class(cls, vec(px, py, ground_z(px, py) + 60.0))
                    a.set_actor_rotation(unreal.Rotator(roll=0.0, pitch=0.0,
                                                        yaw=rng.uniform(0.0, 360.0)), False)
                    finish(a, "EMF_{}_{}_{}".format(tag, c, i), "Props/{}".format(tag))
                    mine.append((px, py))
                    count += 1
                    break

    placed.extend(mine)
    return count


def check_layout(eas):
    """Nothing solid standing inside anything else, and a floor that actually stops things.

    Bounding boxes, not exact geometry: the failure this catches is gross - a banner and a
    decorative cone at identical coordinates - and a box test finds that without pretending to be
    a physics query. Ground plates are skipped because they tile edge to edge on purpose.
    """
    props = []
    for a in eas.get_all_level_actors():
        if TAG not in [str(t) for t in a.tags]:
            continue
        label = a.get_actor_label()
        # Ground, floor paint and the compounds are all skipped. Walls and cover are placed on a
        # spacing rule of their own and touch nothing; checking every block against every other
        # block on a map this size is thousands of pairs for a question already answered.
        if (label.startswith("BENCH_Ground") or label.startswith("DISC_")
                or label.startswith("BEAD_") or label.startswith("RAMP_")
                or label.startswith("WALL_") or label.startswith("FULL_")
                or label.startswith("HALF_")):
            continue
        comp = a.get_component_by_class(unreal.StaticMeshComponent)
        if not comp or not comp.get_editor_property("static_mesh"):
            continue
        if comp.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION:
            continue
        origin, extent = a.get_actor_bounds(False)
        props.append((label, origin, extent))

    clashes = []
    for i in range(len(props)):
        for j in range(i + 1, len(props)):
            (la, oa, ea), (lb, ob, eb) = props[i], props[j]
            # 1 cm of slack so two things merely touching are not reported as one inside the other.
            if (abs(oa.x - ob.x) < ea.x + eb.x - 1.0
                    and abs(oa.y - ob.y) < ea.y + eb.y - 1.0
                    and abs(oa.z - ob.z) < ea.z + eb.z - 1.0):
                clashes.append("{} <-> {}".format(la, lb))

    floors = [a for a in eas.get_all_level_actors()
              if a.get_actor_label().startswith("BENCH_Ground")]
    open_floors = [a.get_actor_label() for a in floors
                   if a.get_component_by_class(unreal.StaticMeshComponent).get_collision_enabled()
                   == unreal.CollisionEnabled.NO_COLLISION]

    log("layout: {} solid props, {} floor plates, {} without collision".format(
        len(props), len(floors), len(open_floors)))
    if clashes:
        log("OVERLAPPING: " + "; ".join(clashes))
    else:
        log("nothing is standing inside anything else")
    return not clashes and not open_floors


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
        unreal.RunLaunchPoint, vec(LAUNCH[0], LAUNCH[1], ground_z(*LAUNCH) + 200.0),
        unreal.Rotator(roll=0.0, pitch=25.0, yaw=-90.0))
    launch.set_editor_property("launch_from_sea", True)
    launch.set_editor_property("launch_speed", 2500.0)
    launch.set_editor_property("boss_intro", False)
    launch.set_editor_property("arena_index", 0)
    finish(launch, "BENCH_RunLaunchPoint", "MapEvents")

    finish(eas.spawn_actor_from_class(unreal.PlayerStart, vec(LAUNCH[0], LAUNCH[1], ground_z(*LAUNCH) + 200.0)),
           "BENCH_PlayerStart", "MapEvents")

    radius_disc(eas, LAUNCH, 900.0, "launch", "DISC_LAUNCH")
    # Beside the start, never on it. Belt and braces after the collision bug above: nothing tall
    # stands where a pawn has to appear.
    marker(eas, CONE_MESH, (LAUNCH[0] + 1200.0, LAUNCH[1], 0.0), (4.0, 4.0, 11.0), "launch", "MARK_LAUNCH")


def main(save=False):
    """save defaults to False: an unsaved rebuild is a free rollback, and the author reviews first."""
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    open_or_create_level(les)
    clear_tagged(eas)

    world_settings(les)
    make_banner_assets()
    ground_and_nav(eas)
    run_entry(eas)

    # The two factions, taken from the author's own assets. A is people, B is machines, and the
    # bench is worthless the moment those two look the same on the ground.
    people = copy_members(SQUAD_A_SOURCE)     # riflemen + juggernaut + grenadier
    machines = copy_members(SQUAD_B_SOURCE)   # tracked tank + flying drones

    gar_a = make_loadout("DA_Bench_Garrison_A", TEAM_A, unreal.SquadInitialTask.DEFEND, people)
    sor_a = make_loadout("DA_Bench_Sortie_A", TEAM_A, unreal.SquadInitialTask.ATTACK, people)

    # A tank makes a poor garrison and a fine spearhead, so B holds with drones and attacks with
    # the author's assault squad exactly as written.
    gar_b = make_loadout("DA_Bench_Garrison_B", TEAM_B, unreal.SquadInitialTask.DEFEND,
                         [entry(DRONE_BP, 3, commander=True)])
    sor_b = make_loadout("DA_Bench_Sortie_B", TEAM_B, unreal.SquadInitialTask.ATTACK, machines)

    # What each side sends once its banner is down. Not "the same but fewer": the thing that made
    # the faction frightening is what goes. A loses the juggernaut and the grenadier, B loses the
    # tank and turns up as a pair of drones.
    weak_a = make_loadout("DA_Bench_Weak_A", TEAM_A, unreal.SquadInitialTask.ATTACK,
                          [entry(SHOOTER_BP, 2, commander=True)])
    weak_b = make_loadout("DA_Bench_Weak_B", TEAM_B, unreal.SquadInitialTask.ATTACK,
                          [entry(DRONE_BP, 2, commander=True)])

    # Two kinds of sheet, so an anchor is a real roll and not a coin flip on nothing: the rich one
    # carries the money, the common one is ammo. Created once; edit them in the editor afterwards.
    ensure_sheet_bp(SHEET_MONEY, [loot_entry(CURRENCY_BP, 1, True), loot_entry(AMMO_BP, 2, False)])
    ensure_sheet_bp(SHEET_AMMO, [loot_entry(AMMO_BP, 3, False)])

    # Money budget: the map used to place a fixed five stacks. Sheets are rolled, so the total is
    # now a distribution rather than a number - four anchors at 0.55 over a mix that is one third
    # money averages out near the same place. The director prints what actually landed.
    rich_mix = [sheet_option(SHEET_MONEY, 1.0), sheet_option(SHEET_AMMO, 2.0)]
    poor_mix = [sheet_option(SHEET_AMMO, 1.0)]

    reward = unreal.FinalConditions()
    reward.set_editor_property("wave_delta", -1)
    reward.set_editor_property("arrival_delay_seconds", 20.0)
    reward.set_editor_property("entry_quality", 1)

    spawn_poi(eas, "P_Mission_West", MISSION_WEST, unreal.PoiRole.MISSION, TEAM_A,
              garrison=gar_a, banner=True, anchors=4, anchor_chance=0.55, sheet_mix=rich_mix,
              mission_kind=unreal.MissionKind.ELIMINATION, reward=reward,
              prize=unreal.PoiPrize.TROPHY_WEAPON, prize_life=20.0)

    spawn_poi(eas, "P_Mission_East", MISSION_EAST, unreal.PoiRole.MISSION, TEAM_B,
              garrison=gar_b, banner=True, anchors=4, anchor_chance=0.55, sheet_mix=rich_mix,
              mission_kind=unreal.MissionKind.SABOTAGE, reward=reward,
              prize=unreal.PoiPrize.WRECKED_VEHICLE, prize_life=60.0)

    spawn_poi(eas, "P_Mission_North", MISSION_NORTH, unreal.PoiRole.MISSION, TEAM_A,
              garrison=gar_a, banner=True, anchors=4, anchor_chance=0.55, sheet_mix=rich_mix,
              mission_kind=unreal.MissionKind.DELIVERY, reward=reward,
              prize=unreal.PoiPrize.POINT_POWER, prize_life=0.0)

    # The safe point: more anchors, worse odds on each, and no banner. Cheap loot that costs time.
    spawn_poi(eas, "P_Plain_South", PLAIN_SOUTH, unreal.PoiRole.PLAIN, TEAM_NEUTRAL,
              anchors=5, anchor_chance=0.4, sheet_mix=rich_mix)

    spawn_poi(eas, "P_Final", FINAL, unreal.PoiRole.FINAL, TEAM_NEUTRAL, radius=5000.0)

    # Ammo only: a headquarters that also paid the best money would make the other five points
    # decorative. The banner here is the one that matters - it is what weakens the sorties.
    # Relief forces: the garrison composition with the MARCHING task. A garrison asset sent as
    # reinforcement never leaves the gate, because only Attack squads advance.
    relief_a = make_loadout("DA_Bench_Relief_A", TEAM_A, unreal.SquadInitialTask.ATTACK, people)
    relief_b = make_loadout("DA_Bench_Relief_B", TEAM_B, unreal.SquadInitialTask.ATTACK,
                            [entry(DRONE_BP, 3, commander=True)])

    spawn_hq(eas, "HQ_A", HQ_A_POS, TEAM_A, sor_a, weak_a, gar_a,
             anchors=3, anchor_chance=0.7, sheet_mix=poor_mix, relief_loadout=relief_a)
    spawn_hq(eas, "HQ_B", HQ_B_POS, TEAM_B, sor_b, weak_b, gar_b,
             anchors=3, anchor_chance=0.7, sheet_mix=poor_mix, relief_loadout=relief_b)

    # Three ways out, drawn at random after the hold. Different lengths and directions on purpose:
    # a team that always leaves the same way has not been asked anything.
    spawn_route(eas, "North", [FINAL, (0.0, -8000.0), (0.0, -20000.0)], (0.0, -28000.0))
    spawn_route(eas, "West", [FINAL, (-8000.0, 6000.0), (-20000.0, 11000.0)], (-28000.0, 13000.0))
    spawn_route(eas, "East", [FINAL, (8000.0, 6000.0), (20000.0, 11000.0)], (28000.0, 13000.0))

    check_layout(eas)

    if save:
        saved = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
        log("Saved: {}".format(saved))
    else:
        log("NOT saved (call main(save=True) once the author has looked)")
    log("Done. Level: {}".format(LEVEL_PATH))


if __name__ == "__main__":
    main()
