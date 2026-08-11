# Football combat arena builder for Polarity.
#
# Builds a one-goal arena where the player must kick one physical ball through
# a SportsGoal while sustain waves pressure the flanks.
#
# Usage (in-editor):
#   py "<...>/Source/Tools/ArenaBlockout/build_football_arena.py"
#
# Output level:
#   /Game/Variant_Shooter/Arenas/Biome1/A10_Football/Lvl_A10_Football
#
# Idempotent: only actors tagged BLOCKOUT_A10_Football are rebuilt.
# Log tag: [FOOTBALL_ARENA]

import math
import os
import sys

import unreal

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.append(TOOLS_DIR)
import build_arena as ba  # noqa: E402

ARENA = "A10_Football"
TAG = "BLOCKOUT_" + ARENA
LEVEL_PATH = "/Game/Variant_Shooter/Arenas/Biome1/A10_Football/Lvl_A10_Football"

CLASSES = {
    "arena_manager": "/Game/Variant_Shooter/Arenas/BPs/BP_ArenaManager",
    "coordinator": "/Game/Variant_Shooter/Blueprints/AI/BP_AICombatCoordinator",
    "spawn_point": "/Script/Polarity.ArenaSpawnPoint",
    "football": "/Game/Variant_Shooter/Blueprints/LevelBPs/FootBall/BP_FootBall",
    "sports_goal": "/Game/Variant_Shooter/Blueprints/LevelBPs/BP_SportsGoal",
    "prop": "/Game/Variant_Shooter/Blueprints/Objects/BP_EMFProp_1",
    "plate": "/Game/Variant_Shooter/Blueprints/Objects/BP_EMF_AcceleratorPlate",
    "melee": "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_MeleeNPC",
    "shooter": "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_ShooterNPC",
    "drone": "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_FlyingDrone",
    "kamikaze": "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_KamikazeDrone",
}


def log(msg):
    unreal.log("[FOOTBALL_ARENA] {}".format(msg))


def warn(msg):
    unreal.log_warning("[FOOTBALL_ARENA] {}".format(msg))


def spawn_actor(eas, cls, pos, yaw=0.0, label=None, folder="A10_Football/Logic"):
    if cls is None:
        return None
    actor = eas.spawn_actor_from_class(
        cls, ba.vec(pos), unreal.Rotator(roll=0.0, pitch=0.0, yaw=float(yaw)))
    if actor and label:
        ba.finish_actor(actor, TAG, label, folder)
    return actor


def set_prop(actor, name, value):
    if not actor:
        return
    try:
        actor.set_editor_property(name, value)
    except Exception as e:
        warn("{}: could not set {} ({})".format(actor.get_actor_label(), name, e))


