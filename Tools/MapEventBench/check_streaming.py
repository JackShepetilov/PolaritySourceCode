import unreal  # first line on purpose: execute_python_code only accepts code that starts with it

# The one check that justifies the architecture.
#
# The run director owns the state of the war and the point actors only report presence. The reason
# is streaming: points live in sublevels, and a sublevel that unloads must not take the run with it.
# Until a sublevel is actually unloaded mid-run and the state survives, that is a claim, not a fact.
#
# Two stages, because unloading a level takes a frame or two and Python cannot wait inside one call:
#   stage_unload()  - reads the state, then asks the sublevel to unload
#   stage_verify()  - reads it again and says whether anything was lost
#
# Run both while PIE is going, one MCP call each.
#
# Log filter tag: [BENCH]

SUBLEVEL = "L_Bench_Sub_West"
TAG_IN_SUBLEVEL = "P_Mission_West"

STATE_FILE = "{}/bench_streaming_before.txt".format(unreal.Paths.project_saved_dir())


def world():
    w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if not w:
        raise RuntimeError("No game world - start PIE first")
    return w


def director(w):
    # Any actor in the world is a good enough world context.
    anchor = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.PoiActor)
    if not anchor:
        raise RuntimeError("No points in the world")
    d = unreal.RunDirectorSubsystem.get_run_director(anchor[0])
    if not d:
        raise RuntimeError("No run director in this world")
    return d


def describe(state):
    return "held={} progress={:.2f} loot_spawned={} garrison_spawned={} loaded={} money={}".format(
        state.controlling_team, state.capture_progress, state.loot_spawned,
        state.garrison_spawned, state.loaded, state.money_stacks_placed)


def stage_unload():
    w = world()
    d = director(w)
    found, state = d.get_poi_state(unreal.Name(TAG_IN_SUBLEVEL))
    if not found:
        raise RuntimeError("{} is not registered".format(TAG_IN_SUBLEVEL))

    before = describe(state)
    with open(STATE_FILE, "w", encoding="utf-8") as f:
        f.write(before)
    unreal.log("[BENCH] before unload: {}".format(before))

    ls = unreal.GameplayStatics.get_streaming_level(w, SUBLEVEL)
    if not ls:
        raise RuntimeError("No streaming level called " + SUBLEVEL)
    ls.set_should_be_loaded(False)
    ls.set_should_be_visible(False)
    unreal.log("[BENCH] asked {} to unload; run stage_verify() next".format(SUBLEVEL))


def stage_verify():
    w = world()
    d = director(w)

    actors = [a for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.PoiActor)
              if str(a.get_editor_property("poi_tag")) == TAG_IN_SUBLEVEL]

    found, state = d.get_poi_state(unreal.Name(TAG_IN_SUBLEVEL))
    after = describe(state) if found else "GONE"
    before = open(STATE_FILE, encoding="utf-8").read()

    print("  actor in world: {}".format("still there (sublevel has not unloaded yet)" if actors else "gone"))
    print("  before: {}".format(before))
    print("  after : {}".format(after))

    ok_state = found and state.controlling_team == int(before.split("held=")[1].split(" ")[0])
    ok_loaded = found and not state.loaded
    ok_flags = found and state.loot_spawned and state.garrison_spawned

    print("  [{}] state survived the unload".format("PASS" if ok_state else "FAIL"))
    print("  [{}] point is marked streamed-out".format("PASS" if ok_loaded else "FAIL"))
    print("  [{}] loot and garrison stay claimed (no refill on reload)".format("PASS" if ok_flags else "FAIL"))
