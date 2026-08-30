import unreal  # first line on purpose: execute_python_code only accepts code that starts with it

# Map event bench: one snapshot of the war, as PASS/FAIL.
#
# Reads the live state through the point actors rather than scraping the log, so a check that passes
# is a check on the numbers the game is using. Every line has a hand equivalent: `polarity.map.dump`
# prints the same thing to the console, and the author can walk the same steps with the same
# commands.
#
# The run director is a world subsystem and Python has no way to reach one (there is no
# SubsystemBlueprintLibrary binding), so the director-side totals - phase, money, earned conditions -
# come from the console dump in the log. Everything per-point comes from APoiActor::GetWarState.
#
# Run it while PIE is going. Each call is one snapshot; the war moves between calls, and that is the
# point - the bench is watched in stages, not asserted once.
#
# Log filter tag: [BENCH]

POINTS = ("P_Mission_West", "P_Mission_East", "P_Mission_North", "P_Plain_South", "P_Final",
          "HQ_A", "HQ_B")

# Where somebody has to be able to walk for the bench to mean anything.
NAV_PROBES = ((0, 0), (-4500, 4500), (-12000, 0), (12000, 0), (-6000, -3000), (6000, -3000),
              (0, -8000), (0, 10000), (0, -14000), (-14000, 6500), (14000, 6500))

TEAM_NAMES = {0: "players", 1: "A", 2: "B", 255: "nobody"}


def team(t):
    return TEAM_NAMES.get(int(t), str(t))


def row(ok, name, detail):
    print("  [{}] {:<36} {}".format("PASS" if ok else "FAIL", name, detail))
    return 1 if ok else 0


def main():
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if not world:
        raise RuntimeError("No game world - start PIE first")

    passed = 0
    total = 0

    # --- 1. is anybody home ---
    pc = unreal.GameplayStatics.get_player_controller(world, 0)
    pawn = pc.get_controlled_pawn() if pc else None
    total += 1
    passed += row(pawn is not None, "1 player pawn spawned",
                  "{} at {}".format(pawn.get_name() if pawn else "NONE",
                                    pawn.get_actor_location() if pawn else "-"))

    # --- 2. navigation, before anything is blamed on the AI ---
    ext = unreal.Vector(1000, 1000, 1000)
    nav_ok = 0
    missing = []
    for (x, y) in NAV_PROBES:
        r = unreal.NavigationSystemV1.project_point_to_navigation(
            world, unreal.Vector(x, y, 100), nav_data=None, filter_class=None, query_extent=ext)
        if r:
            nav_ok += 1
        else:
            missing.append((x, y))
    total += 1
    passed += row(nav_ok == len(NAV_PROBES), "2 navmesh covers the map",
                  "{}/{} probes{}".format(nav_ok, len(NAV_PROBES),
                                          "" if not missing else " | no nav at {}. Run RebuildNavigation".format(missing)))

    # --- 3. points ---
    pois = {str(p.get_editor_property("poi_tag")): p
            for p in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PoiActor)}
    total += 1
    passed += row(all(t in pois for t in POINTS), "3 every point registered",
                  "{}/{}".format(len(pois), len(POINTS)))

    print("  --- war ---")
    money = 0
    loot_done = 0
    garrison_done = 0
    contested = 0
    windows = 0
    for tag in POINTS:
        p = pois.get(tag)
        if not p:
            continue
        s = p.get_war_state()
        money += s.money_stacks_placed
        loot_done += 1 if s.loot_spawned else 0
        garrison_done += 1 if s.garrison_spawned else 0
        contested += 1 if s.contested else 0
        windows += 1 if s.mission_window_open else 0
        print("    {:<16} held {:<8} capture {:>3.0f}% by {:<8}{}{}{}{}".format(
            tag, team(s.controlling_team), s.capture_progress * 100.0, team(s.capturing_team),
            " CONTESTED" if s.contested else "",
            " WINDOW-OPEN" if s.mission_window_open else "",
            " MISSION-DONE" if s.mission_completed else (" MISSION-EXPIRED" if s.mission_expired else ""),
            "" if s.loaded else " streamed-out"))

    total += 1
    passed += row(4 <= money <= 6, "4 money budget", "{} stacks, target 4-6".format(money))

    total += 1
    passed += row(loot_done >= 6, "5 loot placed once per point", "{}/7 points".format(loot_done))

    total += 1
    passed += row(garrison_done >= 5, "6 garrisons placed", "{}/7 points".format(garrison_done))

    # --- 4. headquarters are broken, never taken ---
    total += 1
    hq_ok = True
    for tag, expect in (("HQ_A", 1), ("HQ_B", 2)):
        p = pois.get(tag)
        hq_ok = hq_ok and p is not None and int(p.get_war_state().controlling_team) == expect
    passed += row(hq_ok, "7 headquarters never change hands", "A held by A, B held by B")

    # --- 5. is the war actually moving ---
    pawns = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Pawn)
    moving = 0
    for p in pawns:
        v = p.get_velocity()
        if (v.x * v.x + v.y * v.y) ** 0.5 > 20.0:
            moving += 1
    total += 1
    passed += row(moving > 0, "8 somebody is marching", "{} of {} pawns moving".format(moving, len(pawns)))

    print("  --- contested now: {} | mission windows open: {} ---".format(contested, windows))
    print("=== {}/{} checks passed at this snapshot ===".format(passed, total))

    # The director side goes to the log, where the console command puts it too.
    unreal.SystemLibrary.execute_console_command(world, "polarity.map.dump")
    print("(director dump written to the log, filter [MAP_DEBUG])")


main()
