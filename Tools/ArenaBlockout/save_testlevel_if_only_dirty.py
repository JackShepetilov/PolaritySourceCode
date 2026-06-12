# One-shot: the default startup map auto-dirties itself ~1 min after editor
# launch (construction-script side effects), which trips the dirty-map guard
# in build_biome_island. If the ONLY dirty map is TestLevel - save it (saving
# never loses anything; a scripted load would discard). Anything else dirty -
# refuse loudly so the author decides.
# Log tag: [DIRTY_FIX]
import unreal

ALLOWED = "/Game/Variant_Shooter/TestLevel"

dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
names = [p.get_name() for p in dirty]
if not dirty:
    unreal.log("[DIRTY_FIX] nothing dirty - proceed")
elif names == [ALLOWED]:
    ok = unreal.EditorLoadingAndSavingUtils.save_packages(dirty, True)
    unreal.log("[DIRTY_FIX] saved auto-dirtied {} (ok={})".format(names, ok))
else:
    unreal.log_warning("[DIRTY_FIX] UNEXPECTED dirty maps: {} - NOT touching, "
                       "save manually".format(names))
