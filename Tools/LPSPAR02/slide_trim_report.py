import unreal

# For every slide clip: how long is the "dead head" (nothing happens) and when does the motion end.
# pelvis height (z) is the proxy: standing ~89-96 cm, sliding ~16-17 cm.
BASE = '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/'
CLIPS = [
    'M_Neutral_Slide_FootOut_Into_Lfoot', 'M_Neutral_Slide_FootOut_Into_Rfoot',
    'M_Relaxed_Slide_FootOut_Into_Lfoot', 'M_Relaxed_Slide_FootOut_Into_Rfoot',
    'M_Relaxed_Slide_FootOut_Into_Sprint_Lfoot', 'M_Relaxed_Slide_FootOut_Into_Sprint_Rfoot',
    'M_Neutral_Slide_FootOut_Loop', 'M_Relaxed_Slide_FootOut_Loop',
    'M_Neutral_Slide_FootOut_Out_Idle_Stand', 'M_Neutral_Slide_FootOut_Out_Idle_Crouch',
    'M_Neutral_Slide_FootOut_Out_Moving_Run', 'M_Neutral_Slide_FootOut_Out_Moving_Walk',
    'M_Neutral_Slide_FootOut_Out_Moving_Crouch',
    'M_Relaxed_Slide_FootOut_Out_Moving_Sprint', 'M_Relaxed_Slide_FootOut_Out_Idle_Crouch',
]

print('%-46s %6s %6s %6s %8s' % ('clip', 'len', 'head', 'tail', 'z@0 -> z@move'))
for name in CLIPS:
    a = unreal.load_asset(BASE + name)
    if not a:
        print('%-46s MISSING' % name)
        continue
    length = a.get_editor_property('sequence_length')
    zs = []
    t = 0.0
    while t <= min(length, 4.0) + 1e-6:
        try:
            zs.append((t, unreal.AnimationLibrary.get_bone_pose_for_time(a, 'pelvis', t, False).translation.z))
        except Exception as e:
            zs.append((t, None))
        t = round(t + 0.1, 2)
    good = [(t, z) for t, z in zs if z is not None]
    if not good:
        print('%-46s no data' % name)
        continue
    z0 = good[0][1]
    head = next((t for t, z in good if abs(z - z0) > 5.0), None)
    tail = 0.0
    for i in range(1, len(good)):
        if abs(good[i][1] - good[i - 1][1]) > 3.0:
            tail = good[i][0]
    print('%-46s %6.2f %6s %6.2f   %.0f -> %.0f' % (
        name, length, ('%.2f' % head) if head is not None else '-', tail, z0,
        good[-1][1]))
