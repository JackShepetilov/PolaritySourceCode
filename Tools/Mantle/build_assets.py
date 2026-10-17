"""Mantle animation assets [2026-10-17].

The first person arms play the mantle on DefaultSlot. The pack's graph (UE5_ABP_Viewmodel) has no
LeftArm slot and does not read LeftHandIKAlpha; DefaultSlot is where it plays the grenade and the
reloads, and there the left hand leaves the gun freely. The vault is an additive (mesh space rotation
offset on Infima's rifle idle), so it moves the arms from the pose they are in instead of snapping. Infima's A_RAR_FP_PCH_AssaultRifle_Vault_Medium is the only vault whose left hand leaves the
gun (0.15 .. 0.7 s, measured); Vault_Low keeps both hands on it, so both mantle kinds use Medium.

Run one step at a time: Tools/ue_step.sh Mantle build_assets <step>
  compat   - lets our skeleton use Infima's SKEL_IG_Mannequin animations in the editor
  montage  - builds AM_Mantle_LeftArm on Infima's skeleton, slot DefaultSlot
  slot     - sets the slot of an existing AM_Mantle_LeftArm to SLOT
  assign   - puts it into BP_ShooterCharacter1's MantleLow/HighMontageFP, compiles, saves
  check    - reads everything back
"""
import unreal

EAL = unreal.EditorAssetLibrary
MS = unreal.AnimMontageService

OUR_SKELETON = "/Game/Characters/Mannequins/Meshes/SK_Mannequin"
INFIMA_SKELETON = "/Game/InfimaGames/ArtCore/Mannequin/Meshes/SKEL_IG_Mannequin"
VAULT = "/Game/InfimaGames/RealisticAssaultRifle/Art/Character/Animations/Movement/A_RAR_FP_PCH_AssaultRifle_Vault_Medium"
OUT = "/Game/Variant_Shooter/Anims/Mantle"
MONTAGE_NAME = "AM_Mantle_LeftArm"
MONTAGE = OUT + "/" + MONTAGE_NAME
PLAYER_BP = "/Game/Variant_Shooter/Blueprints/BP_ShooterCharacter1"

SLOT = "LeftArm"
# The hand is back on the gun by 0.77 s and the rest is idle: cut there, so a montage fitted to the
# mantle's 0.3 .. 0.55 s spends that time on the hand going to the ledge and back.
SEGMENT_END = 0.85
BLEND_IN = 0.08
BLEND_OUT = 0.15


def _save(path):
    ok = EAL.save_asset(path, only_if_is_dirty=False)
    print("SAVED:" if ok else "SAVE FAILED:", path)


def compat():
    sk = unreal.load_asset(OUR_SKELETON)
    have = [s.get_path_name() for s in sk.get_editor_property("compatible_skeletons")]
    target = INFIMA_SKELETON + ".SKEL_IG_Mannequin"
    if target in have:
        print("EXISTS: compatible", target)
        return
    sk.add_compatible_skeleton(unreal.load_asset(INFIMA_SKELETON))
    print("MODIFIED: compatible skeletons", [s.get_path_name() for s in sk.get_editor_property("compatible_skeletons")])
    _save(OUR_SKELETON)


def montage():
    # On Infima's skeleton, not ours: AnimMontageService refuses a segment from another skeleton
    # ("Animation skeleton mismatch") whatever CompatibleSkeletons says. At runtime that does not
    # matter: Montage_Play does not check skeletons, the bones are matched by name.
    if EAL.does_asset_exist(MONTAGE):
        segs = MS.list_anim_segments(MONTAGE, 0)
        if len(segs) > 0:
            print("EXISTS:", MONTAGE, "segments", [x.export_text() for x in segs])
            return
        # Only the empty one this script made before it knew the above. Nothing references it.
        assert not EAL.find_package_referencers_for_asset(MONTAGE), "empty montage is referenced, stop"
        EAL.delete_asset(MONTAGE)
        print("DELETED: empty", MONTAGE)
    created = MS.create_montage_from_animation(VAULT, OUT, MONTAGE_NAME)
    print("CREATED:", created)
    MS.set_slot_name(MONTAGE, 0, SLOT)
    MS.set_segment_end_position(MONTAGE, 0, 0, SEGMENT_END)
    MS.set_blend_in(MONTAGE, BLEND_IN)
    MS.set_blend_out(MONTAGE, BLEND_OUT)
    print("  segments:", [x.export_text() for x in MS.list_anim_segments(MONTAGE, 0)])
    _save(MONTAGE)


