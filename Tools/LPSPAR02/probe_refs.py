import unreal

# READ ONLY. Answers the questions the slide mask depends on:
#  1) the real bone hierarchy of SKEL_Character (what a branch filter covers)
#  2) the layer node's blend mode and the phase players' flags (loop / play rate), read explicitly,
#     because dir() on these wrappers does not list every property
#  3) how the Low Poly Shooter Pack itself masks a whole-body posture change on this skeleton
SAVED = unreal.Paths.project_saved_dir() + 'LPSP_AR02/'
L = []


def log(m):
    L.append(str(m))


AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'

log('===== DOCSTRINGS =====')
for fn in ('get_reference_pose', 'get_bone_names', 'get_bone_pose', 'get_ref_bone_pose',
           'get_ref_pose_relative_transform', 'get_relative_transform', 'get_relative_to_ref_pose_transform',
           'get_anim_pose_at_time'):
    f = getattr(unreal.AnimPoseExtensions, fn, None)
    log('%s: %s' % (fn, ((f.__doc__ or '').strip().replace('\n', ' | ')[:400]) if f else 'MISSING'))

log('')
log('===== BONE HIERARCHY (derived) =====')
skel = unreal.load_asset(AB).get_editor_property('target_skeleton')
pose = unreal.AnimPoseExtensions.get_reference_pose(skel)
names = [str(n) for n in unreal.AnimPoseExtensions.get_bone_names(pose)]
log('bones: %d' % len(names))
log('order: %s' % ', '.join(names))
comp = {}
local = {}
for n in names:
    try:
        comp[n] = unreal.AnimPoseExtensions.get_bone_pose(pose, n, unreal.AnimPoseSpaces.COMPONENT)
    except Exception as e:
        log('comp %s ERR %s' % (n, e))
    try:
        local[n] = unreal.AnimPoseExtensions.get_ref_pose_relative_transform(pose, n)
    except Exception as e:
        log('local %s ERR %s' % (n, e))


def loc_of(t):
    try:
        return unreal.MathLibrary.break_transform(t)[0]
    except Exception:
        return None


for b in ('root', 'pelvis', 'spine_01', 'thigh_l', 'calf_l', 'foot_l', 'ik_foot_root', 'ik_foot_l',
          'ik_hand_root', 'ik_hand_gun', 'ik_hand_l', 'ik_hand_r', 'hand_r', 'lowerarm_r', 'clavicle_r'):
    if b not in comp or b not in local:
        log('%-14s MISSING' % b)
        continue
    best, bestd = None, None
    for p in names:
        if p == b:
            continue
        try:
            t = unreal.MathLibrary.compose_transforms(comp[p], local[b])
            d = (loc_of(t) - loc_of(comp[b])).length()
        except Exception as e:
            log('%s <- %s ERR %s' % (b, p, e))
            break
        if bestd is None or d < bestd:
            best, bestd = p, d
    log('%-14s parent=%s  err=%.5f  idx=%d' % (b, best, bestd if bestd is not None else -1, names.index(b)))

log('')
log('===== LAYER + PLAYERS (explicit reads) =====')
pkg = unreal.load_asset(AB).get_outermost()


def nodes(graph='AnimGraph', p=pkg):
    out = []
    for n in unreal.ObjectIterator(unreal.EdGraphNode):
        try:
            if n.get_outermost() == p and n.get_outer() and n.get_outer().get_name() == graph:
                out.append(n)
        except Exception:
            pass
    return out


def show(node, props):
    log('%s [%s]' % (node.get_name(), str(node.get_node_title()).replace('\n', ' ')))
    try:
        inner = node.get_editor_property('node')
    except Exception as e:
        log('   inner ERR %s' % e)
        return
    for p in props:
        try:
            v = inner.get_editor_property(p)
            try:
                v = v.get_name()
            except Exception:
                pass
            log('   %-30s %s' % (p, v))
        except Exception as e:
            log('   %-30s ERR %s' % (p, e))


for nm in ('AnimGraphNode_LayeredBoneBlend_6', 'AnimGraphNode_SequencePlayer_5', 'AnimGraphNode_SequencePlayer_6',
           'AnimGraphNode_SequencePlayer_7', 'AnimGraphNode_SequencePlayer_1'):
    for n in nodes():
        if n.get_name() == nm:
            show(n, ('blend_mode', 'blend_weights', 'root_bone_name', 'blend_root_motion_based_on_root_bone',
                     'reset_child_on_activation', 'sequence', 'loop_animation', 'play_rate',
                     'start_position', 'play_rate_scale', 'override_play_rate' if nm else ''))

log('')
log('===== PACK REFERENCE (ABP_LPSP_TP_PCH) =====')
PACK = '/Game/InfimaGames/LowPolyShooterPack/Core/Characters/ABP_LPSP_TP_PCH'
asset = unreal.load_asset(PACK)
if not asset:
    log('pack ABP not found at %s' % PACK)
else:
    p2 = asset.get_outermost()
    for n in unreal.ObjectIterator(unreal.EdGraphNode):
        try:
            if n.get_outermost() != p2 or not n.get_outer() or 'LayeredBoneBlend' not in n.get_name():
                continue
            g = n.get_outer().get_name()
            inner = n.get_editor_property('node')
            log('%s | graph %s | %s' % (n.get_name(), g, str(n.get_node_title()).replace('\n', ' ')))
            try:
                log('   blend_mode %s' % inner.get_editor_property('blend_mode'))
            except Exception as e:
                log('   blend_mode ERR %s' % e)
            try:
                for i, f in enumerate(inner.get_editor_property('layer_setup')):
                    log('   LAYER_SETUP[%d] %s' % (i, f.export_text()))
            except Exception as e:
                log('   layer_setup ERR %s' % e)
        except Exception as e:
            log('scan ERR %s' % e)

open(SAVED + 'probe_refs.txt', 'w', encoding='utf-8').write('\n'.join(L))
print('WROTE probe_refs.txt lines=%d' % len(L))
