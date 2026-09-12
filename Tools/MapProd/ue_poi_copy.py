import unreal

# ШАГ 1 из 2: набрать акторы точки в буфер обмена редактора.
#
# Два шага, а не один, потому что смена уровня рвёт SSE-соединение MCP: скрипт с двумя load_level
# отрабатывает, но ответа не возвращает вообще - ни ошибки, ни вывода. Один load_level за вызов.
#
# Системы координат Src-уровня и боевой карты совпадают (оба ландшафта в нуле, масштаб 100), так
# что перенос идёт один в один, без пересчёта трансформов.

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

if not les.get_current_level().get_outer().get_name().endswith(SRC.rsplit("/", 1)[-1]):
    les.load_level(SRC)
cur = les.get_current_level()
if not cur.get_outer().get_name().endswith(SRC.rsplit("/", 1)[-1]):
    raise RuntimeError("wrong source level")

take, skipped = [], []
for a in eas.get_all_level_actors():
    cls = type(a).__name__
    lbl = a.get_actor_label()
    if cls in SKIP_CLASSES or any(k in cls or k in lbl for k in SKIP_NAME_PARTS):
        skipped.append(lbl + " (" + cls + ")")
        continue
    take.append(a)

print("to copy:", len(take))
for s in skipped:
    print("   skipped:", s)
if not take:
    raise RuntimeError("nothing to copy")

eas.set_selected_level_actors(take)
unreal.SystemLibrary.execute_console_command(ues.get_editor_world(), "EDIT COPY")
print("copied to clipboard; now run ue_poi_paste_run.py")

# end of script
