import unreal  # first line on purpose: execute_python_code only accepts code that starts with it

# Move two of the bench points into streamed sublevels.
#
# DOES NOT WORK YET - do not run it expecting a streaming bench. It builds the sublevels and moves
# the actors correctly in the editor, but the packages it produces never load in PIE:
#
#   LogLevel: ActivateLevel /Game/MapEventBench/UEDPIE_0_L_Bench_Sub_West 1 1 1
#   LogUObjectGlobals: Warning: Failed to find object 'Object /Game/MapEventBench/UEDPIE_0_L_Bench_Sub_West.L_Bench_Sub_West'
#
# The world object inside the package does not carry the package's own name, so the PIE duplicate
# has nothing to activate. `should_be_loaded`, `flush_level_streaming` and `load_stream_level` all
# report success and change nothing. Suspected fix, untried: build the level first with
# `LevelEditorSubsystem.new_level`, then attach it with `EditorLevelUtils.add_level_to_world`, and
# only then move the actors - rather than letting create_new_streaming_level do all three.
#
# The architectural claim this was meant to prove is proven anyway, by the cheaper route: destroying
# a point actor mid-run runs the same EndPlay -> UnregisterPoi path a sublevel unload runs, and the
# director keeps the point's owner, its loot flag and its garrison flag with loaded=False. See
# check_streaming.py and the session notes.
#
# This is not decoration. The whole reason the run director owns the state of the war, and the point
# actors only report presence, is that points live in sublevels and a sublevel that unloads must not
# take the run with it. Until two of them actually stream, that claim is untested.
#
# Run once, after build_bench.py. Re-running is safe: it skips points that already left the
# persistent level.
#
# Usage:
#   exec this file through execute_python_code, then call main()
#
# Log filter tag: [BENCH]

PERSISTENT = "/Game/MapEventBench/L_MapEventBench"
SUBLEVELS = (
    ("/Game/MapEventBench/L_Bench_Sub_West", ("P_Mission_West",)),
    ("/Game/MapEventBench/L_Bench_Sub_East", ("P_Mission_East",)),
)


def log(msg):
    unreal.log("[BENCH] {}".format(msg))


def main():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    # Creating a streaming level makes it the current one, so a second run starts standing inside
    # the sublevel it made last time. Step back into the persistent level rather than refusing.
    current = les.get_current_level()
    current_name = current.get_outer().get_name() if current else ""
    if current_name.startswith("L_Bench_Sub_"):
        les.set_current_level_by_name("L_MapEventBench")
        current = les.get_current_level()
        current_name = current.get_outer().get_name() if current else ""

    if not current_name.endswith("L_MapEventBench"):
        raise RuntimeError("Open {} first (currently {})".format(PERSISTENT, current_name))

    for path, tags in SUBLEVELS:
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            log("{} already exists, skipping".format(path))
            continue

        wanted = []
        for actor in eas.get_all_level_actors():
            if isinstance(actor, unreal.PoiActor):
                if str(actor.get_editor_property("poi_tag")) in tags:
                    wanted.append(actor)

        if not wanted:
            log("nothing left to move into {}".format(path))
            continue

        # Read the tags first: moving an actor into a new level destroys and recreates it, and every
        # Python reference held across the call dies with "ObjectInstance is null!".
        moved = [str(a.get_editor_property("poi_tag")) for a in wanted]

        eas.set_selected_level_actors(wanted)
        streaming = unreal.EditorLevelUtils.create_new_streaming_level(
            unreal.LevelStreamingDynamic, path, True)
        if not streaming:
            raise RuntimeError("Failed to create streaming level " + path)

        log("moved {} into {}".format(moved, path))

        # Back to the persistent level before the next one, for the same reason as above.
        les.set_current_level_by_name("L_MapEventBench")

    eas.set_selected_level_actors([])
    saved = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    log("Saved: {}".format(saved))
