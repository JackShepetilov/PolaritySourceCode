import unreal
import os
import shutil
HERE = ("C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8"
        "/Source/Tools/MapProd")
sid = open(os.path.join(HERE, "poi_arg.txt")).read().strip()
level = "/Game/MapProd/Src/L_Src_" + sid
tail = "L_Src_" + sid
label = "Src" + sid
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
if dirty:
    raise RuntimeError("unsaved maps: " + str([d.get_name() for d in dirty]))
cur = les.get_current_level()
if not (cur and cur.get_outer().get_name().endswith(tail)):
    if not unreal.EditorAssetLibrary.does_asset_exist(level):
        raise RuntimeError("no level " + level)
    les.load_level(level)
cur = les.get_current_level()
got = cur.get_outer().get_name() if cur else ""
if not got.endswith(tail):
    raise RuntimeError("wrong level: " + got)
src = os.path.join(HERE, "poi", sid, "heightmap.png")
baseline = os.path.join(HERE, "poi", sid, "imported.png")
if not os.path.exists(src):
    raise RuntimeError("no " + src + " - run terrain.py first")
old = next(a for a in eas.get_all_level_actors() if isinstance(a, unreal.Landscape))
xf = old.get_actor_transform()
eas.destroy_actor(old)
print("DELETED: old landscape", label)
r = unreal.LandscapeService.import_heightmap(label, src)
if not getattr(r, "success", False):
    raise RuntimeError("import failed: " + str(getattr(r, "error_message", "")))
new = next(a for a in eas.get_all_level_actors() if isinstance(a, unreal.Landscape))
new.set_actor_label(label)
new.set_actor_transform(xf, False, False)
print("CREATED:", label)
shutil.copyfile(src, baseline)
print("BASELINE ->", baseline)
print("save:", les.save_current_level())

# end of script -- keep this line, the runner clips the tail
