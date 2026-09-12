import unreal
import os
HERE = ("C:/Users/Professional/Documents/Unreal Projects/Polarity_Main5_8"
        "/Source/Tools/MapProd")
sid = open(os.path.join(HERE, "poi_arg.txt")).read().strip()
level = "/Game/MapProd/Src/L_Src_" + sid
tail = "L_Src_" + sid
label = "Src" + sid
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
# Своя несохранённая лепка это ровно то, что мы пришли забрать: её надо сохранить, а не отказаться.
# Чужая - причина остановиться, потому что load_level выбросит её молча.
cur = les.get_current_level()
here = cur.get_outer().get_name() if cur else ""
dirty = [d.get_name() for d in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
mine = [d for d in dirty if d.endswith(tail)]
if mine and here.endswith(tail):
    print("saving my own level first:", mine)
    les.save_current_level()
    dirty = [d.get_name() for d in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
if dirty:
    raise RuntimeError("unsaved maps: " + str(dirty))
cur = les.get_current_level()
if not (cur and cur.get_outer().get_name().endswith(tail)):
    if not unreal.EditorAssetLibrary.does_asset_exist(level):
        raise RuntimeError("no level " + level)
    les.load_level(level)
cur = les.get_current_level()
got = cur.get_outer().get_name() if cur else ""
if not got.endswith(tail):
    raise RuntimeError("wrong level: " + got)
brushes = [a.get_actor_label() for a in eas.get_all_level_actors()
           if "BrushManager" in type(a).__name__]
if brushes:
    print("WARNING: landscape brushes present:", brushes)
    print("  turn off Affects Landscape or their work bakes into hand.png")
land = next(a for a in eas.get_all_level_actors() if isinstance(a, unreal.Landscape))
print("edit layers:", [str(l.get_name_bp()) for l in land.get_edit_layers_bp()])
out_dir = os.path.join(HERE, "poi", sid)
if not os.path.isdir(out_dir):
    os.makedirs(out_dir)
out = os.path.join(out_dir, "exported.png")
print("export:", unreal.LandscapeService.export_heightmap(label, out), "->", out)
print("now run:  python hand_pull.py " + sid)

# end of script -- keep this line, the runner clips the tail