def slot():
    before = [str(t.get_editor_property("slot_name")) for t in unreal.load_asset(MONTAGE).get_editor_property("slot_anim_tracks")]
    MS.set_slot_name(MONTAGE, 0, SLOT)
    after = [str(t.get_editor_property("slot_name")) for t in unreal.load_asset(MONTAGE).get_editor_property("slot_anim_tracks")]
    print("MODIFIED: slot", before, "->", after)
    _save(MONTAGE)


def assign():
    import json
    m = unreal.load_asset(MONTAGE)
    cls = EAL.load_blueprint_class(PLAYER_BP)
    cdo = unreal.get_default_object(cls)
    cdo.set_editor_property("mantle_low_montage_fp", m)
    cdo.set_editor_property("mantle_high_montage_fp", m)
    print("MODIFIED: MantleLow/HighMontageFP =", m.get_path_name())
    res = unreal.ToolsetRegistry.execute_tool(
        "editor_toolset.toolsets.blueprint.BlueprintTools", "compile_blueprint",
        json.dumps({"blueprint": {"refPath": PLAYER_BP + "." + PLAYER_BP.split("/")[-1]}}))
    print("COMPILE complete=%s error=%s" % (res.is_complete, res.error))
    _save(PLAYER_BP)


def check():
    sk = unreal.load_asset(OUR_SKELETON)
    print("COMPAT", [s.get_path_name() for s in sk.get_editor_property("compatible_skeletons")])
    m = unreal.load_asset(MONTAGE)
    print("MONTAGE", m, "len %.2f" % m.get_play_length(),
          "slots", [str(t.get_editor_property("slot_name")) for t in m.get_editor_property("slot_anim_tracks")],
          "skel", m.get_editor_property("skeleton").get_path_name())
    bp = unreal.load_asset(PLAYER_BP)
    print("BP status", bp.get_editor_property("status"))
    cdo = unreal.get_default_object(EAL.load_blueprint_class(PLAYER_BP))
    print("CDO low", cdo.get_editor_property("mantle_low_montage_fp"), "high", cdo.get_editor_property("mantle_high_montage_fp"),
          "fit", cdo.get_editor_property("fit_mantle_montages_to_duration"))


# ==================== PIE test helpers ====================

def _ledge_candidates(world, origin, max_dist=2500.0):
    """Walls near origin whose top is 60..190 cm above the floor in front of them, with room on top.
    Read-only traces; nothing is placed."""
    SL = unreal.SystemLibrary
    out = []
    for yaw in range(0, 360, 10):
        rad = unreal.MathLibrary.degrees_to_radians(yaw)
        d = unreal.Vector(unreal.MathLibrary.cos(rad), unreal.MathLibrary.sin(rad), 0.0)
        # floor under origin
        fh = SL.line_trace_single(world, origin + unreal.Vector(0, 0, 100), origin - unreal.Vector(0, 0, 500),
                                  unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True)
        if not fh:
            continue
        floor_z = fh.to_tuple()[5].z  # to_tuple: 0 blocking, 1 initial overlap, 4 location, 5 impact point, 7 impact normal
        start = unreal.Vector(origin.x, origin.y, floor_z + 40.0)
        hit = SL.line_trace_single(world, start, start + d * max_dist, unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
                                   False, [], unreal.DrawDebugTrace.NONE, True)
        if not hit:
            continue
        t = hit.to_tuple()
        imp, nrm = t[5], t[7]
        if abs(nrm.z) > 0.3:
            continue
        inward = unreal.Vector(-nrm.x, -nrm.y, 0.0)
        probe = imp + inward * 20.0
        top = SL.line_trace_single(world, unreal.Vector(probe.x, probe.y, floor_z + 210), unreal.Vector(probe.x, probe.y, floor_z + 55),
                                   unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [], unreal.DrawDebugTrace.NONE, True)
        if not top:
            continue
        tt = top.to_tuple()
        if tt[1]:  # started inside: wall too tall
            continue
        h = tt[5].z - floor_z
        out.append((yaw, round((imp - start).length()), round(h), imp, nrm, floor_z))
    return out


def find_ledges():
    ues = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    w = ues.get_game_world() or ues.get_editor_world()
    print("WORLD", w.get_name(), "PIE" if ues.get_game_world() else "EDITOR")
    starts = unreal.GameplayStatics.get_all_actors_of_class(w, unreal.PlayerStart)
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0) if ues.get_game_world() else None
    origin = pawn.get_actor_location() if pawn else (starts[0].get_actor_location() if starts else unreal.Vector(0, 0, 0))
    print("ORIGIN", origin)
    for c in _ledge_candidates(w, origin):
        print("LEDGE yaw=%d dist=%d height=%d at=(%.0f,%.0f,%.0f)" % (c[0], c[1], c[2], c[3].x, c[3].y, c[3].z))


