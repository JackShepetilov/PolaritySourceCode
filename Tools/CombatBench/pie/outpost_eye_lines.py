import unreal
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pc = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.PlayerController)[0]
pawn = pc.get_controlled_pawn()
cam = pc.player_camera_manager.get_camera_location()
for n in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.ShooterNPC):
    loc = n.get_actor_location()
    h = n.get_component_by_class(unreal.CapsuleComponent).get_scaled_capsule_half_height()
    eye = unreal.Vector(loc.x, loc.y, loc.z + h - 15.0)
    hit = unreal.SystemLibrary.line_trace_single(w, eye, cam, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [n, pawn], unreal.DrawDebugTrace.NONE, True)
    blk = "CLEAR"
    if hit:
        t = hit.to_tuple()
        a = t[9] if len(t) > 9 else None
        d = ((t[4].x - eye.x) ** 2 + (t[4].y - eye.y) ** 2) ** 0.5
        blk = "BLOCKED by {} at {:.0f} cm from him, hit z {:.0f} (eye z {:.0f})".format(a.get_actor_label() if a else "?", d, t[4].z, eye.z)
    c = n.get_controller()
    foc = c.get_focus_actor() if c else None
    print("{} local({:.1f},{:.1f}) z{:.0f} crouched={} speed {:.0f} focus {} movestatus {} | eye->player camera: {}".format(
        n.get_name()[-3:], (loc.x - 12500) / 100, (loc.y - 11000) / 100, loc.z, n.is_crouched, n.get_velocity().length(),
        foc.get_name()[:12] if foc else None, c.get_move_status() if c else None, blk))
