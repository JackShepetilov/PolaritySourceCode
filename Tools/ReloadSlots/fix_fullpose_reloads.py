# Two pack reload sequences are full poses sitting in montages for the ADDITIVE Action slots, so they
# replace the legs. Project copies of the sequences flagged additive like their pack neighbours, and
# montage copies that play them. Pack assets are left alone.
#   run.sh fix_fullpose_reloads stop_pie | probe | build | assign | verify
import json

import unreal

SRC = "/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Characters/Animations/ARs/"
DST = "/Game/Variant_Shooter/Anims/TP_Reload/"
WEP = "/Game/Variant_Shooter/Blueprints/Pickups/Weapons/Kinemation/"
# family: sequence to copy, healthy neighbour to take additive settings from, montage to copy
JOBS = {
    "AR01": {"seq": SRC + "A_TP_CH_AR_01_Reload_Empty", "like": SRC + "A_TP_CH_AR_01_Reload",
             "montage": SRC + "AM_TP_CH_AR_01_Reload_Empty",
             "weapons": [WEP + "ARs/BP_AKX203", WEP + "MG/BP_MGX5"]},
    "AR02": {"seq": SRC + "A_TP_CH_AR_02_Reload_Empty", "like": SRC + "A_TP_CH_AR_02_Reload",
             "montage": "/Game/Variant_Shooter/Tests/LPSP_AR02/AM_AR02_TP_Reload_Empty",
             "weapons": []},
}
ADD = ["additive_anim_type", "ref_pose_type", "ref_pose_seq", "ref_frame_index"]
EAL = unreal.EditorAssetLibrary


def _pie():
    return unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()


def stop_pie():
    res = unreal.ToolsetRegistry.execute_tool("EditorToolset.EditorAppToolset", "StopPIE", "{}")
    print("STOP_PIE", res.is_complete, res.error)


def _tracks(m):
    out = []
    for tr in m.get_editor_property("slot_anim_tracks"):
        segs = tr.get_editor_property("anim_track").get_editor_property("anim_segments")
        out.append((str(tr.get_editor_property("slot_name")),
                    [(s.get_editor_property("anim_reference").get_path_name(), round(s.get_editor_property("anim_end_time"), 2)) for s in segs]))
    return out


def probe():
    assert not _pie(), "PIE is running"
    print("DST_EXISTS", EAL.does_directory_exist(DST), EAL.list_assets(DST) if EAL.does_directory_exist(DST) else [])
    for fam, j in JOBS.items():
        for key in ("seq", "like"):
            a = EAL.load_asset(j[key])
            print(fam, key, a.get_name(), round(a.get_play_length(), 2), [str(a.get_editor_property(p)) for p in ADD])
        m = EAL.load_asset(j["montage"])
        print(fam, "montage", m.get_name(), round(m.get_play_length(), 2), _tracks(m))
    for w in ["ARs/BP_AKX203", "MG/BP_MGX5", "ARs/BP_MX16A4"]:
        print("WEAPON", w, EAL.does_asset_exist(WEP + w))


def _names(fam):
    return DST + "A_%s_TP_Reload_Empty_Additive" % fam, DST + "AM_%s_TP_Reload_Empty" % fam


def build():
    assert not _pie(), "PIE is running"
    for fam, j in JOBS.items():
        seq_path, mont_path = _names(fam)
        if not EAL.does_asset_exist(seq_path):
            assert EAL.duplicate_asset(j["seq"], seq_path), seq_path
            print("CREATED:", seq_path)
        if not EAL.does_asset_exist(mont_path):
            assert EAL.duplicate_asset(j["montage"], mont_path), mont_path
            print("CREATED:", mont_path)
        seq, like, mont = EAL.load_asset(seq_path), EAL.load_asset(j["like"]), EAL.load_asset(mont_path)
        for p in ("ref_pose_seq", "ref_frame_index", "ref_pose_type", "additive_anim_type"):
            seq.set_editor_property(p, like.get_editor_property(p))
        print("MODIFIED:", seq_path, "additive like", like.get_name())
        tracks = list(mont.get_editor_property("slot_anim_tracks"))
        for i, tr in enumerate(tracks):
            track = tr.get_editor_property("anim_track")
            segs = list(track.get_editor_property("anim_segments"))
            for k, s in enumerate(segs):
                s.set_editor_property("anim_reference", seq)
                segs[k] = s
            track.set_editor_property("anim_segments", segs)
            tr.set_editor_property("anim_track", track)
            tracks[i] = tr
        mont.modify()
        mont.set_editor_property("slot_anim_tracks", tracks)
        print("MODIFIED:", mont_path, "segments ->", seq.get_name())
        for path in (seq_path, mont_path):
            print("SAVED:", path, EAL.save_asset(path, False))


def assign():
    assert not _pie(), "PIE is running"
    for fam, j in JOBS.items():
        mont_path = _names(fam)[1]
        assert EAL.does_asset_exist(mont_path), mont_path
        for w in j["weapons"]:
            print("MODIFIED:", w, "ReloadMontageTP", unreal.BlueprintService.set_property(w, "ReloadMontageTP", mont_path))
            bp = EAL.load_asset(w)
            unreal.BlueprintEditorLibrary.compile_blueprint(bp)
            print("SAVED:", w, EAL.save_loaded_asset(bp, False))


def verify():
    for fam, j in JOBS.items():
        seq_path, mont_path = _names(fam)
        seq, mont = EAL.load_asset(seq_path), EAL.load_asset(mont_path)
        print(fam, "SEQ", [str(seq.get_editor_property(p)).split(" (")[0] for p in ADD])
        print(fam, "MONTAGE", round(mont.get_play_length(), 2), _tracks(mont))
        for w in j["weapons"]:
            cdo = unreal.get_default_object(EAL.load_blueprint_class(w))
            print(fam, "WEAPON", w.split("/")[-1], cdo.get_editor_property("ReloadMontageTP").get_path_name(),
                  "| tactical", cdo.get_editor_property("SecondaryReloadMontageTP").get_name())
    print("DIRTY", [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()])


def fix_base():
    """The base pose did not survive being written before the additive type: write it after."""
    assert not _pie(), "PIE is running"
    for fam, j in JOBS.items():
        seq_path = _names(fam)[0]
        seq, like = EAL.load_asset(seq_path), EAL.load_asset(j["like"])
        seq.modify()
        for p in ("additive_anim_type", "ref_pose_type", "ref_frame_index", "ref_pose_seq"):
            seq.set_editor_property(p, like.get_editor_property(p))
        print("MODIFIED:", seq_path, "base", seq.get_editor_property("ref_pose_seq"))
        print("SAVED:", seq_path, EAL.save_asset(seq_path, False))


def revert_mx16a4():
    """Measured after the fact: the pack's AR_02 empty reload sits in DefaultSlot and never froze the
    legs. BP_MX16A4 goes back to it; the AR02 copies stay unused."""
    assert not _pie(), "PIE is running"
    w = WEP + "ARs/BP_MX16A4"
    print("MODIFIED:", w, unreal.BlueprintService.set_property(w, "ReloadMontageTP", SRC + "AM_TP_CH_AR_02_Reload_Empty"))
    bp = EAL.load_asset(w)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    print("SAVED:", w, EAL.save_loaded_asset(bp, False))
