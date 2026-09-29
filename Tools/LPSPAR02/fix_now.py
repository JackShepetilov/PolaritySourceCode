import unreal, json

# URGENT: restore Fabrik_8.Pose -> ComponentToLocalSpace_5.ComponentPose (the graph is split right now).
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
BS = unreal.BlueprintService
pkg = unreal.load_asset(AB).get_outermost()
L = []


def log(m):
    L.append(str(m))
    print(m)


def find(name):
    for n in unreal.ObjectIterator(unreal.EdGraphNode):
        if n.get_outermost() == pkg and n.get_outer() and n.get_outer().get_name() == GRAPH and n.get_name() == name:
            return n
    return None


def pin(node, name, input_side):
    for p in node.list_all_pins():
        if str(p.get_pin_name()) == name and (p.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_INPUT) == input_side:
            return p
    return None


s = find('AnimGraphNode_Fabrik_8')
d = find('AnimGraphNode_ComponentToLocalSpace_5')
a = pin(s, 'Pose', False)
b = pin(d, 'ComponentPose', True)
log('pins: src=%s dst=%s, src links=%d dst links=%d' % (a is not None, b is not None,
                                                        len(a.list_connected_pins()), len(b.list_connected_pins())))
log('try a->b: %s' % a.try_create_connection(b))
log('try b->a: %s' % b.try_create_connection(a))
log('after: src links=%d dst links=%d' % (len(a.list_connected_pins()), len(b.list_connected_pins())))

# node ids from the graph service, matched by title
ids = []
for n in BS.get_nodes_in_graph(AB, GRAPH):
    t = n.node_title.replace('\n', ' ')
    if t.startswith('FABRIK') or t.startswith('Component To Local') or t.startswith('Local To Component'):
        ids.append((n.node_id, t[:30]))
log('candidate node ids: %s' % ids)

log('world: %s' % (unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None))
open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/fix_now.log', 'w', encoding='utf-8').write('\n'.join(L))
