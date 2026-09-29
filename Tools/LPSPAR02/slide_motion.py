import unreal

# How long is the actual motion inside the retargeted slide sequences?
# Sample pelvis + left foot world-ish local position over time.
SETS = [
    ('/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/M_Neutral_Slide_FootOut_Into_Lfoot', ['pelvis', 'foot_l']),
    ('/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/M_Neutral_Slide_FootOut_Loop', ['pelvis', 'foot_l']),
    ('/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/M_Neutral_Slide_FootOut_Out_Moving_Run', ['pelvis', 'foot_l']),
    ('/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/M_Neutral_Slide_FootOut_Out_Idle_Crouch', ['pelvis', 'foot_l']),
]

for path, bones in SETS:
    a = unreal.load_asset(path)
    if not a:
        print('MISSING', path)
        continue
    length = a.get_editor_property('sequence_length')
    print('=== %s (%.2f s)' % (path.split('/')[-1], length))
    for b in bones:
        row = []
        for t in [0.0, 0.2, 0.4, 0.6, 1.0, 2.0, 3.0, min(5.0, length), max(0.0, length - 0.05)]:
            try:
                pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(a, t, unreal.AnimPoseOptionalData())
                p = unreal.AnimPoseExtensions.get_bone_pose(pose, b, unreal.AnimPoseSpaces.WORLD)
                row.append('t=%.1f:(%.0f,%.0f,%.0f)' % (t, p.translation.x, p.translation.y, p.translation.z))
            except Exception as e:
                row.append('t=%.1f:ERR' % t)
        print('   %s ' % b, ' '.join(row))

# Are Cast nodes available?
BS = unreal.BlueprintService
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
print('\n=== cast-ish nodes ===')
for term in ('Cast To Polarity', 'Cast To Character', 'Pawn Owner', 'Movement Component'):
    ks = BS.discover_nodes(AB, term, '', 8)
    print('---', term)
    for k in ks:
        print('    ', k.spawner_key, '|', k.display_name)
