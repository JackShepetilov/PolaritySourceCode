import unreal

# Where inside the retargeted slide clips is the actual motion? Sample the pelvis/foot pose over time.
BASE = '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/'
CLIPS = ['M_Neutral_Slide_FootOut_Into_Lfoot', 'M_Neutral_Slide_FootOut_Loop',
         'M_Neutral_Slide_FootOut_Out_Moving_Run']

print('AnimPoseExtensions doc:')
print((unreal.AnimPoseExtensions.get_anim_pose_at_time.__doc__ or '')[:500])
print('\nAnimationLibrary doc for get_bone_pose_for_time:')
try:
    print((unreal.AnimationLibrary.get_bone_pose_for_time.__doc__ or '')[:400])
except Exception as e:
    print('no such method:', e)

for name in CLIPS:
    a = unreal.load_asset(BASE + name)
    if not a:
        print('MISSING', name)
        continue
    length = a.get_editor_property('sequence_length')
    print('\n=== %s (%.2f s) ===' % (name, length))
    for t in [0.0, 0.2, 0.5, 1.0, 2.0, 3.0, 4.0, 6.0]:
        if t > length:
            continue
        try:
            p = unreal.AnimationLibrary.get_bone_pose_for_time(a, 'pelvis', t, False)
            f = unreal.AnimationLibrary.get_bone_pose_for_time(a, 'foot_l', t, False)
            print('   t=%.1f pelvis=(%.0f,%.0f,%.0f) foot_l=(%.0f,%.0f,%.0f)' % (
                t, p.translation.x, p.translation.y, p.translation.z,
                f.translation.x, f.translation.y, f.translation.z))
        except Exception as e:
            print('   t=%.1f ERR %s' % (t, e))
            break
