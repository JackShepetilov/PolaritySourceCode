import unreal, json

# Visual anchors: bright comment boxes around exactly the nodes the wires must touch, plus the layer.
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
BS = unreal.BlueprintService
L = []


def log(m):
    L.append(str(m))
    print(m)


def id_at(pos):
    for n in BS.get_nodes_in_graph(AB, GRAPH):
        d = BS.get_node_details(AB, GRAPH, n.node_id)
        if (round(d.pos_x), round(d.pos_y)) == pos:
            return n.node_id
    return None


ctl_id = id_at((-3018, 2480))          # "Component To Local" that feeds Output Pose
layer_id = id_at((-4100, 2400))        # the new slide layer
log('ctl id: %s  layer id: %s' % (ctl_id, layer_id))

src_id = None
for c in BS.get_connections(AB, GRAPH):
    if c.target_node_id == ctl_id and c.target_pin_name == 'ComponentPose':
        src_id = c.source_node_id
        log('source of Component To Local: %s (%s.%s)' % (src_id, c.source_node_name, c.source_pin_name))
log('source id: %s' % src_id)

# 1) the pair that must be re-wired
if src_id and ctl_id:
    log('comment pair: %s' % BS.add_comment_around_nodes(
        AB, GRAPH,
        'СЛАЙД, ДВА ПРОВОДА ТУТ: 1) из левой ноды в BasePose нового слоя (справа), '
        '2) из слоя обратно В ЭТУ ноду (Component To Local). Слой: маска ног, вес от SlideAlpha.',
        [src_id, ctl_id], 60.0, 1.0, 0.25, 0.25, 0.55))

# 2) the layer itself
if layer_id:
    log('comment layer: %s' % BS.add_comment_around_nodes(
        AB, GRAPH, 'НОВЫЙ СЛОЙ СЛАЙДА: BlendPoses и вес УЖЕ подключены, не хватает BasePose и Pose',
        [layer_id], 50.0, 0.25, 0.9, 0.3, 0.55))

# 3) the whole prepared branch
branch = [id_at(p) for p in ((-6800, 2000), (-6800, 2300), (-6400, 2000), (-6400, 2300),
                             (-6000, 2600), (-5600, 2600), (-5200, 2600), (-4800, 2600),
                             (-4300, 2600), (-3900, 2600))]
branch = [i for i in branch if i]
if branch:
    log('comment branch: %s' % BS.add_comment_around_nodes(
        AB, GRAPH, 'ВЕТКА СЛАЙДА: клипы и фазы In/Loop/Out. Готова, ждёт врезки', branch,
        60.0, 1.0, 0.95, 0.4, 0.5))

log('save: %s' % unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/comments.log', 'w', encoding='utf-8').write('\n'.join(L))