def pieces():
    """Blockout field: open central shot lane, combat flanks, raised pressure decks."""
    p = [
        # Main pitch and approach.
        {"id": "pitch", "shape": "box", "mat": "floor",
         "pos": [0, 0, -50], "size": [7800, 10800, 100], "group": "Floor"},
        {"id": "south_runup", "shape": "box", "mat": "floor",
         "pos": [0, -6100, -50], "size": [2200, 1800, 100], "group": "Floor"},

        # Side boards keep the ball playable without making the center a corridor.
        {"id": "board_w", "shape": "box", "mat": "wallrun",
         "pos": [-3350, 0, 190], "size": [120, 9600, 380], "group": "Boards"},
        {"id": "board_e", "shape": "box", "mat": "wallrun",
         "pos": [3350, 0, 190], "size": [120, 9600, 380], "group": "Boards"},
        {"id": "backstop_s_w", "shape": "box", "mat": "wall",
         "pos": [-2300, -5250, 250], "size": [2100, 160, 500], "group": "Boards"},
        {"id": "backstop_s_e", "shape": "box", "mat": "wall",
         "pos": [2300, -5250, 250], "size": [2100, 160, 500], "group": "Boards"},

        # Funnel near the goal: enough bounce to aim, not a full pinball chute.
        {"id": "goal_funnel_w", "shape": "box", "mat": "wall",
         "pos": [-1450, 3920, 210], "size": [1550, 120, 420],
         "yaw": -18, "group": "GoalMouth"},
        {"id": "goal_funnel_e", "shape": "box", "mat": "wall",
         "pos": [1450, 3920, 210], "size": [1550, 120, 420],
         "yaw": 18, "group": "GoalMouth"},

        # Visual/physical goal frame around the Blueprint scoring volume.
        {"id": "goal_post_w", "shape": "box", "mat": "deco",
         "pos": [-760, 4460, 320], "size": [120, 120, 640], "group": "GoalFrame"},
        {"id": "goal_post_e", "shape": "box", "mat": "deco",
         "pos": [760, 4460, 320], "size": [120, 120, 640], "group": "GoalFrame"},
        {"id": "goal_crossbar", "shape": "box", "mat": "deco",
         "pos": [0, 4460, 660], "size": [1640, 120, 120], "group": "GoalFrame"},

        # Raised flanks: enemies pressure the player, but the ball lane stays clean.
        {"id": "deck_w", "shape": "box", "mat": "deco",
         "pos": [-2600, 650, 260], "size": [1200, 4200, 420], "group": "FlankDecks"},
        {"id": "deck_e", "shape": "box", "mat": "deco",
         "pos": [2600, 650, 260], "size": [1200, 4200, 420], "group": "FlankDecks"},
        {"id": "ramp_w_s", "shape": "ramp", "mat": "floor",
         "from": [-1750, -2300, 0], "to": [-2600, -1200, 470],
         "width": 520, "group": "Ramps"},
        {"id": "ramp_w_n", "shape": "ramp", "mat": "floor",
         "from": [-1750, 2900, 0], "to": [-2600, 2000, 470],
         "width": 520, "group": "Ramps"},
        {"id": "ramp_e_s", "shape": "ramp", "mat": "floor",
         "from": [1750, -2300, 0], "to": [2600, -1200, 470],
         "width": 520, "group": "Ramps"},
        {"id": "ramp_e_n", "shape": "ramp", "mat": "floor",
         "from": [1750, 2900, 0], "to": [2600, 2000, 470],
         "width": 520, "group": "Ramps"},

        # Low midfield cover lives outside the pure shot lane.
        {"id": "cover_w_mid", "shape": "box", "mat": "wall",
         "pos": [-1550, 350, 90], "size": [700, 150, 180], "group": "Cover"},
        {"id": "cover_e_mid", "shape": "box", "mat": "wall",
         "pos": [1550, -550, 90], "size": [700, 150, 180], "group": "Cover"},
        {"id": "cover_w_n", "shape": "box", "mat": "wall",
         "pos": [-1900, 2450, 110], "size": [150, 850, 220], "group": "Cover"},
        {"id": "cover_e_n", "shape": "box", "mat": "wall",
         "pos": [1900, 2350, 110], "size": [150, 850, 220], "group": "Cover"},

        # Containment field. It keeps the ball, drones, and player in the test box.
        {"id": "fld_n", "shape": "box", "mat": "field",
         "pos": [0, 5460, 1600], "size": [8200, 20, 3200], "group": "Field"},
        {"id": "fld_s", "shape": "box", "mat": "field",
         "pos": [0, -5460, 1600], "size": [8200, 20, 3200], "group": "Field"},
        {"id": "fld_w", "shape": "box", "mat": "field",
         "pos": [-3960, 0, 1600], "size": [20, 11000, 3200], "group": "Field"},
        {"id": "fld_e", "shape": "box", "mat": "field",
         "pos": [3960, 0, 1600], "size": [20, 11000, 3200], "group": "Field"},
    ]
    return p


