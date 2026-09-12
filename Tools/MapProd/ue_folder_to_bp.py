import unreal
import os

# Собрать блюпринт из папки акторов уровня, с явно заданным пивотом.
#
# Зачем скриптом, а не кнопкой: в 5.8 «Convert To Blueprint Class» в контекстном меню не нашлось, а
# главное - кнопка ставит корень туда, куда решит движок. Дом с пивотом в стороне или в воздухе
# приходится подгонять на глаз при каждой расстановке, и наследники наследуют эту кривизну.
#
# Оригиналы на уровне НЕ трогаются: собрали, посмотрели, и только потом решать, сносить ли их.

LEVEL_TAIL = "L_Src_I2"
FOLDER = "House1"
PIVOT_LABEL = "SM_Bld_Block_1x1_01P"
BP_PATH = "/Game/MapProd/Blueprints"
BP_NAME = "BP_House1"

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

cur = les.get_current_level()
got = cur.get_outer().get_name() if cur else ""
if not got.endswith(LEVEL_TAIL):
    raise RuntimeError("wrong level: " + got)

actors = [a for a in eas.get_all_level_actors() if FOLDER in str(a.get_folder_path())]
if not actors:
    raise RuntimeError("folder empty: " + FOLDER)

pivots = [a for a in actors if a.get_actor_label() == PIVOT_LABEL]
if len(pivots) != 1:
    raise RuntimeError("pivot not unique: {} found {}".format(PIVOT_LABEL, len(pivots)))
# Пивот берётся БЕЗ масштаба, только место и поворот.
#
# Иначе make_relative_transform делит смещения на масштаб пивота, а этот блок отмасштабирован:
# дом на пять метров сложился в полметра и на превью выглядел вертикальной гирляндой. Масштаб
# самих кусков при этом свой и сохраняется - делится только СИСТЕМА ОТСЧЁТА.
pivot_xf = unreal.Transform(pivots[0].get_actor_location(),
                            pivots[0].get_actor_rotation(),
                            unreal.Vector(1.0, 1.0, 1.0))
print("pivot scale ignored, actor had", pivots[0].get_actor_scale3d())
print("actors: {}, pivot at {}".format(len(actors), pivot_xf.translation))

# Блюпринт заводится фабрикой: у BlueprintService метода create нет.
full = BP_PATH + "/" + BP_NAME
if unreal.EditorAssetLibrary.does_asset_exist(full):
    # Пересборка: сносим ТОЛЬКО то, что этот же скрипт и делает. Ручных правок внутри быть не
    # должно - для них есть наследник, а этот блюпринт генерируемый.
    print("rebuilding, dropping old", full, unreal.EditorAssetLibrary.delete_asset(full))
fac = unreal.BlueprintFactory()
fac.set_editor_property("parent_class", unreal.Actor)
bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset(BP_NAME, BP_PATH, unreal.Blueprint, fac)
print("CREATED:", full)

sub = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
handles = sub.k2_gather_subobject_data_for_blueprint(bp)

root = handles[0]

made = 0
skipped = []
for a in actors:
    smc = a.get_component_by_class(unreal.StaticMeshComponent)
    mesh = smc.get_editor_property("static_mesh") if smc else None
    is_plain = type(a).__name__ == "StaticMeshActor" and mesh is not None

    params = unreal.AddNewSubobjectParams()
    params.set_editor_property("parent_handle", root)
    params.set_editor_property("blueprint_context", bp)
    # Трансформ ставим сами, относительно пивота: пусть движок ничего не подгоняет.
    params.set_editor_property("conform_transform_to_parent", False)
    params.set_editor_property("new_class",
                               unreal.StaticMeshComponent if is_plain else unreal.ChildActorComponent)

    handle, fail = sub.add_new_subobject(params)
    if fail and str(fail):
        skipped.append((a.get_actor_label(), str(fail)))
        continue

    data = sub.k2_find_subobject_data_from_handle(handle)
    comp = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(data)

    # Относительный трансформ считает движок. Transform.multiply перемножает НЕ в том порядке, и
    # первая сборка дома развалилась в вертикальную гирлянду: компоненты разъехались по высоте.
    rel = unreal.MathLibrary.make_relative_transform(a.get_actor_transform(), pivot_xf)

    if is_plain:
        comp.set_editor_property("static_mesh", mesh)
        mats = smc.get_editor_property("override_materials")
        if mats:
            comp.set_editor_property("override_materials", mats)
        # Профиль коллизии ставится методом, а не свойством: у компонента такого UPROPERTY нет.
        comp.set_collision_profile_name(smc.get_collision_profile_name())
    else:
        # Не простой меш - вставляем как дочерний актор. ВНИМАНИЕ: у ChildActorComponent берутся
        # ДЕФОЛТЫ класса, настройки конкретного экземпляра на уровне сюда не переезжают.
        comp.set_editor_property("child_actor_class", a.get_class())

    # Компонент шаблона, никакой физики: свип и телепорт выключены явно, иначе вызов не принимается.
    comp.set_relative_transform(rel, False, False)
    sub.rename_subobject(handle, a.get_actor_label())
    made += 1

print("components added:", made)
for lbl, why in skipped:
    print("  SKIPPED", lbl, why)

unreal.EditorAssetLibrary.save_loaded_asset(bp)
print("saved")

# end of script
