"""The arms graph (UE5_ABP_Viewmodel) and its LeftArm layer for the mantle [2026-10-17].

  Tools/ue_step.sh Mantle arms_graph api     - AnimGraphService methods
  Tools/ue_step.sh Mantle arms_graph dump    - AnimGraph nodes and links
"""
import unreal

ABP = "/FPSAnimationPack/Animations/UE5/UE5_ABP_Viewmodel"
AGS = unreal.AnimGraphService


def api():
    names = [n for n in dir(AGS) if not n.startswith("_")]
    print("AGS", names)
    bs = [n for n in dir(unreal.BlueprintService) if any(k in n for k in ("node", "connect", "pin", "graph"))]
    print("BS", bs)


def dump():
    print("EXISTS", unreal.EditorAssetLibrary.does_asset_exist(ABP))
    nodes = unreal.BlueprintService.get_nodes_in_graph(ABP, "AnimGraph")
    for n in nodes:
        print("NODE", n.export_text() if hasattr(n, "export_text") else n)
    for c in unreal.BlueprintService.get_connections(ABP, "AnimGraph"):
        print("LINK", c.export_text() if hasattr(c, "export_text") else c)


PACK_SKEL = "/FPSAnimationPack/Meshes/Characters/Mannequin_UE5/Meshes/SK_Mannequin"
IG_SKEL = "/Game/InfimaGames/ArtCore/Mannequin/Meshes/SKEL_IG_Mannequin"


def slots():
    for p in (PACK_SKEL, IG_SKEL):
        sk = unreal.load_asset(p)
        try:
            groups = sk.get_editor_property("slot_groups")
            print("GROUPS", p, [g.export_text() for g in groups])
        except Exception as e:
            print("NO slot_groups", p, e)
    print("MS", [n for n in dir(unreal.AnimMontageService) if "slot" in n.lower() or "group" in n.lower()])
    print("SKEL methods", [n for n in dir(unreal.Skeleton) if "slot" in n.lower()])
    print("SKS", [n for n in dir(getattr(unreal, "SkeletonService", object)) if "slot" in n.lower()])
    print("ADD_SLOT_DOC", unreal.AnimGraphService.add_slot_node.__doc__)
    print("ADD_LAYERED_DOC", unreal.AnimGraphService.add_layered_blend.__doc__)
    print("CONNECT_DOC", unreal.AnimGraphService.connect_anim_nodes.__doc__)
    print("DISCONNECT_DOC", unreal.AnimGraphService.disconnect_anim_node.__doc__)
    print("SAVE_DOC", unreal.AnimGraphService.add_save_cached_pose.__doc__)
    print("USE_DOC", unreal.AnimGraphService.add_use_cached_pose.__doc__)


def _graph():
    return unreal.find_object(None, unreal.load_asset(ABP).get_path_name() + ":AnimGraph")


def _layered_nodes():
    g = _graph()
    out = []
    for i in range(0, 12):
        n = unreal.find_object(g, "AnimGraphNode_LayeredBoneBlend_%d" % i)
        if n:
            out.append(n)
    return out


def inspect():
    print("GRAPH", _graph())
    for n in _layered_nodes():
        inner = n.get_editor_property("node")
        print("LAYERED", n.get_name(), inner.export_text()[:1500])


DEFAULT_SLOT_ID = "1DC28BE348CA3A46F529B28762B4B9E4"   # Slot 'DefaultSlot'
L2C_ID = "BBF9E0D749B9E2515EC7789B3CFD083D"            # Local To Component right after it
CACHE = "ArmsBeforeLeftArm"
LEFT_SLOT = "LeftArm"
LEFT_ROOT = "clavicle_l"


def _slot_nodes():
    g = _graph()
    out = []
    for i in range(0, 12):
        n = unreal.find_object(g, "AnimGraphNode_Slot_%d" % i)
        if n:
            out.append(n)
    return out


