import unreal

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
asset = unreal.load_asset(AB)
pkg = asset.get_outermost()

# A) Full backward trace from Output Pose, deep, printing only the spine of pose nodes
root_node = None
events = []
for n in unreal.ObjectIterator(unreal.EdGraphNode):
    if n.get_outermost() != pkg:
        continue
    if 'AnimGraphNode_Root' in n.get_name():
        root_node = n
    cname = n.get_class().get_name()
    if cname in ('K2Node_Event', 'K2Node_CustomEvent', 'EdGraphNode'):
        try:
            events.append('%s [%s] in %s' % (n.get_name(), str(n.get_node_title()).replace('\n', ' '), n.get_outer().get_name()))
        except Exception:
            pass

print('=== EVENTS in ABP ===')
for e in events:
    print('  ', e)

print('\n=== POSE SPINE from Output Pose (deep) ===')
visited = set()
chain = []


def trace(node, depth):
    if not node or depth > 60:
        return
    if node in visited:
        return
    visited.add(node)
    cname = node.get_class().get_name()
    title = str(node.get_node_title()).replace('\r', '').replace('\n', ' ') if hasattr(node, 'get_node_title') else ''
    if not cname.startswith('K2Node_Knot'):
        chain.append('%4d %s | %s | %s' % (depth, node.get_name(), cname.replace('AnimGraphNode_', ''), title[:60]))
    # follow first pose-ish input
    for p in node.list_all_pins():
        if p.get_pin_direction() != unreal.EdGraphPinDirection.EGPD_INPUT:
            continue
        nm = str(p.get_pin_name()).lower()
        if any(k in nm for k in ('pose', 'result', 'base', 'blend', 'inputpin', 'componentpose')):
            links = p.list_connected_pins()
            if links:
                trace(links[0].get_owning_node(), depth + 1)
                break


trace(root_node, 0)
for c in chain:
    print(c)
