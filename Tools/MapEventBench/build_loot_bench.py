import unreal  # first line on purpose: execute_python_code only accepts code that starts with it

# Loot bench: six zones in a row, each worth a different amount, so the loot generator can be
# looked at instead of reasoned about.
#
# The point of it is comparison. On a real map the zones are kilometres apart and nobody can hold
# two of them in their head at once; here they touch, so walking from one end to the other IS the
# curve. What lands in each is rolled exactly the way it would be on a real point - same generator,
# same registry, same anchors - and the only thing the bench changes is the distance between them.
#
#   Tools/mcp.sh py Source/Tools/MapEventBench/build_loot_bench.py
#
# Rebuild it as often as you like: it is idempotent by tag and never saves unless asked.
# Log filter tag: [LOOTBENCH]

import math

LEVEL_PATH = "/Game/MapEventBench/L_LootBench"
TAG = "LootBench"

BP_DIR = "/Game/Variant_Shooter/Blueprints/LevelBPs"
BP_POI = BP_DIR + "/BP_PoiActor"
BP_ANCHOR = BP_DIR + "/BP_LootAnchor"
BP_SHEET = BP_DIR + "/BP_Sheet_Rolled"

GAMEMODE = "/Game/Variant_Shooter/Blueprints/BP_ShooterGameMode"
CUBE = "/Engine/BasicShapes/Cube.Cube"

# The six places, and what each is worth. Spread stays the same across all of them so the only
# thing being compared is quality; change one zone's spread by hand to see what that does instead.
ZONES = [
    ("Z_010", 0.10),
    ("Z_025", 0.25),
    ("Z_040", 0.40),
    ("Z_060", 0.60),
    ("Z_080", 0.80),
    ("Z_095", 0.95),
]
SPREAD = 0.15

ZONE_RADIUS = 1800.0
ZONE_SPACING = 3800.0     # touching, not overlapping: two radii plus a walkable seam
ANCHORS_PER_ZONE = 4
GROUND_Z = 0.0


def log(msg):
    unreal.log("[LOOTBENCH] {}".format(msg))


def vec(x, y, z):
    return unreal.Vector(float(x), float(y), float(z))


def bp_class(path):
    c = unreal.EditorAssetLibrary.load_blueprint_class(path)
    if not c:
        raise RuntimeError("Blueprint missing: {} (build the C++ first, then make the blueprint)".format(path))
    return c


def zone_x(index):
    span = ZONE_SPACING * (len(ZONES) - 1)
    return -span * 0.5 + ZONE_SPACING * index


def finish(actor, actor_label, folder):
    actor.set_actor_label(actor_label)
    actor.set_folder_path(folder)
    tags = list(actor.tags)
    tags.append(TAG)
    actor.tags = tags
    return actor


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


def open_or_create_level(les):
    if unreal.EditorAssetLibrary.does_asset_exist(LEVEL_PATH):
        les.load_level(LEVEL_PATH)
        log("Loaded existing level")
    else:
        les.new_level(LEVEL_PATH)
        log("CREATED: {}".format(LEVEL_PATH))


def world_settings():
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    ws = world.get_world_settings()
    gm = unreal.EditorAssetLibrary.load_blueprint_class(GAMEMODE)
    if gm:
        ws.set_editor_property("default_game_mode", gm)
        log("GameMode override -> {}".format(GAMEMODE))


def lighting(eas):
    # A new level is a black box. This is the minimum needed to see the floor, and deliberately
    # nothing more: the bench is for reading loot off the ground, and anything prettier is time
    # spent on the wrong problem.
    sun = eas.spawn_actor_from_class(unreal.DirectionalLight, vec(0, 0, 3000),
                                     unreal.Rotator(-42.0, 30.0, 0.0))
    finish(sun, "LIGHT_Sun", "LootBench/Lighting")

    sky = eas.spawn_actor_from_class(unreal.SkyLight, vec(0, 0, 3200))
    finish(sky, "LIGHT_Sky", "LootBench/Lighting")

    atmo = eas.spawn_actor_from_class(unreal.SkyAtmosphere, vec(0, 0, 0))
    finish(atmo, "LIGHT_Atmosphere", "LootBench/Lighting")


