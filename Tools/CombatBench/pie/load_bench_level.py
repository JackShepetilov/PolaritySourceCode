import unreal
assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
ok = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level("/Game/Prototype/CombatBench/L_CombatBench")
print("LOADED", ok)
