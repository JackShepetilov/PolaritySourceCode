# Read-only dump of a montage asset: slots, notifies, segments, skeleton.
# Usage (editor console): py "<path>/dump_throw_montage.py"
import unreal

PATHS = [
    "/Game/Characters/Mannequins/Anims/ThrowAnim/SkeletalMeshes/ThrowAnim_Montage",
]

TAG = "[YANK_DUMP]"

def dump_montage(path):
    m = unreal.load_asset(path)
    if not m:
        unreal.log_warning(f"{TAG} FAILED to load {path}")
        return
    unreal.log(f"{TAG} ===== {path} =====")
    unreal.log(f"{TAG} class={m.get_class().get_name()}")
    try:
        unreal.log(f"{TAG} play_length={m.get_play_length():.3f}s")
    except Exception as e:
        unreal.log(f"{TAG} play_length error: {e}")
    try:
        skel = m.get_editor_property("skeleton")
        unreal.log(f"{TAG} skeleton={skel.get_path_name() if skel else None}")
    except Exception as e:
        unreal.log(f"{TAG} skeleton error: {e}")

    # Slot tracks + segments
    try:
        tracks = m.get_editor_property("slot_anim_tracks")
        for t in tracks:
            slot_name = t.get_editor_property("slot_name")
            anim_track = t.get_editor_property("anim_track")
            segs = anim_track.get_editor_property("anim_segments")
            unreal.log(f"{TAG} slot='{slot_name}' segments={len(segs)}")
            for s in segs:
                ref = s.get_editor_property("anim_reference")
                start = s.get_editor_property("start_pos")
                unreal.log(f"{TAG}   segment anim={ref.get_path_name() if ref else None} start_pos={start}")
    except Exception as e:
        unreal.log(f"{TAG} slot tracks error: {e}")

    # Notifies
    try:
        notifies = m.get_editor_property("animation_notifies") if hasattr(m, "animation_notifies") else None
    except Exception:
        notifies = None
    if notifies is None:
        try:
            notifies = m.get_editor_property("notifies")
        except Exception as e:
            unreal.log(f"{TAG} notifies error: {e}")
            notifies = []
    unreal.log(f"{TAG} notify_count={len(notifies)}")
    for i, ev in enumerate(notifies):
        try:
            notify_obj = ev.get_editor_property("notify")
        except Exception:
            notify_obj = None
        try:
            state_obj = ev.get_editor_property("notify_state_class")
        except Exception:
            state_obj = None
        cls = None
        if notify_obj:
            cls = notify_obj.get_class().get_name()
        elif state_obj:
            cls = state_obj.get_class().get_name() + " (state)"
        try:
            t = ev.get_editor_property("link_value")
        except Exception:
            t = "?"
        try:
            nm = ev.get_editor_property("notify_name")
        except Exception:
            nm = "?"
        unreal.log(f"{TAG} notify[{i}] name={nm} class={cls} link_value={t}")

for p in PATHS:
    dump_montage(p)

unreal.log(f"{TAG} DONE")
