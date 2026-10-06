import unreal
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
cls = unreal.load_object(None, "/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation/ARs/BP_G3.BP_G3_C")
print("cls", cls)
wp = pawn.add_weapon_class_animated(cls)
print("added", wp)
