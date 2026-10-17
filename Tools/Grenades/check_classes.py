import unreal
for n in ["BenchGrenadeRack", "GrenadeDefinition_FPV", "GrenadeComponent", "GrenadeProjectile", "GrenadeDefinition_Frag", "GrenadeDefinition_Flash", "GrenadePickup", "DamageType_Fragmentation"]:
    print(n, unreal.find_object(None, "/Script/Polarity." + n) is not None)
print("dirty maps:", [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()])
print("pie:", unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None)
