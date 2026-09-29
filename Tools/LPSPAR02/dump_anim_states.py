import unreal

# Dumps every anim-graph node of an AnimBP (including state and transition graphs) with its pin links.
# Pin links come from EdGraphNode.list_all_pins() -> BlueprintGraphPin.list_connected_pins(),
# which reaches inside state graphs where BlueprintService.get_connections does not.
# Read-only. Output: Saved/LPSP_AR02/states_<asset>.txt


def label(n):
    try:
        t = str(n.get_node_title())
    except Exception:
        t = ''
    return (t or '').replace('\r', '').replace('\n', ' | ')


def asset_of(n):
    try:
        inner = n.get_editor_property('node')
    except Exception:
        return ''
    for p in ('sequence', 'blend_space'):
        try:
            v = inner.get_editor_property(p)
            if v:
                return ' asset=' + v.get_name()
        except Exception:
            pass
    return ''


def main(path):
    pkg = unreal.load_asset(path).get_outermost()
    by_graph = {}
    for n in unreal.ObjectIterator(unreal.EdGraphNode):
        if n.get_outermost() != pkg:
            continue
        if not hasattr(n, 'list_all_pins'):
            continue
        g = n.get_outer()
        by_graph.setdefault(g.get_path_name(), []).append(n)
    lines = []
    for gp in sorted(by_graph):
        lines.append('=== ' + gp.split(':', 1)[-1])
        for n in by_graph[gp]:
            lines.append('N %s  %s%s' % (n.get_name(), label(n), asset_of(n)))
            for p in n.list_all_pins():
                links = p.list_connected_pins()
                if not links:
                    continue
                if p.get_pin_direction() != unreal.EdGraphPinDirection.EGPD_OUTPUT:
                    continue
                for q in links:
                    lines.append('   %s -> %s.%s' % (p.get_pin_name(), q.get_owning_node().get_name(), q.get_pin_name()))
    out = unreal.Paths.project_saved_dir() + 'LPSP_AR02/states_' + path.split('/')[-1] + '.txt'
    open(out, 'w', encoding='utf-8').write('\n'.join(lines))
    print('WROTE', out, len(lines))
