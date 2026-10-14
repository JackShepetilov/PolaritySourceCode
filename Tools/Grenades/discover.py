import unreal


def montage_api():
    for name in dir(unreal.AnimMontageService):
        if not name.startswith("_"):
            fn = getattr(unreal.AnimMontageService, name)
            print(name, "::", (fn.__doc__ or "").split("\n")[0][:220])


def root_bone():
    # Is the travel in the root bone (root motion can carry it) or in the pelvis only?
    seq = unreal.load_asset("/Game/Variant_Shooter/Anims/Knockdown/Frank_Damage_KnockDown_02")
    opts = unreal.AnimPoseEvaluationOptions()
    for t in (0.0, seq.get_play_length() - 0.02):
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(seq, t, opts)
        names = unreal.AnimPoseExtensions.get_bone_names(pose)
        root = unreal.AnimPoseExtensions.get_bone_pose(pose, names[0], unreal.AnimPoseSpaces.WORLD)
        print("t=%.2f root=%s at %s" % (t, names[0], root.translation))
    old = unreal.load_asset("/Game/Characters/Mannequins/Anims/Rifle/Knockdown/AM_Knockdown_Front")
    print("existing knockdown slot:", [str(t.get_editor_property("slot_name")) for t in old.get_editor_property("slot_anim_tracks")])
    print("existing knockdown blend in/out:", old.get_editor_property("blend_in").get_editor_property("blend_time"),
          old.get_editor_property("blend_out").get_editor_property("blend_time"))
    rm = unreal.load_asset("/Game/Characters/Mannequins/Anims/Rifle/Knockdown/Frank_Damage_Stun05_start_RM")
    for p in ("enable_root_motion", "root_motion_root_lock", "force_root_lock"):
        print("RM copy", p, rm.get_editor_property(p))


def stun_roots():
    opts = unreal.AnimPoseEvaluationOptions()
    for n in ("01", "02", "04", "05"):
        for part in ("start", "loop", "end"):
            seq = unreal.load_asset("/Game/Variant_Shooter/Anims/Stuns/Frank_Damage_Stun%s_%s" % (n, part))
            out = []
            for t in (0.0, seq.get_play_length() - 0.02):
                pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(seq, t, opts)
                root = unreal.AnimPoseExtensions.get_bone_pose(pose, "root", unreal.AnimPoseSpaces.WORLD)
                r = root.rotation.rotator()
                out.append("loc=(%.0f,%.0f) yaw=%.0f" % (root.translation.x, root.translation.y, r.yaw))
            print("Stun%s_%s" % (n, part), " -> ".join(out))
