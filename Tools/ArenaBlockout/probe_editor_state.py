# Read-only probe of the current editor state before any navlink work.
# Reports: PIE state, world name, dirty maps, which biome-1 arena sublevels are loaded,
# and how many BLOCKOUT meshes + NavLinkProxy each currently has in the editor world.
# Run: py "<...>/probe_editor_state.py"   Log tag: [PROBE]
import unreal

ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
world = ues.get_editor_world()


def log(m):
    unreal.log("[PROBE] {}".format(m))


try:
    log("PIE active: {}".format(les.is_in_play_in_editor()))
except Exception as e:
    log("PIE check failed: {}".format(e))

log("editor world = {}".format(world.get_package().get_name() if world else "None"))

try:
    dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    log("dirty maps: {}".format([p.get_name() for p in dirty]))
except Exception as e:
    log("dirty check err: {}".format(e))

ARENAS = ["A1_Pier", "A2_Courtyard", "A3_Dome", "A4_Hangar", "A5_Amphitheater",
          "A6_Villa", "A7_Gallery", "A8_Lighthouse", "A9_Temple"]

counts = {}
navs = {}
for a in eas.get_all_level_actors():
    try:
        tags = [str(t) for t in a.tags]
    except Exception:
        continue
    cls = a.get_class().get_name()
    for t in tags:
        if t.startswith("BLOCKOUT_"):
            arena = t[len("BLOCKOUT_"):]
            if cls == "NavLinkProxy":
                navs[arena] = navs.get(arena, 0) + 1
            else:
                counts[arena] = counts.get(arena, 0) + 1

for arena in ARENAS:
    pkg = "/Game/Variant_Shooter/Arenas/Biome1/{}/Lvl_{}".format(arena, arena)
    sl = unreal.GameplayStatics.get_streaming_level(world, pkg)
    loaded = "NO-STREAM"
    if sl:
        try:
            loaded = "loaded={}".format(sl.is_level_loaded())
        except Exception as e:
            loaded = "stream-err:{}".format(e)
    log("{:18s} [{}] meshes={} navlinks={}".format(
        arena, loaded, counts.get(arena, 0), navs.get(arena, 0)))

log("DONE")
