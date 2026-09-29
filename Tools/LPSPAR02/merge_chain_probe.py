import unreal

# Neighbourhood of the standing/aiming merge, so the DefaultSlot node can go into one place that covers both.

ABP = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/merge_chain.txt'
ROOTS = ('AnimGraphNode_BlendListByBool_0', 'AnimGraphNode_SaveCachedPose_36', 'AnimGraphNode_SaveCachedPose_35')

blueprint = unreal.EditorAssetLibrary.load_asset(ABP)
package = blueprint.get_outermost()
nodes = {}
for node in unreal.ObjectIterator(unreal.EdGraphNode):
    if node.get_outermost() == package:
        nodes[node.get_name()] = node

lines = []
seen = set()
queue = [(name, 0) for name in ROOTS if name in nodes]
while queue:
    name, depth = queue.pop(0)
    if name in seen or depth > 4:
        continue
    seen.add(name)
    node = nodes[name]
    try:
        title = str(node.get_node_title()).replace('\n', ' / ')
    except Exception:
        title = '<no title>'
    lines.append('%sNODE %s | %s | graph=%s' % ('  ' * depth, name, title, node.get_outer().get_name()))
    for pin in node.list_all_pins():
        try:
            links = [(p.get_owning_node().get_name(), p.get_pin_name()) for p in pin.list_connected_pins()]
        except Exception:
            links = []
        if links:
            lines.append('%s   %-20s -> %s' % ('  ' * depth, pin.get_pin_name(),
                                               ', '.join('%s.%s' % l for l in links)))
        if depth < 4 and str(pin.get_pin_name()).lower() not in ('execute', 'then'):
            for owner, _ in links:
                if owner in nodes:
                    queue.append((owner, depth + 1))
with open(OUT, 'w', encoding='utf-8') as handle:
    handle.write('\n'.join(lines))
print('MERGE CHAIN DONE', len(lines), OUT)
