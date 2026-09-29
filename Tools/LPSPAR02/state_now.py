import unreal, json

# Current wiring of the slide branch, and what the outer gate reacts to.
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
pkg = unreal.load_asset(AB).get_outermost()
JSON = unreal.Paths.project_saved_dir() + 'LPSP_AR02/slide_nodes.json'
W = json.load(open(JSON, encoding='utf-8'))
L = []


def log(m):
    L.append(str(m))
    print(m)


def find(name):
    for n in unreal.ObjectIterator(unreal.EdGraphNode):
        if n.get_outermost() == pkg and n.get_outer() and n.get_outer().get_name() == GRAPH and n.get_name() == name:
            return n
    return None


for nm in ('AnimGraphNode_Fabrik_8', 'AnimGraphNode_ComponentToLocalSpace_5',
           W['MaskLayer'], W['OuterGate'], W['HoldGate'], W['PhaseGate']):
    n = find(nm)
    if not n:
        log('%s MISSING' % nm)
        continue
    log('%s:' % nm)
    for p in n.list_all_pins():
        q = [x.get_owning_node().get_name() for x in p.list_connected_pins()]
        if q or p.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_OUTPUT:
            log('   %-14s %s' % (str(p.get_pin_name()), q))

# what does the outer gate listen to?
og = find(W['OuterGate'])
if og:
    for p in og.list_all_pins():
        if str(p.get_pin_name()) == 'bActiveValue':
            src = [x.get_owning_node().get_name() for x in p.list_connected_pins()]
            log('outer gate active <- %s' % src)
            for s in src:
                o = find(s)
                if o:
                    log('   source title: %s' % str(o.get_node_title()).replace('\n', ' ')[:60])
open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/state_now.log', 'w', encoding='utf-8').write('\n'.join(L))
