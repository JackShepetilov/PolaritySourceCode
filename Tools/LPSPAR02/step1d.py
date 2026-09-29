import unreal, json

# Undo the bogus links created by connect_nodes and restore the main pose line.
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
BS = unreal.BlueprintService
TOOLS = 'editor_toolset.toolsets.blueprint.BlueprintTools'
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


def cut(name, pin_name):
    n = find(name)
    p = pin(n, pin_name, True) if n else None
    if not p:
        log('cut %s.%s : MISSING' % (name, pin_name))
        return
    before = [q.get_owning_node().get_name() for q in p.list_connected_pins()]
    p.break_pin_links()
    log('cut %s.%s (was %s)' % (name, pin_name, before))


# 1) remove the links connect_nodes made into the wrong receivers
cut('AnimGraphNode_ComponentToLocalSpace_1', 'ComponentPose')
cut('AnimGraphNode_LayeredBoneBlend_3', 'BasePose')
cut('AnimGraphNode_BlendListByBool_5', 'BlendPose_0')
cut('AnimGraphNode_LocalToComponentSpace_0', 'LocalPose')

# 2) restore the main line: last FABRIK -> Component To Local -> Output
fab = find('AnimGraphNode_Fabrik_8')
ctl = find('AnimGraphNode_ComponentToLocalSpace_5')
a = pin(fab, 'Pose', False)
b = pin(ctl, 'ComponentPose', True)
log('restore main line: %s' % b.try_create_connection(a))
log('after: Fabrik_8.Pose -> %s, CTL_5.ComponentPose <- %s' % (
    [q.get_owning_node().get_name() for q in a.list_connected_pins()],
    [q.get_owning_node().get_name() for q in b.list_connected_pins()]))

log('--- verify key nodes ---')
for nm in ('AnimGraphNode_Fabrik_8', 'AnimGraphNode_ComponentToLocalSpace_5', 'AnimGraphNode_LocalToComponentSpace_5',
           'AnimGraphNode_UseCachedPose_19', 'AnimGraphNode_LayeredBoneBlend_3', 'AnimGraphNode_BlendListByBool_5'):
    n = find(nm)
    log('%s:' % nm)
    for p in n.list_all_pins():
        q = p.list_connected_pins()
        if q or p.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_OUTPUT:
            log('   %-14s %s' % (str(p.get_pin_name()), [x.get_owning_node().get_name() for x in q]))

r = unreal.ToolsetRegistry.execute_tool(TOOLS, 'compile_blueprint', json.dumps({'blueprint': {'refPath': AB + '.ABP_AR02_LPSP_Test'}}))
log('compile: %s %s' % (r.is_complete, r.error))
log('save: %s' % unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/step1d.log', 'w', encoding='utf-8').write('\n'.join(L))
