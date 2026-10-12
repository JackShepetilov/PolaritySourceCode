"""Падение от попадания в голову: монтажи из Frank_Slash_Pack, совместимость скелетов (2026-10-12).
Стан 4 играется, когда урон пришёл сзади, стан 5 когда спереди [автор].

    probe()            что лежит в папке стана, на каком скелете, какой рут моушн
    measure()          куда и на сколько уезжает корень и как далеко от него ложится тело
    step_compat()      скелет пака в CompatibleSkeletons скелета бойца
    step_montages()    AM_Knockdown_Back (стан 4) и AM_Knockdown_Front (стан 5): start, бленд в end
    step_enemy()       монтажи в компонент Helmet у BP_ShooterNPC

Запуск: Tools/dp_step.sh setup_knockdown <функция>.
"""

import json
import unreal

EAL = unreal.EditorAssetLibrary
STUN = "/Game/Frank_Slash_Pack/Frank_Damages/Animations/04_Stun"
NPC = "/Game/Variant_Shooter/Blueprints/AI/BPs/BP_ShooterNPC"
OUT = "/Game/Characters/Mannequins/Anims/Rifle/Knockdown"
BT = "editor_toolset.toolsets.blueprint.BlueprintTools"


def log(msg):
    print("[KNOCKDOWN] " + str(msg))


def _npc_mesh():
    cdo = unreal.get_default_object(EAL.load_blueprint_class(NPC))
    return cdo.mesh.get_editor_property("skeletal_mesh_asset")


def probe():
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    for a in reg.get_assets_by_path(STUN, recursive=True):
        obj = a.get_asset()
        line = "{} [{}]".format(a.package_name, a.asset_class_path.asset_name)
        if isinstance(obj, unreal.AnimSequence):
            sk = obj.get_editor_property("skeleton")
            line += " len {:.2f} skel {} rootmotion {} lock {} additive {}".format(
                obj.get_play_length(), sk.get_path_name() if sk else None,
                obj.get_editor_property("enable_root_motion"), obj.get_editor_property("root_motion_root_lock"),
                obj.get_editor_property("additive_anim_type"))
        log(line)
    mesh = _npc_mesh()
    sk = mesh.get_editor_property("skeleton")
    log("npc mesh {} skeleton {}".format(mesh.get_path_name(), sk.get_path_name()))
    log("compatible: {}".format([str(s) for s in sk.get_editor_property("compatible_skeletons")]))
    cdo = unreal.get_default_object(EAL.load_blueprint_class(NPC))
    log("anim class {}".format(cdo.mesh.get_editor_property("anim_class")))

FRANK_SKEL = "/Game/Frank_Slash_Pack/Frank_Damages/Skeletal_Meshes/SK_Mannequin_Skeleton"
SEQS = {"Back": ("Frank_Damage_Stun04_start", "Frank_Damage_Stun04_end"),
        "Front": ("Frank_Damage_Stun05_start", "Frank_Damage_Stun05_end")}


def step_compat():
    mesh = _npc_mesh()
    sk = mesh.get_editor_property("skeleton")
    frank = unreal.load_asset(FRANK_SKEL)
    have = [s.get_path_name() for s in sk.get_editor_property("compatible_skeletons") if s]
    if frank.get_path_name() in have:
        log("ok: {} уже совместим с {}".format(frank.get_name(), sk.get_name()))
        return
    sk.modify()
    sk.add_compatible_skeleton(frank)
    saved = EAL.save_asset(sk.get_path_name().split(".")[0], False)
    log("MODIFIED: {} + compatible {} | saved {}".format(sk.get_name(), frank.get_name(), saved))


def measure():
    """Компонентное пространство меша бойца: вперёд это +Y, вверх +Z."""
    mesh = _npc_mesh()
    opts = unreal.AnimPoseEvaluationOptions()
    opts.set_editor_property("optional_skeletal_mesh", mesh)
    opts.set_editor_property("should_retarget", True)
    opts.set_editor_property("extract_root_motion", False)
    X = unreal.AnimPoseExtensions
    for side, names in SEQS.items():
        for name in names:
            anim = unreal.load_asset(STUN + "/" + name)
            n = anim.get_play_length()
            log("{} {} len {:.2f}".format(side, name, n))
            t = 0.0
            while t <= n + 0.001:
                pose = X.get_anim_pose_at_time(anim, min(t, n), opts)
                def p(b):
                    v = X.get_bone_pose(pose, b, unreal.AnimPoseSpaces.WORLD).translation
                    return "({:.0f},{:.0f},{:.0f})".format(v.x, v.y, v.z)
                log("  t {:.2f} root {} pelvis {} head {} footL {} footR {} handR {}".format(
                    t, p("root"), p("pelvis"), p("head"), p("foot_l"), p("foot_r"), p("hand_r")))
                t += 0.2 if n > 0.5 else 0.1