def start_pie():
    import json
    res = unreal.ToolsetRegistry.execute_tool("EditorToolset.EditorAppToolset", "StartPIE",
        json.dumps({"options": {"bSimulate": False, "playMode": "PlayMode_InEditorFloating", "warmupSeconds": 3}}))
    print("STARTPIE complete=%s error=%s" % (res.is_complete, res.error))


def setup():
    w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if not w:
        print("NO PIE")
        return
    unreal.SystemLibrary.execute_console_command(w, "polarity.ai.IgnorePlayers 1")
    unreal.SystemLibrary.execute_console_command(w, "polarity.mantle.debug 1")
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    print("PAWN", pawn, pawn.get_actor_location() if pawn else None)
    if pawn:
        pawn.equip_starting_weapon_animated()
        print("EQUIPPED")


def place():
    """Puts the pawn 70 cm in front of the nearest ledge found by _ledge_candidates, facing it."""
    w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if not w:
        print("NO PIE")
        return
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    cands = sorted(_ledge_candidates(w, pawn.get_actor_location()), key=lambda c: c[1])
    for c in cands[:8]:
        print("LEDGE yaw=%d dist=%d height=%d" % (c[0], c[1], c[2]))
    if not cands:
        print("NO LEDGE")
        return
    yaw, dist, h, imp, nrm, floor_z = cands[0]
    half = pawn.capsule_component.get_scaled_capsule_half_height()
    loc = unreal.Vector(imp.x + nrm.x * 70.0, imp.y + nrm.y * 70.0, floor_z + half + 2.0)
    pawn.teleport(loc, unreal.Rotator(roll=0, pitch=0, yaw=yaw))
    pc = pawn.get_controller()
    pc.set_control_rotation(unreal.Rotator(roll=0, pitch=0, yaw=yaw))
    print("PLACED at", loc, "facing yaw", yaw, "ledge height", h)


def state():
    w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if not w:
        print("NO PIE")
        return
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    mv = pawn.get_movement_component()
    print("STATE loc", pawn.get_actor_location(), "mode", mv.movement_mode, "mantling", mv.is_mantling(), "kind", mv.get_mantle_kind(),
          "vel %.0f" % mv.velocity.length())


def _fp_mesh(pawn):
    for c in pawn.get_components_by_class(unreal.SkeletalMeshComponent):
        if c.get_name() == "First Person Mesh":
            return c
    return None


def probe_montage():
    """PIE: slow time down, play the mantle montage on the arms, free the left hand, then report
    whether it plays and where the left hand is relative to the view (degrees off centre)."""
    w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if not w:
        print("NO PIE")
        return
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    fp = _fp_mesh(pawn)
    anim = fp.get_anim_instance()
    m = unreal.load_object(None, MONTAGE + "." + MONTAGE_NAME)
    unreal.SystemLibrary.execute_console_command(w, "polarity.debug.timescale 0.05")
    try:
        pawn.set_left_hand_ik_alpha(0.0)
    except Exception as e:
        print('NO IK SETTER', e)
    d = anim.montage_play(m, 1.0)
    print("PLAY duration", d, "animclass", anim.get_class().get_name(), "playing", anim.montage_is_playing(m))


def probe_hand():
    w = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    if not w:
        print("NO PIE")
        return
    pawn = unreal.GameplayStatics.get_player_pawn(w, 0)
    fp = _fp_mesh(pawn)
    anim = fp.get_anim_instance()
    m = unreal.load_object(None, MONTAGE + "." + MONTAGE_NAME)
    cam = unreal.GameplayStatics.get_player_camera_manager(w, 0)
    cl, cr = cam.get_camera_location(), cam.get_camera_rotation()
    fwd, right, up = cr.get_forward_vector(), cr.get_right_vector(), cr.get_up_vector()
    for b in ("hand_l", "hand_r"):
        p = fp.get_socket_location(b) - cl
        x, y, z = p.dot(fwd), p.dot(right), p.dot(up)
        yaw = unreal.MathLibrary.radians_to_degrees(unreal.MathLibrary.atan2(y, x))
        pit = unreal.MathLibrary.radians_to_degrees(unreal.MathLibrary.atan2(z, x))
        print("HAND %s fwd=%.0f right=%.0f up=%.0f -> yaw %.0f pitch %.0f" % (b, x, y, z, yaw, pit))
    print("MONTAGE playing", anim.montage_is_playing(m), "pos %.2f" % anim.montage_get_position(m), "fov", cam.get_fov_angle(),
          "active", anim.get_current_active_montage())
