import unreal

# Put the slide branch next to the insertion point and label everything with comments, so it can be
# found and wired by hand in the editor.
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
BS = unreal.BlueprintService
L = []


def log(m):
    L.append(str(m))
    print(m)


log('add_comment_node: %s' % (BS.add_comment_node.__doc__ or '')[:300])
log('add_comment_around_nodes: %s' % (BS.add_comment_around_nodes.__doc__ or '')[:300])
log('set_node_position: %s' % (BS.set_node_position.__doc__ or '')[:300])

nodes = {}
for n in BS.get_nodes_in_graph(AB, GRAPH):
    d = BS.get_node_details(AB, GRAPH, n.node_id)
    nodes[(round(d.pos_x), round(d.pos_y))] = n.node_id

MOVE = {
    'SlideIn': ((0, -3000), (-6800, 2600)),
    'SlideLoop': ((500, -3000), (-6800, 2900)),
    'SlideOut': ((1000, -3000), (-6400, 2600)),
    'SlideOutCrouch': ((1500, -3000), (-6400, 2900)),
    'PhaseGate': ((-2500, -3600), (-6000, 2600)),
    'HoldGate': ((-2000, -3600), (-5600, 2600)),
    'ExitGate': ((-1200, -3600), (-5200, 2600)),
    'MaskLayer': ((-1500, -3600), (-4800, 2600)),
    'OuterGate': ((-1000, -3600), (-4300, 2600)),
    'JumpGate': ((-600, -3600), (-3900, 2600)),
    'PawnOwner': ((-3500, -4200), (-6800, 3300)),
    'CastChar': ((-3100, -4200), (-6400, 3300)),
    'Apex': ((-2700, -4200), (-6000, 3300)),
    'IsSliding': ((-2300, -4200), (-5600, 3300)),
    'SlideDuration': ((-2300, -4500), (-5600, 3600)),
    'LessThan': ((-1900, -4200), (-5200, 3300)),
    'SlideAlphaFn': ((-1900, -2900), (-4800, 3300)),
    'GetCrouching': ((-1700, -3300), (-4400, 3300)),
    'GetJumping': ((-1700, -3600), (-4400, 3600)),
    'NotJumping': ((-900, -4000), (-4000, 3300)),
}
moved = []
for tag, (cur, new) in MOVE.items():
    nid = nodes.get(cur)
    if not nid:
        log('%s: node not found at %s' % (tag, cur))
        continue
    ok = BS.set_node_position(AB, GRAPH, nid, new[0], new[1])
    log('%s -> %s : %s' % (tag, new, ok))
    if ok:
        moved.append(nid)

# comments: one over the insertion point, one over the branch
ids_ins = [i for (k, i) in nodes.items() if k in ((-4046, 2480), (-3801, 2480), (-3540, 2480),
                                                  (-3279, 2480), (-3018, 2480))]
t1 = ('СЛАЙД: ВРЕЗКА ЗДЕСЬ. Разорвать Fabrik_8.Pose -> ComponentToLocalSpace_5.ComponentPose, '
      'затем Fabrik_8.Pose -> LayeredBoneBlend_3.BasePose, BlendListByBool_5.BlendPose_0 <- Fabrik_8.Pose, '
      'BlendListByBool_5.Pose -> ComponentToLocalSpace_5.ComponentPose')
try:
    log('comment around insertion: %s' % BS.add_comment_around_nodes(AB, GRAPH, ids_ins, t1))
except Exception as e:
    log('add_comment_around_nodes err: %s' % e)
    try:
        log('add_comment_node: %s' % BS.add_comment_node(AB, GRAPH, t1, -4200.0, 2150.0))
    except Exception as e2:
        log('add_comment_node err: %s' % e2)

t2 = 'ВЕТКА СЛАЙДА (собрана, но ОТКЛЮЧЕНА от пайплайна): OuterGate -> MaskLayer -> фазы In/Loop/Out'
try:
    log('comment around branch: %s' % BS.add_comment_around_nodes(AB, GRAPH, moved, t2))
except Exception as e:
    log('branch comment err: %s' % e)

log('save: %s' % unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/anchors.log', 'w', encoding='utf-8').write('\n'.join(L))
