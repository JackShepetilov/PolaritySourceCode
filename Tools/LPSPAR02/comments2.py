import unreal

# Find the three nodes by their connections (positions moved, so coordinates are useless now)
# and put bright comment boxes on them.
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
BS = unreal.BlueprintService
L = []


def log(m):
    L.append(str(m))
    print(m)


conns = BS.get_connections(AB, GRAPH)
log('connections: %d' % len(conns))
if conns:
    c = conns[0]
    log('fields: %s' % [a for a in dir(c) if not a.startswith('_')])

# the node feeding Output Pose
ctl_id = None
for c in conns:
    if c.target_pin_name == 'Result':
        ctl_id = c.source_node_id
        log('output pose fed by: %s (%s)' % (ctl_id, c.source_node_title))

# what feeds that node
src_id = None
if ctl_id:
    for c in conns:
        if c.target_node_id == ctl_id and c.target_pin_name == 'ComponentPose':
            src_id = c.source_node_id
            log('  and it is fed by: %s (%s.%s)' % (src_id, c.source_node_title, c.source_pin_name))

# our new layer: it has an input BlendWeights_0 and its Pose goes nowhere
layer_ids = sorted({c.target_node_id for c in conns if c.target_pin_name == 'BlendWeights_0'})
log('nodes with a BlendWeights_0 input: %s' % layer_ids)
layer_id = None
for lid in layer_ids:
    outgoing = [c for c in conns if c.source_node_id == lid]
    if not outgoing:
        layer_id = lid
        log('  %s has no outgoing wire -> that is the new layer' % lid)
for lid in layer_ids:
    log('  %s outgoing: %s' % (lid, [(c.source_pin_name, c.target_node_name, c.target_pin_name)
                                     for c in conns if c.source_node_id == lid]))

if src_id and ctl_id:
    log('comment pair: %s' % BS.add_comment_around_nodes(
        AB, GRAPH,
        'СЛАЙД: ПРОВОД 1 отсюда -> BasePose нового слоя. ПРОВОД 2: слой -> ЭТА нода (Component To Local)',
        [src_id, ctl_id], 60.0, 1.0, 0.25, 0.25, 0.6))
if layer_id:
    log('comment layer: %s' % BS.add_comment_around_nodes(
        AB, GRAPH, 'НОВЫЙ СЛОЙ СЛАЙДА: маска ног и вес уже подключены; не хватает BasePose и Pose',
        [layer_id], 50.0, 0.2, 0.9, 0.25, 0.6))

log('save: %s' % unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/comments2.log', 'w', encoding='utf-8').write('\n'.join(L))
