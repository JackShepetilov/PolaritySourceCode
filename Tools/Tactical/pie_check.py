import unreal
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
print("LEVEL", les.get_current_level().get_outer().get_name())
print("DIRTY", [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()])
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
acts = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
npcs = [a for a in acts if isinstance(a, unreal.ShooterNPC)]
print("NPCS", len(npcs), sorted(set(a.get_class().get_name() for a in npcs))[:10])
print("PIE", unreal.PerformanceService.frame_timing())
