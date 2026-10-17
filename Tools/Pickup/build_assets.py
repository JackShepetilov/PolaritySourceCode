"""Hand pickup animation assets [2026-10-17]. Plan: Docs/Pickup_Hand_Plan_2026-10-17.md

The pack's UE5 pickup (UE5_A_FP_PickUp_Item) has no camera curves, and without the camera nodding
down the hand leaves the frame. The UE4 version (A_FP_PickUp_Item) carries CameraPitch/Yaw/Roll,
baked by the pack from A_Cam_PickUp_Item. The project copy gets those curves; the pack asset stays.

Run one step at a time: Tools/ue_step.sh Pickup build_assets <step>
  inspect  - lengths, curves of both pack animations
  copy     - project copy of the UE5 animation with the UE4 camera curves
  grab     - when the left hand is lowest (the grab moment), on the pack arms mesh
  montage  - AM_HandPickup on slot LeftArm
  assign   - BP_ShooterCharacter1: HandPickupMontageFP, HandPickupGrabTime; compile, save
  check    - reads everything back
"""
import unreal

EAL = unreal.EditorAssetLibrary
AL = unreal.AnimationLibrary
MS = unreal.AnimMontageService

UE5_ANIM = "/FPSAnimationPack/Animations/UE5/General/UE5_A_FP_PickUp_Item"
UE4_ANIM = "/FPSAnimationPack/Animations/UE4/General/Character/A_FP_PickUp_Item"
ARMS_MESH = "/FPSAnimationPack/Meshes/Characters/Mannequin_UE5/Meshes/SK_Mannequin_Arms"
PACK_SKELETON = "/FPSAnimationPack/Meshes/Characters/Mannequin_UE5/Meshes/SK_Mannequin"
OUT = "/Game/Variant_Shooter/Anims/Pickup"
ANIM_NAME = "A_FP_PickUp_Item_Cam"
ANIM = OUT + "/" + ANIM_NAME
MONTAGE_NAME = "AM_HandPickup"
MONTAGE = OUT + "/" + MONTAGE_NAME
PLAYER_BP = "/Game/Variant_Shooter/Blueprints/BP_ShooterCharacter1"
CAMERA_CURVES = ("CameraPitch", "CameraYaw", "CameraRoll")
SLOT = "LeftArm"
SEGMENT_END = 1.1
# The left hand is lowest here (step grab, measured on SK_Mannequin_Arms): the item starts flying in.
GRAB_TIME = 0.6
BLEND_IN = 0.12
BLEND_OUT = 0.2


def _save(path):
    a = unreal.load_asset(path)
    if a:
        a.modify()
    ok = EAL.save_asset(path, only_if_is_dirty=False)
    print("SAVED:" if ok else "SAVE FAILED:", path)


def _curves(anim):
    return [str(n) for n in AL.get_animation_curve_names(anim, unreal.RawCurveTrackTypes.RCT_FLOAT)]


def inspect():
    for p in (UE5_ANIM, UE4_ANIM):
        a = unreal.load_asset(p)
        print("ANIM", p, "len %.3f" % a.get_play_length(), "skel", a.get_skeleton().get_path_name(), "curves", _curves(a))
    a4 = unreal.load_asset(UE4_ANIM)
    for c in CAMERA_CURVES:
        if c in _curves(a4):
            t, v = AL.get_float_keys(a4, c)
            print("KEYS", c, len(t), "range %.2f..%.2f" % (min(v), max(v)) if v else "")


def copy():
    if EAL.does_asset_exist(ANIM):
        print("EXISTS:", ANIM)
    else:
        EAL.duplicate_asset(UE5_ANIM, ANIM)
        print("CREATED:", ANIM)
    dst = unreal.load_asset(ANIM)
    src = unreal.load_asset(UE4_ANIM)
    # Both are the same take retargeted; stretch the keys if the lengths differ at all.
    scale = dst.get_play_length() / max(src.get_play_length(), 0.0001)
    print("LENGTHS ue5 %.3f ue4 %.3f scale %.4f" % (dst.get_play_length(), src.get_play_length(), scale))
    have = _curves(dst)
    for c in CAMERA_CURVES:
        if c not in _curves(src):
            print("MISSING in UE4:", c)
            continue
        t, v = AL.get_float_keys(src, c)
        if c in have:
            AL.remove_curve(dst, c, False)
        AL.add_curve(dst, c, unreal.RawCurveTrackTypes.RCT_FLOAT, False)
        AL.add_float_curve_keys(dst, c, [x * scale for x in t], list(v))
        print("ADDED: curve", c, len(t), "keys")
    _save(ANIM)


def grab():
    a = unreal.load_asset(ANIM) or unreal.load_asset(UE5_ANIM)
    mesh = unreal.load_asset(ARMS_MESH)
    opts = unreal.AnimPoseEvaluationOptions()
    opts.set_editor_property("optional_skeletal_mesh", mesh)
    n = 40
    length = a.get_play_length()
    rows = []
    for i in range(n + 1):
        t = length * i / n
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(a, t, opts)
        hl = unreal.AnimPoseExtensions.get_bone_pose(pose, "hand_l", unreal.AnimPoseSpaces.WORLD).translation
        hr = unreal.AnimPoseExtensions.get_bone_pose(pose, "hand_r", unreal.AnimPoseSpaces.WORLD).translation
        rows.append((t, hl, hr))
    lowest = min(rows, key=lambda r: r[1].z)
    for t, hl, hr in rows[::4]:
        print("T %.2f hand_l (%.0f,%.0f,%.0f) hand_r (%.0f,%.0f,%.0f)" % (t, hl.x, hl.y, hl.z, hr.x, hr.y, hr.z))
    print("LOWEST hand_l at %.3f s, z %.1f; length %.3f" % (lowest[0], lowest[1].z, length))


