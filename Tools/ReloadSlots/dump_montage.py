# Slot tracks, segments and notifies of a few third person reload montages, side by side.
import unreal

D = "/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Characters/Animations/ARs/"
NAMES = ["AM_TP_CH_AR_01_Reload_Empty", "AM_TP_CH_AR_01_Reload", "AM_TP_CH_AR_03_Reload_Empty"]


def dump():
    for n in NAMES:
        m = unreal.find_object(None, D + n + "." + n) or unreal.load_object(None, D + n + "." + n)
        print("==", n, "len", round(m.get_play_length(), 2), "blend in/out",
              round(m.get_editor_property("blend_in").get_editor_property("blend_time"), 2),
              round(m.get_editor_property("blend_out").get_editor_property("blend_time"), 2),
              "auto_out", m.get_editor_property("enable_auto_blend_out"))
        for tr in m.get_editor_property("slot_anim_tracks"):
            segs = tr.get_editor_property("anim_track").get_editor_property("anim_segments")
            print("  SLOT", tr.get_editor_property("slot_name"), [
                (s.get_editor_property("anim_reference").get_name(), round(s.get_editor_property("start_pos"), 2),
                 round(s.get_editor_property("anim_start_time"), 2), round(s.get_editor_property("anim_end_time"), 2),
                 round(s.get_editor_property("anim_play_rate"), 2)) for s in segs])
            for s in segs:
                a = s.get_editor_property("anim_reference")
                print("    SEQ", a.get_name(), "len", round(a.get_play_length(), 2), "additive",
                      a.get_editor_property("additive_anim_type"), a.get_editor_property("ref_pose_type"),
                      "curves", [str(c) for c in unreal.AnimationLibrary.get_animation_curve_names(a, unreal.RawCurveCurveTypes.RCT_FLOAT if hasattr(unreal, "RawCurveCurveTypes") else unreal.RawCurveTrackTypes.RCT_FLOAT)])
        try:
            for ev in unreal.AnimationLibrary.get_animation_notify_events(m):
                nf = ev.get_editor_property("notify") or ev.get_editor_property("notify_state_class")
                print("  NOTIFY", ev.get_editor_property("notify_name"), nf.get_class().get_name() if nf else None,
                      round(ev.get_editor_property("link_value"), 2), round(ev.get_editor_property("duration"), 2))
        except Exception as exc:
            print("  NOTIFY READ FAILED", exc)