def spawn_goal(eas, classes):
    cls = classes.get("sports_goal") or ba.resolve_class("/Script/Polarity.SportsGoal")
    goal = spawn_actor(
        eas, cls, [0, 4380, 160], 90.0,
        "BLK_A10_Football_SportsGoal", "A10_Football/Football")
    if not goal:
        warn("SportsGoal failed to spawn")
        return None

    set_prop(goal, "bOneShot", True)
    set_prop(goal, "full_power_speed", 2800.0)
    set_prop(goal, "slow_goal_power", 0.18)
    set_prop(goal, "slow_impulse_radius", 500.0)
    set_prop(goal, "full_impulse_radius", 2100.0)
    set_prop(goal, "slow_radial_impulse", 450.0)
    set_prop(goal, "full_radial_impulse", 4200.0)

    try:
        box = goal.get_component_by_class(unreal.BoxComponent)
        if box:
            # Local X is goal depth; with yaw=90 local Y becomes world width.
            box.set_box_extent(unreal.Vector(160.0, 720.0, 300.0))
            log("GoalVolume extent -> 160 x 720 x 300")
    except Exception as e:
        warn("Goal volume resize failed: {}".format(e))
    return goal


def spawn_ball(eas, classes):
    cls = classes.get("football") or ba.resolve_class("/Script/Polarity.SportsBall")
    ball = spawn_actor(
        eas, cls, [0, -2550, 90], 90.0,
        "BLK_A10_Football_Ball", "A10_Football/Football")
    if not ball:
        warn("Football failed to spawn")
        return None

    set_prop(ball, "ball_diameter", 90.0)
    set_prop(ball, "ball_mass_kg", 8.5)
    set_prop(ball, "linear_damping", 0.08)
    set_prop(ball, "angular_damping", 0.18)
    set_prop(ball, "kick_velocity_change", 1550.0)
    set_prop(ball, "kick_upward_bias", 0.06)
    set_prop(ball, "player_velocity_to_kick_scale", 0.45)
    set_prop(ball, "max_player_velocity_bonus", 900.0)
    set_prop(ball, "sliding_kick_forward_velocity_change", 550.0)
    set_prop(ball, "sliding_kick_up_velocity_change", 520.0)
    set_prop(ball, "sliding_body_push_max_ball_speed", 2100.0)
    return ball


def spawn_spawns(eas, classes):
    cls = classes.get("spawn_point")
    specs = [
        ("G_South_W", [-1200, -3300, 10], 55, False),
        ("G_South_E", [1200, -3300, 10], 125, False),
        ("G_Mid_W", [-2700, -200, 480], 0, False),
        ("G_Mid_E", [2700, -200, 480], 180, False),
        ("G_North_W", [-2700, 2550, 480], -35, False),
        ("G_North_E", [2700, 2550, 480], -145, False),
        ("G_Back_W", [-1650, 3950, 10], -45, False),
        ("G_Back_E", [1650, 3950, 10], -135, False),
        ("Air_W", [-2100, 1050, 80], 0, True),
        ("Air_E", [2100, 1050, 80], 180, True),
    ]
    out = []
    for sid, pos, yaw, air in specs:
        actor = spawn_actor(
            eas, cls, pos, yaw, "BLK_A10_Football_{}".format(sid),
            "A10_Football/Spawns")
        if actor and air:
            set_prop(actor, "air_spawn", True)
            set_prop(actor, "air_spawn_height", 360.0)
        if actor:
            out.append(actor)
    log("Spawned {} ArenaSpawnPoint actors".format(len(out)))
    return out


def spawn_combat_props(eas, classes):
    prop_cls = classes.get("prop")
    plate_cls = classes.get("plate")
    for cid, cx, cy in [
        ("SW", -2150, -1650), ("SE", 2150, -1650),
        ("NW", -2250, 1550), ("NE", 2250, 1550),
    ]:
        for i in range(3):
            ang = i * math.tau / 3.0
            pos = [cx + math.cos(ang) * 170.0, cy + math.sin(ang) * 170.0, 60]
            spawn_actor(
                eas, prop_cls, pos, math.degrees(ang),
                "BLK_A10_Football_Prop_{}_{}".format(cid, i),
                "A10_Football/Props")

    for label, pos, yaw in [
        ("Plate_W", [-1180, -1100, 20], 18),
        ("Plate_E", [1180, -1100, 20], -18),
    ]:
        spawn_actor(
            eas, plate_cls, pos, yaw,
            "BLK_A10_Football_{}".format(label), "A10_Football/Props")


