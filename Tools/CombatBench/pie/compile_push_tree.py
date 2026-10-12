import unreal
T = "/Game/Variant_Shooter/Blueprints/AI/STs/ST_ShooterNPC_Push"
r = unreal.StateTreeService.compile_state_tree(T)
print("COMPILE", r)
print("SAVE", unreal.StateTreeService.save_state_tree(T))
