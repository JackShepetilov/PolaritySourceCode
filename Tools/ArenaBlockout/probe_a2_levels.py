# Read-only: enumerate the levels in the current world and find which sublevel holds the
# BLOCKOUT_A2_Courtyard geometry + navlinks, so we can target the gameplay sublevel
# without touching the dirty art map. Log tag: [A2LV]
import unreal

ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
world = ues.get_editor_world()


def log(m):
    unreal.log("[A2LV] {}".format(m))


log("persistent world pkg = {}".format(world.get_package().get_name()))

# Try to enumerate streaming levels several ways.
try:
    sls = world.get_editor_property("streaming_levels")
    log("streaming_levels count = {}".format(len(sls)))
    for sl in sls:
        try:
            wa = sl.get_world_asset()
            name = wa.get_path_name() if wa else "?"
        except Exception:
            name = "?"
        try:
            loaded = sl.is_level_loaded()
        except Exception:
            loaded = "?"
        log("  SL: {} loaded={} class={}".format(name, loaded, sl.get_class().get_name()))
except Exception as e:
    log("streaming_levels err: {}".format(e))

# Where do the A2 BLOCKOUT actors actually live? (level package name)
lvl_pkgs = {}
sample_navlink_level = None
for a in eas.get_all_level_actors():
    try:
        tags = [str(t) for t in a.tags]
    except Exception:
        continue
    if "BLOCKOUT_A2_Courtyard" not in tags:
        continue
    try:
        lvl = a.get_outer()  # the ULevel
        pkg = lvl.get_outer().get_name() if lvl else "?"
    except Exception:
        pkg = "err"
    lvl_pkgs[pkg] = lvl_pkgs.get(pkg, 0) + 1
    if a.get_class().get_name() == "NavLinkProxy" and sample_navlink_level is None:
        sample_navlink_level = pkg

log("A2 BLOCKOUT actors by level-package: {}".format(lvl_pkgs))
log("navlink sample level-package: {}".format(sample_navlink_level))
log("DONE")