def build():
    """DefaultSlot -> Save cached pose; base = cached, LeftArm slot over cached, Layered blend from
    clavicle_l -> the Local To Component that DefaultSlot used to feed. Layered blend settings: configure."""
    for n in _slot_nodes():
        if str(n.get_editor_property("node").get_editor_property("slot_name")) == LEFT_SLOT:
            print("EXISTS: LeftArm slot", n.get_name())
            return
    before = set(x.get_name() for x in _layered_nodes())
    G = "AnimGraph"
    print("DISCONNECT DefaultSlot.Pose", AGS.disconnect_anim_node(ABP, G, DEFAULT_SLOT_ID, "Pose"))
    save = AGS.add_save_cached_pose(ABP, G, CACHE, -3360.0, -480.0); print("ADDED save", save)
    use_a = AGS.add_use_cached_pose(ABP, G, CACHE, -3360.0, -640.0); print("ADDED use_a", use_a)
    use_b = AGS.add_use_cached_pose(ABP, G, CACHE, -3360.0, -800.0); print("ADDED use_b", use_b)
    slot = AGS.add_slot_node(ABP, G, LEFT_SLOT, -3152.0, -800.0); print("ADDED slot", slot)
    lay = AGS.add_layered_blend(ABP, G, -2912.0, -640.0); print("ADDED layered", lay)
    print("LINK slot->save", AGS.connect_anim_nodes(ABP, G, DEFAULT_SLOT_ID, "Pose", save, "Pose"))
    print("LINK use_a->base", AGS.connect_anim_nodes(ABP, G, use_a, "Pose", lay, "BasePose"))
    print("LINK use_b->leftslot", AGS.connect_anim_nodes(ABP, G, use_b, "Pose", slot, "Source"))
    print("LINK leftslot->blend0", AGS.connect_anim_nodes(ABP, G, slot, "Pose", lay, "BlendPoses_0"))
    print("LINK layered->l2c", AGS.connect_anim_nodes(ABP, G, lay, "Pose", L2C_ID, "LocalPose"))
    after = [x.get_name() for x in _layered_nodes() if x.get_name() not in before]
    print("NEW LAYERED OBJECT", after)


def configure():
    """The layered blend that reads the LeftArm slot: left arm only, mesh space rotation."""
    target = None
    for n in _layered_nodes():
        txt = n.get_editor_property("node").export_text()
        if "VB Curves" in txt or "VB ik_hand_gun_r" in txt:
            continue
        target = n
    if not target:
        print("NO NEW LAYERED NODE")
        return
    inner = target.get_editor_property("node")
    txt = inner.export_text()
    if "LayerSetup=(())" not in txt:
        print("UNEXPECTED LayerSetup, not touching:", txt[:300])
        return
    txt = txt.replace("LayerSetup=(())", 'LayerSetup=((BranchFilters=((BoneName="%s",BlendDepth=0))))' % LEFT_ROOT)
    txt = txt.replace("bMeshSpaceRotationBlend=False", "bMeshSpaceRotationBlend=True")
    # Curves only from the arms pose: the vault must not add its own curves on top.
    txt = txt.replace("CurveBlendOption=Override", "CurveBlendOption=UseBasePose")
    inner.import_text(txt)
    target.set_editor_property("node", inner)
    print("MODIFIED", target.get_name(), target.get_editor_property("node").export_text()[:400])


def compile_save():
    import json
    res = unreal.ToolsetRegistry.execute_tool("editor_toolset.toolsets.blueprint.BlueprintTools", "compile_blueprint",
        json.dumps({"blueprint": {"refPath": ABP + ".UE5_ABP_Viewmodel"}}))
    print("COMPILE complete=%s error=%s" % (res.is_complete, res.error))
    ok = unreal.EditorAssetLibrary.save_asset(ABP, only_if_is_dirty=False)
    print("SAVED" if ok else "SAVE FAILED", ABP)


def _left_layer():
    for n in _layered_nodes():
        if 'BoneName="%s"' % LEFT_ROOT in n.get_editor_property("node").export_text():
            return n
    return None


def _set_mesh_space(flag):
    n = _left_layer()
    inner = n.get_editor_property("node")
    inner.set_editor_property("mesh_space_rotation_blend", flag)
    n.set_editor_property("node", inner)
    print("MODIFIED mesh_space_rotation_blend ->", n.get_editor_property("node").get_editor_property("mesh_space_rotation_blend"))
    compile_save()


def local_space():
    _set_mesh_space(False)


def mesh_space():
    _set_mesh_space(True)


