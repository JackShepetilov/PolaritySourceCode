# Three-POI scenario for the squad playground.
#
# Builds the loadouts and the scenario that test a CHAIN of fights rather than one fight:
#
#   POI1 (faction A) --attacks--> POI2 (faction B: one squad defends, one squad attacks POI3)
#                                 POI3 (faction A) defends, and is the stronger side there
#
# The point of the shape: B's attack on POI3 is meant to fail. Its survivors withdraw to POI2,
# which is their spawn point, find the defenders there in better shape than themselves, and merge
# into that squad - so the first fight's outcome decides the odds of the second one.
#
# Numbers chosen so the break can actually happen: a squad of two can never fall to a third of its
# strength without being wiped, so the attacker is four drones breaking at 0.6 (two down, two left
# to walk home and join the defence).
#
# Usage, with the editor open and the playground level loaded:
#   exec(open(r"<project>/Source/Tools/SquadPlayground/build_scenario_three_pois.py", encoding="utf-8").read())
#
# Log filter tag: [SCENARIO]

import unreal

FOLDER = "/Game/Squads"
BPS = "/Game/Variant_Shooter/Blueprints/AI/BPs/"

TEAM_PLAYERS = 0
TEAM_A = 1
TEAM_B = 2


def log(msg):
    unreal.log("[SCENARIO] {}".format(msg))


def make_asset(name, cls):
    path = "{}/{}".format(FOLDER, name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        log("reusing {}".format(path))
        return unreal.load_asset(path)

    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", cls)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, FOLDER, cls, factory)
    log("created {}".format(path))
    return asset


def entry(bp_name, count):
    e = unreal.SquadLoadoutEntry()
    e.set_editor_property("npc_class", unreal.load_object(None, "{}{}.{}_C".format(BPS, bp_name, bp_name)))
    e.set_editor_property("count", count)
    return e


def loadout(name, members, team, task, formation, withdraw_strength, regroup_seconds):
    asset = make_asset(name, unreal.SquadLoadout)
    asset.set_editor_property("members", members)
    asset.set_editor_property("faction_team_id", team)
    asset.set_editor_property("initial_task", task)
    asset.set_editor_property("formation", formation)
    asset.set_editor_property("withdraw_strength", withdraw_strength)
    asset.set_editor_property("regroup_seconds", regroup_seconds)
    unreal.EditorAssetLibrary.save_loaded_asset(asset)
    return asset


ATTACK = unreal.SquadInitialTask.ATTACK
DEFEND = unreal.SquadInitialTask.DEFEND
WEDGE = unreal.SquadFormation.WEDGE
LINE = unreal.SquadFormation.LINE

# --- A attacks the centre from the west ---
a_assault = loadout("DA_A_Assault_POI1",
                    [entry("BP_ShooterNPC", 2), entry("BP_GrenadierNPC", 1)],
                    TEAM_A, ATTACK, WEDGE, 0.34, 20.0)

# --- B holds the centre ---
b_defend = loadout("DA_B_Defend_POI2",
                   [entry("BP_TrackedTank", 1), entry("BP_FlyingDrone", 1)],
                   TEAM_B, DEFEND, LINE, 0.34, 20.0)

# --- B attacks the east, and is meant to lose there ---
b_assault = loadout("DA_B_Assault_POI3",
                    [entry("BP_FlyingDrone", 4)],
                    TEAM_B, ATTACK, WEDGE, 0.6, 15.0)

# --- A holds the east, stronger than the attack coming at it ---
a_defend = loadout("DA_A_Defend_POI3",
                   [entry("BP_ShooterNPC", 2), entry("BP_JuggernautNPC", 1)],
                   TEAM_A, DEFEND, LINE, 0.34, 20.0)


def scenario_entry(point_tag, squad, target_tag=None):
    e = unreal.SquadScenarioEntry()
    e.set_editor_property("point_tag", unreal.Name(point_tag))
    e.set_editor_property("loadout", squad)
    if target_tag:
        e.set_editor_property("target_point_tag", unreal.Name(target_tag))
    return e


scenario = make_asset("SC_ThreePOI", unreal.SquadScenario)
scenario.set_editor_property("entries", [
    scenario_entry("POI1", a_assault, "POI2"),
    scenario_entry("POI2", b_defend),
    scenario_entry("POI2", b_assault, "POI3"),
    scenario_entry("POI3", a_defend),
])
scenario.set_editor_property("description", unreal.Text(
    "A pushes POI2 from POI1. B holds POI2 and sends a weaker force at POI3, which A holds in "
    "strength. The beaten attackers fall back onto POI2 and join its defence."))
unreal.EditorAssetLibrary.save_loaded_asset(scenario)

log("scenario ready: {}/SC_ThreePOI".format(FOLDER))
for e in scenario.get_editor_property("entries"):
    lo = e.get_editor_property("loadout")
    log("  {} -> {} (team {}, {}) target={}".format(
        e.get_editor_property("point_tag"),
        lo.get_name(),
        lo.get_editor_property("faction_team_id"),
        lo.get_editor_property("initial_task"),
        e.get_editor_property("target_point_tag")))
