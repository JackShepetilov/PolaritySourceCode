# Probe pass 2: discover accessors for slots/notifies on AnimMontage, then dump.
import unreal

TAG = "[YANK_DUMP2]"
m = unreal.load_asset("/Game/Characters/Mannequins/Anims/ThrowAnim/SkeletalMeshes/ThrowAnim_Montage")

attrs = [a for a in dir(m) if ("slot" in a.lower() or "notif" in a.lower() or "segment" in a.lower())]
unreal.log(f"{TAG} montage attrs: {attrs}")

lib_attrs = [a for a in dir(unreal.AnimationLibrary) if ("notify" in a.lower() or "slot" in a.lower() or "montage" in a.lower())]
unreal.log(f"{TAG} AnimationLibrary attrs: {lib_attrs}")

# Try the obvious candidates defensively
for cand in ("get_slot_names", "get_anim_notify_events", "get_animation_notify_events"):
    try:
        fn = getattr(m, cand, None) or getattr(unreal.AnimationLibrary, cand, None)
        unreal.log(f"{TAG} candidate {cand}: {'FOUND' if fn else 'missing'} doc={getattr(fn, '__doc__', '')!r}")
    except Exception as e:
        unreal.log(f"{TAG} candidate {cand} error: {e}")

unreal.log(f"{TAG} DONE")
