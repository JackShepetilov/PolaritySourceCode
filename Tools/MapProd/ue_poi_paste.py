import unreal

# ШАГ 2 из 2: вставить содержимое буфера в боевую карту.
#
# Идемпотентность по тегу: свои прошлые копии сносим, чужие акторы не трогаем НИКОГДА.

SRC = "/Game/MapProd/Src/L_Src_I2"
DST = "/Game/MapProd/Test/L_FightTest"
TAG = "PoiImport_I2"
FOLDER = "Imported/I2"

# Что НЕ едет. Ландшафт у приёмника свой; небо и постпроцесс там уже есть; вода отдельно - её кисть
# лепит ландшафт, и в боевой карте она перепахала бы рельеф, который туда кладёт генератор.
SKIP_CLASSES = ("Landscape", "LandscapeStreamingProxy", "PostProcessVolume")
SKIP_NAME_PARTS = ("Water", "SeaPlane", "Ultra_Dynamic_Sky")

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

dirty = [d.get_name() for d in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
if dirty:
    raise RuntimeError("unsaved maps: " + str(dirty))

if not les.get_current_level().get_outer().get_name().endswith(DST.rsplit("/", 1)[-1]):
    les.load_level(DST)
cur = les.get_current_level()
if not cur.get_outer().get_name().endswith(DST.rsplit("/", 1)[-1]):
    raise RuntimeError("wrong target level")

old = [a for a in eas.get_all_level_actors() if TAG in [str(t) for t in a.tags]]
for a in old:
    eas.destroy_actor(a)
print("removed previous import:", len(old))

before = set(eas.get_all_level_actors())
unreal.SystemLibrary.execute_console_command(ues.get_editor_world(), "EDIT PASTE")
fresh = [a for a in eas.get_all_level_actors() if a not in before]
print("pasted:", len(fresh))

for a in fresh:
    tags = [str(t) for t in a.tags]
    tags.append(TAG)
    a.set_editor_property("tags", tags)
    a.set_folder_path(FOLDER)

print("save:", les.save_current_level())

# end of script
