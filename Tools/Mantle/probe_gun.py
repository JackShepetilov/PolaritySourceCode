"""Does the gun stay in the right hand while the mantle montage plays? [2026-10-17]

PIE only. Freezes a montage at a time, then (next call) reads where the gun is relative to hand_r.
The same for the weapon's own reload, as the reference: there the gun is known to stay in the hand.

  Tools/ue_step.sh Mantle probe_gun start     - StartPIE (then wait for the pawn)
  Tools/ue_step.sh Mantle probe_gun setup     - ignore AI, equip, print how the gun is attached
  Tools/ue_step.sh Mantle probe_gun step      - read the frozen pose, freeze the next one (repeat)
  Tools/ue_step.sh Mantle probe_gun stop      - StopPIE
"""
import json
import unreal

MONTAGE = "/Game/Variant_Shooter/Anims/Mantle/AM_Mantle_LeftArm.AM_Mantle_LeftArm"
TIMES = [0.0, 0.15, 0.3, 0.45, 0.6, 0.75]
BONES = ["hand_r", "ik_hand_gun", "VB ik_hand_gun", "ik_hand_root", "hand_l", "head"]


def _world():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def _fp(pawn):
    for c in pawn.get_components_by_class(unreal.SkeletalMeshComponent):
        if c.get_name() == "First Person Mesh":
            return c
    return None


def start():
    res = unreal.ToolsetRegistry.execute_tool("EditorToolset.EditorAppToolset", "StartPIE",
        json.dumps({"options": {"bSimulate": False, "playMode": "PlayMode_InEditorFloating", "warmupSeconds": 3}}))
    print("STARTPIE complete=%s error=%s" % (res.is_complete, res.error))


def stop():
    res = unreal.ToolsetRegistry.execute_tool("EditorToolset.EditorAppToolset", "StopPIE", "{}")
    print("STOPPIE complete=%s error=%s" % (res.is_complete, res.error))


def setup():
    w = _world()
    if not w:
        print("NO PIE")
        return
    unreal.SystemLibrary.execute_console_command(w, "polarity.ai.IgnorePlayers 1")
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    pawn.equip_starting_weapon_animated()
    unreal.probe_gun_queue = None
    print("PAWN", pawn.get_class().get_name())


def _queue(weapon):
    q = [("mantle", MONTAGE, t) for t in TIMES]
    rel = weapon.get_reload_montage() if weapon else None
    if rel:
        n = rel.get_play_length()
        q += [("reload", rel.get_path_name(), n * f) for f in (0.4,)]
    q.append(("none", None, 0.0))
    return q


def _report(fp, weapon, label):
    hr = fp.get_socket_transform("hand_r", unreal.RelativeTransformSpace.RTS_WORLD)
    line = []
    for b in BONES:
        if fp.does_socket_exist(b):
            p = fp.get_socket_location(b)
            r = unreal.MathLibrary.inverse_transform_location(hr, p)
            line.append("%s(%.1f,%.1f,%.1f)" % (b, r.x, r.y, r.z))
        else:
            line.append(b + "(missing)")
    wm = weapon.get_first_person_mesh() if weapon else None
    if wm:
        rel = unreal.MathLibrary.make_relative_transform(wm.get_world_transform(), hr)
        loc, rot = rel.translation, rel.rotation.rotator()
        print("%s GUNvsHAND_R loc(%.1f,%.1f,%.1f) rot(p%.1f y%.1f r%.1f)" % (label, loc.x, loc.y, loc.z, rot.pitch, rot.yaw, rot.roll))
    print("%s BONESvsHAND_R %s" % (label, " ".join(line)))
    cam = unreal.GameplayStatics.get_player_camera_manager(fp.get_world(), 0)
    cl, cr = cam.get_camera_location(), cam.get_camera_rotation()
    q = fp.get_socket_location("hand_l") - cl
    x, y, z = q.dot(cr.get_forward_vector()), q.dot(cr.get_right_vector()), q.dot(cr.get_up_vector())
    print("%s HAND_L view fwd=%.0f right=%.0f up=%.0f -> yaw %.0f pitch %.0f" % (label, x, y, z,
          unreal.MathLibrary.radians_to_degrees(unreal.MathLibrary.atan2(y, x)),
          unreal.MathLibrary.radians_to_degrees(unreal.MathLibrary.atan2(z, x))))


def step():
    w = _world()
    if not w:
        print("NO PIE")
        return
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    fp = _fp(pawn)
    anim = fp.get_anim_instance()
    weapon = pawn.get_current_weapon()
    q = getattr(unreal, "probe_gun_queue", None)
    if not q and getattr(unreal, "probe_gun_cur", None) is None:
        wm = weapon.get_first_person_mesh() if weapon else None
        print("WEAPON", weapon.get_class().get_name() if weapon else None,
              "attach", wm.get_attach_socket_name() if wm else None,
              "parent", wm.get_attach_parent().get_name() if wm else None,
              "animclass", anim.get_class().get_name())
        q = _queue(weapon)
        unreal.probe_gun_queue = q
        unreal.probe_gun_cur = None
    cur = getattr(unreal, "probe_gun_cur", None)
    if cur:
        playing = anim.get_current_active_montage()
        label = "%s t=%.2f active=%s" % (cur[0], cur[2], playing.get_name() if playing else None)
        _report(fp, weapon, label)
    if not q:
        print("QUEUE DONE")
        unreal.probe_gun_cur = None
        return
    nxt = q.pop(0)
    unreal.probe_gun_cur = nxt
    anim.montage_stop(0.0, None)
    if nxt[1]:
        m = unreal.load_object(None, nxt[1])
        anim.montage_play(m, 1.0)
        anim.montage_set_position(m, nxt[2])
        anim.montage_pause(m)
    print("FROZE", nxt[0], "%.2f" % nxt[2], "left", len(q))


def _pawn():
    w = _world()
    return (w, unreal.GameplayStatics.get_player_pawn(w, 0)) if w else (None, None)


def _status(pawn, tag):
    fp = _fp(pawn)
    anim = fp.get_anim_instance()
    wpn = pawn.get_current_weapon()
    m = anim.get_current_active_montage()
    print("%s reloading=%s bullets=%d active=%s" % (tag, wpn.is_reloading(), getattr(wpn, "get_bullet_count", lambda: -1)(), m.get_name() if m else None))


def fire():
    w, pawn = _pawn()
    if not pawn:
        print("NO PIE"); return
    pawn.do_start_firing()
    pawn.do_stop_firing()
    _status(pawn, "FIRED")


def reload():
    w, pawn = _pawn()
    if not pawn:
        print("NO PIE"); return
    pawn.do_reload()
    _status(pawn, "RELOAD")


def mantle_start():
    w, pawn = _pawn()
    if not pawn:
        print("NO PIE"); return
    _status(pawn, "BEFORE_MANTLE")
    pawn.call_method("OnMantleStarted_Handler")
    _status(pawn, "MANTLE_STARTED")


def mantle_end():
    w, pawn = _pawn()
    if not pawn:
        print("NO PIE"); return
    fp = _fp(pawn)
    _report(fp, pawn.get_current_weapon(), "DURING_MANTLE")
    _status(pawn, "BEFORE_END")
    pawn.call_method("OnMantleEnded_Handler")
    _status(pawn, "MANTLE_ENDED")


def after():
    w, pawn = _pawn()
    if not pawn:
        print("NO PIE"); return
    _status(pawn, "AFTER")