def montage():
    if EAL.does_asset_exist(MONTAGE):
        print("EXISTS:", MONTAGE)
    else:
        print("CREATED:", MS.create_montage_from_animation(ANIM, OUT, MONTAGE_NAME))
    MS.set_slot_name(MONTAGE, 0, SLOT)
    # The hand is back on its rest pose by 1.05 s and the rest of the 1.5 s is idle: cut there, so
    # the pickup (and a holster stretched to it) is not 0.4 s longer than the move.
    MS.set_segment_end_position(MONTAGE, 0, 0, SEGMENT_END)
    print("  segments:", [x.export_text() for x in MS.list_anim_segments(MONTAGE, 0)])
    MS.set_blend_in(MONTAGE, BLEND_IN)
    MS.set_blend_out(MONTAGE, BLEND_OUT)
    m = unreal.load_asset(MONTAGE)
    print("MODIFIED: slot", [str(t.get_editor_property("slot_name")) for t in m.get_editor_property("slot_anim_tracks")],
          "group", m.get_group_name(), "len %.3f" % m.get_play_length())
    _save(MONTAGE)


def assign(grab_time=GRAB_TIME):
    import json
    cls = EAL.load_blueprint_class(PLAYER_BP)
    cdo = unreal.get_default_object(cls)
    cdo.set_editor_property("hand_pickup_montage_fp", unreal.load_asset(MONTAGE))
    if grab_time is not None:
        cdo.set_editor_property("hand_pickup_grab_time", float(grab_time))
    print("MODIFIED:", PLAYER_BP, cdo.get_editor_property("hand_pickup_montage_fp"), cdo.get_editor_property("hand_pickup_grab_time"))
    res = unreal.ToolsetRegistry.execute_tool("editor_toolset.toolsets.blueprint.BlueprintTools", "compile_blueprint",
        json.dumps({"blueprint": {"refPath": PLAYER_BP + ".BP_ShooterCharacter1"}}))
    print("COMPILE complete=%s error=%s" % (res.is_complete, res.error))
    _save(PLAYER_BP)


def check():
    a = unreal.load_asset(ANIM)
    print("ANIM", a, "curves", _curves(a) if a else None)
    m = unreal.load_asset(MONTAGE)
    if m:
        print("MONTAGE slot", [str(t.get_editor_property("slot_name")) for t in m.get_editor_property("slot_anim_tracks")],
              "group", m.get_group_name(), "len %.3f" % m.get_play_length())
    cdo = unreal.get_default_object(EAL.load_blueprint_class(PLAYER_BP))
    print("BP montage", cdo.get_editor_property("hand_pickup_montage_fp"), "grab", cdo.get_editor_property("hand_pickup_grab_time"),
          "strip", [c.get_editor_property("strip_time") for c in cdo.get_components_by_class(unreal.FieldCareComponent)])


def strip_time():
    """UFieldCareComponent::StripTime = the grab moment: the armour comes off when the hand closes."""
    import json
    cdo = unreal.get_default_object(EAL.load_blueprint_class(PLAYER_BP))
    comps = cdo.get_components_by_class(unreal.FieldCareComponent)
    for c in comps:
        c.set_editor_property("strip_time", GRAB_TIME)
    print("MODIFIED: strip_time", [c.get_editor_property("strip_time") for c in comps])
    res = unreal.ToolsetRegistry.execute_tool("editor_toolset.toolsets.blueprint.BlueprintTools", "compile_blueprint",
        json.dumps({"blueprint": {"refPath": PLAYER_BP + ".BP_ShooterCharacter1"}}))
    print("COMPILE complete=%s error=%s" % (res.is_complete, res.error))
    _save(PLAYER_BP)


SHOULDER_CURVE = "PickupShoulderAlpha"
# Seconds and weights: in while the hand goes down, held at the floor, out as it comes back. The
# author tunes the shape in the curve editor of A_FP_PickUp_Item_Cam.
SHOULDER_KEYS = ((0.0, 0.0), (0.35, 1.0), (0.8, 1.0), (1.05, 0.0))


def shoulder_curve():
    """The alpha of the left shoulder offset in UE5_ABP_Viewmodel (Mantle/arms_graph shoulder_node)."""
    a = unreal.load_asset(ANIM)
    if SHOULDER_CURVE in _curves(a):
        AL.remove_curve(a, SHOULDER_CURVE, False)
    AL.add_curve(a, SHOULDER_CURVE, unreal.RawCurveTrackTypes.RCT_FLOAT, False)
    AL.add_float_curve_keys(a, SHOULDER_CURVE, [k[0] for k in SHOULDER_KEYS], [k[1] for k in SHOULDER_KEYS])
    print("ADDED: curve", SHOULDER_CURVE, SHOULDER_KEYS)
    _save(ANIM)
