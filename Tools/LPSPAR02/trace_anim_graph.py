import unreal

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
asset = unreal.load_asset(AB)
pkg = asset.get_outermost()

# Trace back from Root Node
root_node = None
for n in unreal.ObjectIterator(unreal.EdGraphNode):
    if n.get_outermost() == pkg and 'AnimGraphNode_Root' in n.get_name():
        root_node = n
        break

print('Root Node:', root_node.get_name() if root_node else 'None')

visited = set()
def trace_back(node, depth=0):
    if not node or node in visited or depth > 15:
        return
    visited.add(node)
    title = str(node.get_node_title()).replace('\r', '').replace('\n', ' ') if hasattr(node, 'get_node_title') else ''
    cname = node.get_class().get_name()
    print('  ' * depth + '-> %s (%s) [%s]' % (node.get_name(), cname, title))
    
    for p in node.list_all_pins():
        if p.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_INPUT:
            pname = str(p.get_pin_name()).lower()
            if any(k in pname for k in ('pose', 'result', 'base', 'blend', 'inputpin')):
                for link in p.list_connected_pins():
                    src = link.get_owning_node()
                    print('  ' * (depth + 1) + '|-- Pin: %s <- %s.%s' % (p.get_pin_name(), src.get_name(), link.get_pin_name()))
                    trace_back(src, depth + 1)

trace_back(root_node)