def ground(eas):
    # One slab. No navmesh on purpose: nothing in this bench walks except the player.
    span = ZONE_SPACING * (len(ZONES) - 1) + ZONE_RADIUS * 4.0
    slab = eas.spawn_actor_from_class(unreal.StaticMeshActor, vec(0, 0, GROUND_Z - 50.0))
    comp = slab.static_mesh_component
    comp.set_static_mesh(unreal.EditorAssetLibrary.load_asset(CUBE))
    comp.set_relative_scale3d(vec(span / 100.0, 60.0, 1.0))
    # Profile, not the enabled-flag: set_collision_enabled does not survive saving the level.
    comp.set_collision_profile_name("BlockAll")
    finish(slab, "GROUND", "LootBench/World")


def make_label(eas, text, pos, size=200.0):
    t = eas.spawn_actor_from_class(unreal.TextRenderActor, vec(*pos), unreal.Rotator(0.0, 180.0, 0.0))
    trc = t.text_render
    trc.set_text(unreal.Text(text))
    trc.set_world_size(size)
    trc.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
    return t


def spawn_zone(eas, index, tag, quality):
    x = zone_x(index)

    poi = eas.spawn_actor_from_class(bp_class(BP_POI), vec(x, 0, GROUND_Z + 100.0))
    poi.set_editor_property("poi_tag", unreal.Name(tag))
    poi.set_editor_property("poi_role", unreal.PoiRole.PLAIN)
    poi.set_editor_property("influence_radius", ZONE_RADIUS)
    poi.set_editor_property("loot_quality", quality)
    poi.set_editor_property("loot_spread", SPREAD)
    finish(poi, "POI_{}".format(tag), "LootBench/Zones")

    t = make_label(eas, "{}\nquality {:.2f}".format(tag, quality),
                   (x, 0.0, GROUND_Z + 700.0), size=220.0)
    finish(t, "LABEL_{}".format(tag), "LootBench/Zones")

    # A post at the centre, so a zone is findable from the far end of the bench.
    post = eas.spawn_actor_from_class(unreal.StaticMeshActor, vec(x, 0, GROUND_Z + 250.0))
    pc = post.static_mesh_component
    pc.set_static_mesh(unreal.EditorAssetLibrary.load_asset(CUBE))
    pc.set_relative_scale3d(vec(0.4, 0.4, 5.0))
    pc.set_collision_profile_name("NoCollision")
    finish(post, "POST_{}".format(tag), "LootBench/Zones")

    for i in range(ANCHORS_PER_ZONE):
        # Golden angle inside the inner half of the zone: sheets spread out instead of stacking,
        # and none of them lands on the seam between two zones.
        angle = i * 2.39996323
        r = ZONE_RADIUS * 0.5 * math.sqrt((i + 0.5) / ANCHORS_PER_ZONE)
        ax = x + r * math.cos(angle)
        ay = r * math.sin(angle)

        a = eas.spawn_actor_from_class(bp_class(BP_ANCHOR), vec(ax, ay, GROUND_Z + 20.0))
        a.set_editor_property("anchor_tag", unreal.Name("{}_A{}".format(tag, i)))
        # Always pays. The chance roll is a real mechanic, but on a bench it only adds noise to the
        # thing being measured, which is quality.
        a.set_editor_property("chance", 1.0)
        opt = unreal.LootSheetOption()
        opt.set_editor_property("sheet_class", bp_class(BP_SHEET))
        opt.set_editor_property("weight", 1.0)
        a.set_editor_property("sheets", [opt])
        a.set_editor_property("owning_poi", poi)
        finish(a, "ANCHOR_{}_{}".format(tag, i), "LootBench/Anchors")

    log("zone {} at x={:.0f}, quality {:.2f}, {} anchors".format(tag, x, quality, ANCHORS_PER_ZONE))


def main(save=False):
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    open_or_create_level(les)
    clear_tagged(eas)
    world_settings()
    lighting(eas)
    ground(eas)

    for i, (tag, quality) in enumerate(ZONES):
        spawn_zone(eas, i, tag, quality)

    start_x = zone_x(0) - ZONE_RADIUS - 1200.0
    start = eas.spawn_actor_from_class(unreal.PlayerStart, vec(start_x, 0, GROUND_Z + 120.0))
    finish(start, "PLAYERSTART", "LootBench/World")

    t = make_label(eas, "poorest -> richest", (start_x, 0.0, GROUND_Z + 700.0), size=180.0)
    finish(t, "LABEL_START", "LootBench/World")

    if save:
        les.save_current_level()
        log("SAVED")
    else:
        log("NOT saved (call main(save=True) once you have looked)")

    log("Done. Level: {}  Zones: {}".format(LEVEL_PATH, len(ZONES)))


if __name__ == "__main__":
    main()
