import unreal, json

# Move the slide layer behind the IK chain using connect_nodes with GUIDs resolved by graph position.
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
BS = unreal.BlueprintService
TOOLS = 'editor_toolset.toolsets.blueprint.BlueprintTools'
L = []


def log(m):
    L.append(str(m))
    print(m)


nodes = {}
for n in BS.get_nodes_in_graph(AB, GRAPH):
    d = BS.get_node_details(AB, GRAPH, n.node_id)
    nodes[(round(d.pos_x), round(d.pos_y))] = (n.node_id, d.node_title)

# our slide branch was created at y = -3600 with these x positions
OURS = {'PhaseGate': -2500, 'HoldGate': -2000, 'MaskLayer': -1500, 'ExitGate': -1200, 'OuterGate': -1000}
ids = {}
for tag, x in OURS.items():
    hit = nodes.get((x, -3600))
    if hit:
        ids[tag] = hit[0]
log('our nodes: %s' % {k: v[:8] for k, v in ids.items()})

# the IK chain: last FABRIK and the Component To Local that feeds Output Pose (both at y = 2480)
fab = nodes.get((-3279, 2480))
ctl = nodes.get((-3018, 2480))
log('Fabrik_8: %s  ComponentToLocalSpace_5: %s' % (fab[0][:8] if fab else None, ctl[0][:8] if ctl else None))


def conn(src, src_pin, dst, dst_pin, tag=''):
    if not src or not dst:
        log('missing ids for %s' % tag)
        return False
    ok = BS.connect_nodes(AB, GRAPH, src, src_pin, dst, dst_pin)
    log('%s: %s.%s -> %s.%s : %s' % (tag, src[:8], src_pin, dst[:8], dst_pin, ok))
    return ok


if fab and ctl and ids.get('MaskLayer') and ids.get('OuterGate'):
    conn(fab[0], 'Pose', ids['MaskLayer'], 'BasePose', 'layer base')
    conn(fab[0], 'Pose', ids['OuterGate'], 'BlendPose_0', 'gate base')
    conn(ids['OuterGate'], 'Pose', ctl[0], 'ComponentPose', 'gate into output')

log('--- verify ---')
pkg = unreal.load_asset(AB).get_outermost()


def find(name):
    for n in unreal.ObjectIterator(unreal.EdGraphNode):
        if n.get_outermost() == pkg and n.get_outer() and n.get_outer().get_name() == GRAPH and n.get_name() == name:
            return n
    return None


for nm in ('AnimGraphNode_Fabrik_8', 'AnimGraphNode_ComponentToLocalSpace_5', 'AnimGraphNode_LayeredBoneBlend_3',
           'AnimGraphNode_BlendListByBool_5'):
    n = find(nm)
    log('%s:' % nm)
    for p in n.list_all_pins():
        q = p.list_connected_pins()
        if q or p.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_OUTPUT:
            log('   %-14s %s' % (str(p.get_pin_name()), [x.get_owning_node().get_name() for x in q]))

r = unreal.ToolsetRegistry.execute_tool(TOOLS, 'compile_blueprint', json.dumps({'blueprint': {'refPath': AB + '.ABP_AR02_LPSP_Test'}}))
log('compile: %s %s' % (r.is_complete, r.error))
log('save: %s' % unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/step1c.log', 'w', encoding='utf-8').write('\n'.join(L))
