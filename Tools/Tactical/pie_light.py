import unreal
DEV = "/Game/Variant_Shooter/Blueprints/Pickups/Attachments/Tactical/DA_Attach_Tactical_"
KIND = "Laser"
DIST = 700.0
w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
VIS = unreal.TraceTypeQuery.TRACE_TYPE_QUERY1


def floor_at(p):
    hit = unreal.SystemLibrary.line_trace_single(w, p + unreal.Vector(0, 0, 300), p - unreal.Vector(0, 0, 600),
        VIS, False, [], unreal.DrawDebugTrace.NONE, True)
    t = hit.to_tuple()
    return t[0], t[4]  # blocking hit, impact point


pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
wpn = pawn.get_current_weapon()
if not wpn:
    pawn.equip_starting_weapon_animated()
    print("EQUIP called")
else:
    old = wpn.get_attachment_of_type(unreal.WeaponAttachmentType.TACTICAL)
    if not old or old.get_name() != "DA_Attach_Tactical_" + KIND:
        if old:
            wpn.uninstall_attachment_of_type(unreal.WeaponAttachmentType.TACTICAL)
        print("INSTALL", KIND, wpn.install_attachment(unreal.load_object(None, DEV + KIND + ".DA_Attach_Tactical_" + KIND)))
    npcs = [a for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.ShooterNPC)
            if not a.is_dead() and a.get_actor_location().z > -2000 and "Tank" not in a.get_class().get_name()]
    t = npcs[0]
    tl = t.get_actor_location()
    spot = None
    for yaw in range(0, 360, 30):
        d = unreal.MathLibrary.get_forward_vector(unreal.Rotator(roll=0, pitch=0, yaw=yaw))
        cand = tl + d * DIST
        ok, imp = floor_at(cand)
        if ok:
            spot = imp + unreal.Vector(0, 0, 100)
            break
    print("TARGET", t.get_name(), tl, "spot", spot)
    if spot:
        pawn.set_actor_location(spot, False, True)
        head = t.mesh.get_socket_location("head")
        eye = spot + unreal.Vector(0, 0, 60)
        pawn.get_controller().set_control_rotation(unreal.MathLibrary.find_look_at_rotation(eye, head))
        pawn.do_start_ads()
