import unreal
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pc = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.PlayerController)[0]
pawn = pc.get_controlled_pawn()
cam = pc.player_camera_manager.get_camera_location()
for n in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.ShooterNPC):
    loc = n.get_actor_location()
    mz = None
    for a in n.get_attached_actors():
        if isinstance(a, unreal.ShooterWeapon):
            mz = a.get_muzzle_world_location()
    res = []
    for name, src in (("muzzle", mz), ("head", unreal.Vector(loc.x, loc.y, loc.z + 70.0))):
        hit = unreal.SystemLibrary.line_trace_single(w, src, cam, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [n, pawn], unreal.DrawDebugTrace.NONE, True)
        if hit:
            t = hit.to_tuple(); a = t[9]
            res.append("{} BLOCKED by {} at z {:.0f}".format(name, a.get_actor_label() if a else "?", t[4].z))
        else:
            res.append(name + " CLEAR")
    print("sniper x {:.0f} (parapet x 6600-6620) crouched={} | muzzle x {:.0f} z {:.0f} (parapet top z 415) | dist to player {:.0f} m | {}".format(
        loc.x, n.is_crouched, mz.x, mz.z, ((loc.x - cam.x) ** 2 + (loc.y - cam.y) ** 2) ** 0.5 / 100, " ; ".join(res)))
