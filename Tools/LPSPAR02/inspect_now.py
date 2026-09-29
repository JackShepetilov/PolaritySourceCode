import unreal

# READ ONLY dump of the current ABP_AR02_LPSP_Test AnimGraph: every node, its pins and the settings of
# the interesting nodes (layered blends with masks, sequence players, modify bone). Plus the skeleton
# hierarchy, which decides what a branch filter actually does.
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
SAVED = unreal.Paths.project_saved_dir() + 'LPSP_AR02/'
L = []


def log(m):
    L.append(str(m))


pkg = unreal.load_asset(AB).get_outermost()
log('game world (PIE): %s' % (unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None))
try:
    log('dirty content: %s' % [p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()])
except Exception as e:
    log('dirty ERR %s' % e)

nodes = []
for n in unreal.ObjectIterator(unreal.EdGraphNode):
    try:
        if n.get_outermost() == pkg and n.get_outer() and n.get_outer().get_name() == GRAPH:
            nodes.append(n)
    except Exception:
        pass
log('AnimGraph nodes: %d' % len(nodes))

SKIP = ('static_class', 'class', 'outer', 'package', 'full_name', 'path_name', 'name',
        'blend_poses', 'blend_weights', 'layer_setup', 'blend_masks', 'base_pose', 'local_pose',
        'component_pose', 'pose', 'source_pose', 'node', 'bones_to_modify', 'bone_to_modify')

for n in sorted(nodes, key=lambda x: x.get_name()):
    try:
        title = str(n.get_node_title()).replace('\r', ' ').replace('\n', ' ')
    except Exception:
        title = '?'
    log('')
    log('%s | %s' % (n.get_name(), title[:80]))
    try:
        pins = n.list_all_pins()
    except Exception as e:
        log('    pins ERR %s' % e)
        pins = []
    for p in pins:
        try:
            q = [x.get_owning_node().get_name() + '.' + str(x.get_pin_name()) for x in p.list_connected_pins()]
            d = 'IN ' if p.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_INPUT else 'OUT'
        except Exception as e:
            q, d = ['ERR %s' % e], '??'
        if q or d == 'OUT':
            log('    %-3s %-16s %s' % (d, str(p.get_pin_name()), q))
    if not any(k in n.get_name() for k in ('LayeredBoneBlend', 'SequencePlayer', 'RandomPlayer', 'ModifyBone', 'TwoWayBlend')):
        continue
    try:
        inner = n.get_editor_property('node')
    except Exception as e:
        log('    << inner ERR %s' % e)
        continue
    if 'LayeredBoneBlend' in n.get_name():
        try:
            for i, f in enumerate(inner.get_editor_property('layer_setup')):
                log('    LAYER_SETUP[%d] %s' % (i, f.export_text()))
        except Exception as e:
            log('    layer_setup ERR %s' % e)
        try:
            log('    BLEND_MASKS %s' % [str(m) for m in inner.get_editor_property('blend_masks')])
        except Exception as e:
            log('    blend_masks ERR %s' % e)
    for k in dir(inner):
        if k.startswith('_') or k in SKIP:
            continue
        try:
            v = getattr(inner, k)
        except Exception:
            continue
        if callable(v):
            continue
        try:
            r = v.get_name()
        except Exception:
            r = v
        log('    .%-30s %s' % (k, r))

log('')
log('===== SKELETON =====')
ab = unreal.load_asset(AB)
skel = None
try:
    skel = ab.get_editor_property('target_skeleton')
except Exception as e:
    log('target_skeleton ERR %s' % e)
log('skeleton: %s' % (skel.get_name() if skel else None))
log('AnimPoseExtensions: %s' % [a for a in dir(unreal.AnimPoseExtensions) if not a.startswith('_')])
BONES = ('root', 'pelvis', 'spine_01', 'spine_02', 'spine_03', 'thigh_l', 'calf_l', 'foot_l',
         'ik_foot_root', 'ik_foot_l', 'ik_hand_root', 'ik_hand_gun', 'ik_hand_l', 'ik_hand_r',
         'clavicle_r', 'upperarm_r', 'lowerarm_r', 'hand_r', 'neck_01', 'head')
if skel:
    try:
        pose = unreal.AnimPoseExtensions.get_reference_pose(skel)
        for b in BONES:
            try:
                p = unreal.AnimPoseExtensions.find_bone_path_to_root(pose, b)
                log('%-14s <- %s' % (b, ' / '.join(str(x) for x in p)))
            except Exception as e:
                log('%-14s ERR %s' % (b, e))
    except Exception as e:
        log('get_reference_pose ERR %s' % e)

log('')
log('===== PAWN TP MESH =====')
try:
    bp = unreal.load_asset('/Game/Variant_Shooter/Tests/LPSP_AR02/BP_ShooterCharacter_AR02_Test')
    cdo = unreal.get_default_object(bp.generated_class())
    mesh = cdo.get_editor_property('mesh')
    sm = mesh.get_editor_property('skeletal_mesh_asset')
    log('pawn TP mesh: %s' % (sm.get_name() if sm else None))
    log('anim class: %s' % mesh.get_editor_property('anim_class'))
    sk = sm.get_editor_property('skeleton')
    log('mesh skeleton: %s' % (sk.get_name() if sk else None))
    for s in sm.get_editor_property('sockets'):
        log('  socket %-20s bone %s' % (s.get_editor_property('socket_name'), s.get_editor_property('bone_name')))
except Exception as e:
    log('pawn mesh ERR %s' % e)

open(SAVED + 'graph_now.txt', 'w', encoding='utf-8').write('\n'.join(L))
print('WROTE graph_now.txt lines=%d' % len(L))
