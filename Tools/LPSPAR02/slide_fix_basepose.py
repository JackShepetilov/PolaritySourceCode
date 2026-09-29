import unreal, json

BS = unreal.BlueprintService
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
TOOLS = 'editor_toolset.toolsets.blueprint.BlueprintTools'
pkg = unreal.load_asset(AB).get_outermost()
JSON = unreal.Paths.project_saved_dir() + 'LPSP_AR02/slide_nodes.json'
W = json.load(open(JSON, encoding='utf-8'))


def find(name):
    for n in unreal.ObjectIterator(unreal.EdGraphNode):
        if n.get_outermost() == pkg and n.get_outer() and n.get_outer().get_name() == GRAPH and n.get_name() == name:
            return n
    return None


def get_pin(node, name, input_side):
    for p in node.list_all_pins():
        if str(p.get_pin_name()) == name and (p.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_INPUT) == input_side:
            return p
    return None


uc = find('AnimGraphNode_UseCachedPose_19')
ml = find(W['MaskLayer'])
print('BasePose links now:', len(get_pin(ml, 'BasePose', True).list_connected_pins()))
ok = get_pin(uc, 'Pose', False).try_create_connection(get_pin(ml, 'BasePose', True))
print('restored BasePose <- CPose Procedural Spine Look:', ok)

print('\nfull pin state of the layer:')
for p in ml.list_all_pins():
    links = ['%s.%s' % (q.get_owning_node().get_name(), q.get_pin_name()) for q in p.list_connected_pins()]
    print('   %-14s %-4s %s' % (str(p.get_pin_name()),
                                'IN' if p.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_INPUT else 'OUT',
                                ', '.join(links) or '-'))

r = unreal.ToolsetRegistry.execute_tool(TOOLS, 'compile_blueprint', json.dumps({'blueprint': {'refPath': AB + '.ABP_AR02_LPSP_Test'}}))
print('\ncompile:', r.is_complete, r.error)
print('status:', unreal.load_asset(AB).get_editor_property('status'))
print('saved:', unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
