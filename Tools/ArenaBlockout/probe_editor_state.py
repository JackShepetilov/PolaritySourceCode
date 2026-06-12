# Quick read-only probe: PIE state + current editor world + landscape presence.
# Log tag: [PROBE]
import unreal


def log(msg):
    unreal.log("[PROBE] {}".format(msg))


les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
try:
    log("PIE active: {}".format(les.is_in_play_in_editor()))
except Exception as e:
    log("PIE check failed: {}".format(e))

w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
if w:
    log("Editor world: {}".format(w.get_package().get_name()))
else:
    log("Editor world: None")

try:
    if hasattr(unreal, "LandscapeService"):
        log("LandscapeService present, landscape_exists(Biome1Island): {}".format(
            unreal.LandscapeService.landscape_exists("Biome1Island")))
    else:
        log("LandscapeService missing")
except Exception as e:
    log("Landscape probe failed: {}".format(e))

dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
log("Dirty map packages: {}".format([p.get_name() for p in dirty]))
