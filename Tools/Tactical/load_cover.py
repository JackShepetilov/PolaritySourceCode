import unreal
if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world():
    print("PIE STILL RUNNING")
else:
    dirty = [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
    if dirty:
        print("DIRTY, refusing:", dirty)
    else:
        les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        print("LOAD", les.load_level("/Game/Variant_Shooter/CoverTestLevel"))
        acts = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
        print("NPCS", [(a.get_name(), a.get_actor_location()) for a in acts if isinstance(a, unreal.ShooterNPC)][:8])
