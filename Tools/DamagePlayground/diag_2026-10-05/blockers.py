import unreal, collections
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
P = unreal.CollisionChannel.ECC_PROJECTILE; WD = unreal.CollisionChannel.ECC_WORLD_DYNAMIC
B = unreal.CollisionResponseType.ECR_BLOCK
cnt = collections.Counter()
for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.Actor):
    for c in a.get_components_by_class(unreal.PrimitiveComponent):
        ce = c.get_collision_enabled()
        if ce == unreal.CollisionEnabled.NO_COLLISION or ce == unreal.CollisionEnabled.PHYSICS_ONLY: continue
        if c.get_collision_response_to_channel(P) == B and c.get_collision_response_to_channel(WD) != B:
            cnt["%s / %s (%s) prof=%s" % (a.get_class().get_name(), c.get_name(), c.get_class().get_name(), c.get_collision_profile_name())] += 1
for k, v in cnt.most_common(40): print(v, k)
print("TOTAL kinds", len(cnt))