def spawn_nav_links(eas):
    specs = [
        ("NL_W_S", [-2360, -1200, 500], [0, -300, 0], [550, 350, -470]),
        ("NL_W_N", [-2360, 2000, 500], [0, 300, 0], [550, -350, -470]),
        ("NL_E_S", [2360, -1200, 500], [0, -300, 0], [-550, 350, -470]),
        ("NL_E_N", [2360, 2000, 500], [0, 300, 0], [-550, -350, -470]),
    ]
    for sid, pos, left, right in specs:
        actor = eas.spawn_actor_from_class(unreal.NavLinkProxy, ba.vec(pos))
        if not actor:
            warn("{} NavLinkProxy failed".format(sid))
            continue
        link = unreal.NavigationLink()
        link.set_editor_property("left", ba.vec(left))
        link.set_editor_property("right", ba.vec(right))
        try:
            link.set_editor_property("direction", unreal.NavLinkDirection.BOTH_WAYS)
            link.set_editor_property("snap_radius", 180.0)
        except Exception as e:
            warn("{} point link optional setup failed: {}".format(sid, e))
        actor.set_editor_property("point_links", [link])
        try:
            actor.set_editor_property("smart_link_is_relevant", True)
            smart = actor.get_editor_property("smart_link_comp")
            smart.set_editor_property("link_relative_start", ba.vec(left))
            smart.set_editor_property("link_relative_end", ba.vec(right))
            smart.set_editor_property("link_direction", unreal.NavLinkDirection.BOTH_WAYS)
            smart.set_editor_property("link_enabled", True)
        except Exception as e:
            warn("{} smart link setup failed: {}".format(sid, e))
        ba.finish_actor(actor, TAG, "BLK_A10_Football_{}".format(sid), "A10_Football/Nav")


def spawn_entry_trigger(eas):
    actor = eas.spawn_actor_from_class(
        unreal.TriggerBox, ba.vec([0, -260, 1550]), unreal.Rotator(0, 0, 0))
    if actor:
        comps = actor.get_components_by_class(unreal.BoxComponent)
        if comps:
            comps[0].set_box_extent(ba.vec([3700, 5200, 1600]))
        ba.finish_actor(actor, TAG, "BLK_A10_Football_EntryTrigger", "A10_Football/Logic")
    return actor


def build_sustain_pool(classes):
    pool = []
    for key, weight in [("melee", 4.0), ("shooter", 2.5), ("drone", 1.5), ("kamikaze", 0.75)]:
        cls = classes.get(key)
        if not cls:
            warn("Sustain pool skips missing class {}".format(key))
            continue
        entry = unreal.SustainSpawnEntry()
        entry.set_editor_property("npc_class", cls)
        entry.set_editor_property("weight", float(weight))
        pool.append(entry)
    return pool


def spawn_manager(eas, classes, trigger, blockers, respawn):
    manager = spawn_actor(
        eas, classes.get("arena_manager"), [0, -200, 2100], 0.0,
        "BLK_A10_Football_ArenaManager", "A10_Football/Logic")
    if not manager:
        warn("ArenaManager failed to spawn")
        return None
    try:
        manager.set_editor_property("arena_mode", unreal.ArenaMode.SUSTAIN)
        manager.set_editor_property("sustain_enemy_pool", build_sustain_pool(classes))
        manager.set_editor_property("max_sustain_enemies", 6)
        manager.set_editor_property("sustain_total_enemies", 18)
        if trigger:
            manager.set_editor_property("entry_triggers", [trigger])
        if blockers:
            manager.set_editor_property("exit_blockers", blockers)
        if respawn:
            manager.set_editor_property("player_respawn_point", respawn)
        log("ArenaManager sustain: max_alive=6 total=18")
    except Exception as e:
        warn("ArenaManager wiring failed: {}".format(e))
    return manager


