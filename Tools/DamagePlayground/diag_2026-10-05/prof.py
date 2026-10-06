import unreal
print([n for n in dir(unreal.CollisionChannel) if n.startswith("ECC")])
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
ch = unreal.CollisionChannel.ECC_PROJECTILE
print("chan", ch)
c = unreal.SphereComponent()
counts = {}
for prof in ["OverlapAll", "OverlapAllDynamic", "OverlapOnlyPawn", "UI", "Trigger", "Spectator", "IgnoreOnlyPawn", "Pawn", "CharacterMesh", "Ragdoll", "PhysicsActor", "BlockAllDynamic", "InvisibleWall"]:
    c.set_collision_profile_name(prof)
    print(prof, c.get_collision_response_to_channel(ch) if ch else "?", "| WorldDynamic:", c.get_collision_response_to_channel(unreal.CollisionChannel.ECC_WORLD_DYNAMIC))
