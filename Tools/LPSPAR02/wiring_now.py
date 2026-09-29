import unreal

# Dump exactly how the output path and the new layer are wired right now.
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
BS = unreal.BlueprintService
L = []


def log(m):
    L.append(str(m))
    print(m)


conns = BS.get_connections(AB, GRAPH)
titles = {}
for c in conns:
    titles[c.source_node_id] = c.source_node_title
    titles[c.target_node_id] = c.target_node_title

def dump(nid, tag):
    log('=== %s %s (%s)' % (tag, nid[:8] if nid else None, titles.get(nid, '?').replace('\n', ' ')[:40]))
    for c in conns:
        if c.target_node_id == nid:
            log('   IN  %-14s <- %s (%s)' % (c.target_pin_name, c.source_node_id[:8], c.source_node_title.replace('\n', ' ')[:40]))
        if c.source_node_id == nid:
            log('   OUT %-14s -> %s (%s)' % (c.source_pin_name, c.target_node_id[:8], c.target_node_title.replace('\n', ' ')[:40]))


# the node that feeds Output Pose
root_in = [c for c in conns if c.target_pin_name == 'Result']
ctl = root_in[0].source_node_id if root_in else None
dump(ctl, 'Component To Local -> Output Pose')

# everything that feeds that node
if ctl:
    for c in conns:
        if c.target_node_id == ctl:
            dump(c.source_node_id, 'feeds it')

# the new layer
layer = 'C34CDA6C496FABF30DF1F99E4E503921'
dump(layer, 'Layered blend per bone (new)')

# every Component To Local / Local To Component node in the graph
for c in conns:
    for nid, t in ((c.source_node_id, c.source_node_title), (c.target_node_id, c.target_node_title)):
        if t.replace('\n', ' ').startswith(('Component To Local', 'Local To Component')):
            if nid not in globals().get('_seen', set()):
                pass
seen = set()
for c in conns:
    for nid, t in ((c.source_node_id, c.source_node_title), (c.target_node_id, c.target_node_title)):
        if t.replace('\n', ' ').startswith(('Component To Local', 'Local To Component')) and nid not in seen:
            seen.add(nid)
            dump(nid, 'space node')
open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/wiring_now.log', 'w', encoding='utf-8').write('\n'.join(L))
