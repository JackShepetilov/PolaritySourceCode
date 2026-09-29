import unreal

# READ ONLY. Where does the slide's height drop live: in the root bone track or in the pelvis track?
# Offline the pelvis reads ~17 cm but in game it reads 61 cm, which is exactly the pelvis track value
# with the root track dropped. Also stop PIE on the way out.
SAVED = unreal.Paths.project_saved_dir() + 'LPSP_AR02/'
L = []
A = '/Game/Variant_Shooter/Tests/LPSP_AR02/SlideAnims/'


def log(m):
    L.append(str(m))


mesh = None
try:
    bp = unreal.load_asset('/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterCharacter_AR02_Test')
    mesh = unreal.get_default_object(bp.generated_class()).get_editor_property('mesh').get_editor_property('skeletal_mesh_asset')
except Exception as e:
    log('mesh ERR %s' % e)

CLIPS = ('M_Neutral_Slide_FootOut_Into_Lfoot', 'M_Neutral_Slide_FootOut_Loop',
         'M_Neutral_Slide_FootOut_Out_Moving_Walk', 'M_Neutral_Slide_FootOut_Out_Idle_Crouch')

for c in CLIPS:
    a = unreal.load_asset(A + c)
    if not a:
        log('%s MISSING' % c)
        continue
    length = unreal.AnimationLibrary.get_sequence_length(a)
    rm = None
    for p in ('enable_root_motion', 'b_enable_root_motion'):
        try:
            rm = a.get_editor_property(p)
            break
        except Exception:
            pass
    log('')
    log('%s len=%.3f enable_root_motion=%s' % (c, length, rm))
    opts = unreal.AnimPoseEvaluationOptions()
    opts.should_retarget = True
    opts.optional_skeletal_mesh = mesh
    for i in (0, 1, 2, 3, 4):
        t = min(length * i / 4.0, max(0.0, length - 0.001))
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(a, t, opts)
        rl = unreal.AnimPoseExtensions.get_bone_pose(pose, 'root', unreal.AnimPoseSpaces.LOCAL)
        pl = unreal.AnimPoseExtensions.get_bone_pose(pose, 'Pelvis', unreal.AnimPoseSpaces.LOCAL)
        rw = unreal.AnimPoseExtensions.get_bone_pose(pose, 'root', unreal.AnimPoseSpaces.WORLD)
        pw = unreal.AnimPoseExtensions.get_bone_pose(pose, 'Pelvis', unreal.AnimPoseSpaces.WORLD)
        log('   t=%.2f  root local z=%.1f  pelvis local z=%.1f | root world z=%.1f  pelvis world z=%.1f  (world sum check %.1f)'
            % (t, rl.translation.z, pl.translation.z, rw.translation.z, pw.translation.z,
               rw.translation.z + pl.translation.z))

log('')
log('===== pack clips for comparison =====')
FOLDERS = ('/Game/InfimaGames/LowPolyShooterPack/Core/Characters',
           '/Game/InfimaGames/LowPolyShooterPack/Usable')
seen = []
for f in FOLDERS:
    try:
        for p in unreal.EditorAssetLibrary.list_assets(f, recursive=True, include_folder=False):
            n = p.split('/')[-1].split('.')[0]
            if ('Crouch' in n or 'Slide' in n) and n.startswith(('A_', 'M_', 'SLIDE', 'Slide')):
                seen.append(p)
    except Exception as e:
        log('list %s ERR %s' % (f, e))
for p in sorted(set(seen))[:24]:
    a = unreal.load_asset(p)
    if not a or not hasattr(a, 'get_editor_property'):
        continue
    try:
        if a.get_class().get_name() not in ('AnimSequence', 'AnimComposite', 'AnimMontage'):
            continue
    except Exception:
        continue
    length = unreal.AnimationLibrary.get_sequence_length(a)
    rm = None
    try:
        rm = a.get_editor_property('enable_root_motion')
    except Exception:
        pass
    try:
        opts = unreal.AnimPoseEvaluationOptions()
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(a, 0.0, opts)
        rl = unreal.AnimPoseExtensions.get_bone_pose(pose, 'root', unreal.AnimPoseSpaces.LOCAL)
        pl = unreal.AnimPoseExtensions.get_bone_pose(pose, 'Pelvis', unreal.AnimPoseSpaces.LOCAL)
        log('%-46s len=%.2f root_motion=%s root local z=%.1f pelvis local z=%.1f' % (p.split('/')[-1], length, rm, rl.translation.z, pl.translation.z))
    except Exception as e:
        log('%-46s ERR %s' % (p.split('/')[-1], e))

try:
    r = unreal.ToolsetRegistry.execute_tool('EditorToolset.EditorAppToolset', 'StopPIE', '{"options": {}}')
    log('StopPIE is_complete=%s error=%s' % (r.is_complete, r.error))
except Exception as e:
    log('StopPIE ERR %s' % e)

open(SAVED + 'clip_root.txt', 'w', encoding='utf-8').write('\n'.join(L))
print('WROTE clip_root.txt lines=%d' % len(L))