def curves_from_slot():
    """The left arm layer passes the LeftArm montage's curves (the pickup's camera nod). Without a
    montage the slot pose IS the base pose, so Override changes nothing then."""
    n = _left_layer()
    inner = n.get_editor_property("node")
    txt = inner.export_text()
    print("BEFORE", "mesh_space", inner.get_editor_property("mesh_space_rotation_blend"),
          "curves", "UseBasePose" if "CurveBlendOption=UseBasePose" in txt else txt[txt.find("CurveBlendOption"):][:40])
    inner.import_text(txt.replace("CurveBlendOption=UseBasePose", "CurveBlendOption=Override"))
    n.set_editor_property("node", inner)
    after = n.get_editor_property("node").export_text()
    print("MODIFIED", after[after.find("CurveBlendOption"):][:40])
    compile_save()


COPY_GUN_ID = "06DEC13B47CA83C586A261926F531F17"   # Copy Bone ik_hand_gun -> VB ik_hand_gun_r, after L2C
SHOULDER_BONE = "clavicle_l"
SHOULDER_CURVE = "PickupShoulderAlpha"


def _modify_nodes():
    g = _graph()
    out = []
    for i in range(0, 16):
        n = unreal.find_object(g, "AnimGraphNode_ModifyBone_%d" % i)
        if n:
            out.append(n)
    return out


def _shoulder_node():
    for n in _modify_nodes():
        inner = n.get_editor_property("node")
        if str(inner.get_editor_property("bone_to_modify").get_editor_property("bone_name")) == SHOULDER_BONE:
            return n
    return None


def shoulder_node():
    """Left shoulder offset for the hand pickup [author, 2026-10-17]: Transform (Modify) Bone on
    clavicle_l between Local To Component and the copies into the VB bones, so the left hand target
    (VB pose_hand_l) moves with it. Alpha = curve PickupShoulderAlpha, carried only by the pickup
    animation: zero everywhere else. Translation in component space, rotation in bone space, both
    added. The values stay zero here; the author sets them on the node."""
    if _shoulder_node():
        print("EXISTS: shoulder node", _shoulder_node().get_name())
        return
    G = "AnimGraph"
    before = set(n.get_name() for n in _modify_nodes())
    node_id = AGS.add_modify_bone_node(ABP, G, SHOULDER_BONE, -3000.0, -330.0)
    print("ADDED modify bone", node_id)
    print("DISCONNECT L2C.ComponentPose", AGS.disconnect_anim_node(ABP, G, L2C_ID, "ComponentPose"))
    print("LINK l2c->modify", AGS.connect_anim_nodes(ABP, G, L2C_ID, "ComponentPose", node_id, "ComponentPose"))
    print("LINK modify->copygun", AGS.connect_anim_nodes(ABP, G, node_id, "Pose", COPY_GUN_ID, "ComponentPose"))
    new = [n for n in _modify_nodes() if n.get_name() not in before]
    print("NEW OBJECT", [n.get_name() for n in new])


def shoulder_configure():
    n = _shoulder_node()
    if not n:
        print("NO shoulder node")
        return
    inner = n.get_editor_property("node")
    inner.set_editor_property("translation_mode", unreal.BoneModificationMode.BMM_ADDITIVE)
    inner.set_editor_property("translation_space", unreal.BoneControlSpace.BCS_COMPONENT_SPACE)
    inner.set_editor_property("rotation_mode", unreal.BoneModificationMode.BMM_ADDITIVE)
    inner.set_editor_property("rotation_space", unreal.BoneControlSpace.BCS_BONE_SPACE)
    inner.set_editor_property("alpha_input_type", unreal.AnimAlphaInputType.CURVE)
    inner.set_editor_property("alpha_curve_name", SHOULDER_CURVE)
    n.set_editor_property("node", inner)
    back = n.get_editor_property("node")
    print("MODIFIED", n.get_name(), "tmode", back.get_editor_property("translation_mode"),
          "tspace", back.get_editor_property("translation_space"), "rmode", back.get_editor_property("rotation_mode"),
          "rspace", back.get_editor_property("rotation_space"), "alpha", back.get_editor_property("alpha_input_type"),
          back.get_editor_property("alpha_curve_name"))
    compile_save()