def step_copies():
    """Копии четырёх секвенций пака с включённым рут моушном: оригиналы пака не трогаются. С рут
    моушном корень вынимается из позы, и капсулу везёт движение, а не меш уезжает от капсулы."""
    for f in ("add_anim_segment", "list_anim_segments", "get_root_motion_at_time", "remove_anim_segment"):
        log("DOC {}: {}".format(f, (getattr(unreal.AnimMontageService, f).__doc__ or "")[:500].replace(chr(10), " | ")))
    for side, names in SEQS.items():
        for name in names:
            dst = "{}/{}_RM".format(OUT, name)
            if not EAL.does_asset_exist(dst):
                EAL.duplicate_asset(STUN + "/" + name, dst)
                log("CREATED: " + dst)
            anim = unreal.load_asset(dst)
            if not anim.get_editor_property("enable_root_motion"):
                anim.set_editor_property("enable_root_motion", True)
                log("MODIFIED: {} enable_root_motion".format(dst))
            EAL.save_asset(dst, False)


def step_montages():
    """Один монтаж на падение: start, сразу за ним end, без петли лежания. Позы на стыке совпадают
    (замер: таз и голова расходятся до 4 см), потому что end записан от места, где кончился start."""
    AMS = unreal.AnimMontageService
    for side, (start, end) in SEQS.items():
        name = "AM_Knockdown_" + side
        path = "{}/{}".format(OUT, name)
        a, b = "{}/{}_RM".format(OUT, start), "{}/{}_RM".format(OUT, end)
        if not EAL.does_asset_exist(path):
            made = AMS.create_montage_from_animation(a, OUT, name)
            log("CREATED: {}".format(made))
            first = unreal.load_asset(a).get_play_length()
            idx = AMS.add_anim_segment(path, 0, b, first, 1.0)
            log("ADDED: segment {} at {:.2f} -> index {}".format(end, first, idx))
        AMS.set_blend_in(path, 0.1, "Linear")
        AMS.set_blend_out(path, 0.2, "Linear")
        EAL.save_asset(path, False)
        segs = AMS.list_anim_segments(path, 0)
        log("{}: length {:.2f}, segments {}".format(name, AMS.get_montage_length(path), len(segs)))
        m = unreal.load_asset(path)
        log("   slots {} skeleton {}".format(AMS.get_all_used_slot_names(path), m.get_editor_property("skeleton").get_name()))
        for t in (0.2, 0.6, AMS.get_montage_length(path) - 0.05):
            log("   root at {:.2f}: {}".format(t, AMS.get_root_motion_at_time(path, t)))


def step_enemy():
    bp = unreal.load_asset(NPC)
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    lib = unreal.SubobjectDataBlueprintFunctionLibrary
    helmet = None
    for h in sds.k2_gather_subobject_data_for_blueprint(bp):
        obj = lib.get_object(sds.k2_find_subobject_data_from_handle(h))
        if isinstance(obj, unreal.HelmetComponent):
            helmet = obj
    if helmet is None:
        raise RuntimeError("у BP_ShooterNPC нет Helmet")
    changed = []
    for prop, side in (("knockdown_montage_front", "Front"), ("knockdown_montage_back", "Back")):
        m = unreal.load_asset("{}/AM_Knockdown_{}".format(OUT, side))
        if helmet.get_editor_property(prop) != m:
            helmet.set_editor_property(prop, m)
            changed.append(prop)
    if changed:
        ref = "{}.{}".format(NPC, NPC.split("/")[-1])
        unreal.ToolsetRegistry.execute_tool(BT, "compile_blueprint", json.dumps({"blueprint": {"refPath": ref}}))
        saved = EAL.save_asset(NPC, False)
        log("MODIFIED: BP_ShooterNPC Helmet: {} | status {} | saved {}".format(", ".join(changed), bp.get_editor_property("status"), saved))
    else:
        log("ok: BP_ShooterNPC Helmet")