def ensure_dynamic_nav(eas):
    nav_actor = None
    for actor in eas.get_all_level_actors():
        if actor and actor.get_class().get_name() == "RecastNavMesh":
            nav_actor = actor
            break
    if nav_actor is None:
        nav_actor = eas.spawn_actor_from_class(unreal.RecastNavMesh, unreal.Vector(0, 0, 0))
    if nav_actor:
        try:
            nav_actor.set_editor_property("runtime_generation",
                                          unreal.RuntimeGenerationType.DYNAMIC)
            log("RecastNavMesh runtime_generation -> Dynamic")
        except Exception as e:
            warn("Could not set RecastNavMesh Dynamic: {}".format(e))


def build():
    ba.ensure_safe_to_build()
    les, eas = ba.get_subsystems()
    mats = ba.ensure_materials(force=False)
    ba.open_or_create_level(les, LEVEL_PATH)
    ba.report_sublevels("After load")
    ba.clear_tagged(eas, TAG, LEVEL_PATH)

    classes = {key: ba.resolve_class(path) for key, path in CLASSES.items()}

    blockers = []
    count = 0
    for piece in pieces():
        actor = ba.spawn_shape(eas, mats, piece, TAG, ARENA)
        if actor:
            count += 1
            if piece.get("mat") == "field":
                blockers.append(actor)
    log("Spawned {} geometry pieces".format(count))

    ba.spawn_navmesh_bounds(
        eas, TAG, ARENA,
        {"navmesh_bounds": {"pos": [0, 0, 1000], "size": [8600, 11600, 2600]}})

    player = spawn_actor(
        eas, unreal.PlayerStart, [0, -6150, 100], 90.0,
        "BLK_A10_Football_PlayerStart", "A10_Football/Logic")
    respawn = spawn_actor(
        eas, unreal.TargetPoint, [0, -5150, 100], 90.0,
        "BLK_A10_Football_Respawn", "A10_Football/Logic")
    if player:
        log("PlayerStart at south run-up")

    trigger = spawn_entry_trigger(eas)
    spawn_ball(eas, classes)
    spawn_goal(eas, classes)
    spawn_spawns(eas, classes)
    spawn_combat_props(eas, classes)
    spawn_nav_links(eas)
    spawn_actor(
        eas, classes.get("coordinator"), [0, 0, 1900], 0.0,
        "BLK_A10_Football_Coordinator", "A10_Football/Logic")
    spawn_manager(eas, classes, trigger, blockers, respawn)

    ensure_dynamic_nav(eas)
    try:
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        unreal.SystemLibrary.execute_console_command(world, "RebuildNavigation")
        log("RebuildNavigation issued")
    except Exception as e:
        warn("RebuildNavigation failed: {}".format(e))

    ba.backup_level(LEVEL_PATH, ARENA)
    ba.report_sublevels("Before save")
    if not les.save_current_level():
        warn("save_current_level returned false")
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    ba.dump_actors(eas, TAG, ARENA, LEVEL_PATH)

    ba.take_screenshots(eas, {
        "screenshots": [
            {"id": "top", "pos": [0, 0, 9000], "pitch": -90, "yaw": -90, "fov": 72},
            {"id": "kickoff", "pos": [0, -5650, 450], "pitch": -4, "yaw": 90, "fov": 88},
            {"id": "goal_lane", "pos": [0, -1600, 520], "pitch": -3, "yaw": 90, "fov": 78},
            {"id": "iso_flanks", "pos": [5600, -5600, 3300], "pitch": -26, "yaw": 135, "fov": 86},
        ]
    }, ARENA)
    log("RESULT: SUCCESS - {} built into {}".format(ARENA, LEVEL_PATH))


def main():
    ok = False
    quit_when_done = "--quit" in sys.argv[1:]
    try:
        build()
        ok = True
    except Exception:
        import traceback
        for line in traceback.format_exc().splitlines():
            warn(line)
    finally:
        log("RESULT: {}".format("SUCCESS" if ok else "FAILED"))
        if quit_when_done:
            log("Quitting editor (--quit)")
            unreal.SystemLibrary.quit_editor()


main()
