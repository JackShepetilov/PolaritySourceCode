import unreal

# READ ONLY. What is actually inside the cropped slide clips: pelvis height, leg extension, and how
# much the legs move over the clip (a "frozen" looking slide can come from a static loop asset).
SAVED = unreal.Paths.project_saved_dir() + 'LPSP_AR02/'
L = []
A = '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/'


def log(m):
    L.append(str(m))


log('AnimPoseSpaces: %s' % [a for a in dir(unreal.AnimPoseSpaces) if not a.startswith('_')])

mesh = None
try:
    bp = unreal.load_asset('/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterCharacter_AR02_Test')
    cdo = unreal.get_default_object(bp.generated_class())
    mesh = cdo.get_editor_property('mesh').get_editor_property('skeletal_mesh_asset')
    log('mesh: %s  path: %s' % (mesh.get_name(), mesh.get_path_name()))
except Exception as e:
    log('mesh ERR %s' % e)

skeleton = None
try:
    skeleton = unreal.load_asset('/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test').get_editor_property('target_skeleton')
except Exception as e:
    log('skeleton ERR %s' % e)

BONES = ('Pelvis', 'Thigh_L', 'Foot_L', 'spine_01')


def sample(pose, tag):
    d = {}
    for b in BONES:
        try:
            t = unreal.AnimPoseExtensions.get_bone_pose(pose, b, unreal.AnimPoseSpaces.WORLD)
            d[b] = t
        except Exception as e:
            log('   %s %s ERR %s' % (tag, b, e))
    return d


if skeleton:
    ref = unreal.AnimPoseExtensions.get_reference_pose(skeleton)
    d = sample(ref, 'ref')
    log('REFERENCE POSE: ' + '  '.join('%s=(%.1f,%.1f,%.1f)' % (b, d[b].translation.x, d[b].translation.y, d[b].translation.z)
                                        for b in BONES if b in d))

CLIPS = ('M_Neutral_Slide_FootOut_Into_Lfoot', 'M_Neutral_Slide_FootOut_Loop',
         'M_Neutral_Slide_FootOut_Out_Moving_Walk', 'M_Neutral_Slide_FootOut_Out_Idle_Crouch',
         'M_Neutral_Slide_FootOut_Out_Idle_Stand')

for c in CLIPS:
    a = unreal.load_asset(A + c)
    if not a:
        log('%s MISSING' % c)
        continue
    length = unreal.AnimationLibrary.get_sequence_length(a)
    sk = None
    try:
        sk = a.get_editor_property('skeleton').get_name()
    except Exception as e:
        sk = 'ERR %s' % e
    log('')
    log('%s len=%.3f skeleton=%s' % (c, length, sk))
    opts = unreal.AnimPoseEvaluationOptions()
    try:
        opts.should_retarget = True
        opts.optional_skeletal_mesh = mesh
    except Exception as e:
        log('   opts ERR %s' % e)
    prev = None
    foot_z, pel_z, thigh = [], [], []
    steps = 10
    for i in range(steps + 1):
        t = length * i / float(steps)
        if t >= length:
            t = max(0.0, length - 0.001)
        try:
            pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(a, t, opts)
        except Exception as e:
            log('   eval t=%.2f ERR %s' % (t, e))
            break
        d = sample(pose, 't=%.2f' % t)
        pz = d['Pelvis'].translation.z if 'Pelvis' in d else -1.0
        fz = d['Foot_L'].translation.z if 'Foot_L' in d else -1.0
        dp = (d['Foot_L'].translation - d['Pelvis'].translation).length() if ('Pelvis' in d and 'Foot_L' in d) else -1.0
        if pz > -1:
            pel_z.append(pz)
        if fz > -1:
            foot_z.append(fz)
        q = d['Thigh_L'].rotation if 'Thigh_L' in d else None
        if q is not None:
            thigh.append((q.x, q.y, q.z, q.w))
        log('   t=%.2f Pelvis z=%.1f  Foot_L z=%.1f  |Foot-Pelvis|=%.1f  Thigh_L q=%s'
            % (t, pz, fz, dp, ('(%.2f,%.2f,%.2f,%.2f)' % thigh[-1]) if q is not None else 'n/a'))
    if thigh:
        span = [max(v[i] for v in thigh) - min(v[i] for v in thigh) for i in range(4)]
        log('   MOTION OVER CLIP: pelvis z %.1f..%.1f  foot z %.1f..%.1f  thigh quat span (%.3f,%.3f,%.3f,%.3f)'
            % (min(pel_z), max(pel_z), min(foot_z), max(foot_z), span[0], span[1], span[2], span[3]))

open(SAVED + 'clip_poses.txt', 'w', encoding='utf-8').write('\n'.join(L))
print('WROTE clip_poses.txt lines=%d' % len(L))
