import unreal, json

# Fixed mask semantics: branch filters KEEP the base pose, everything else takes the layer pose.
# So the filter must name what ignores the slide (torso, arms, hand IK) - same set the pack uses for
# its crouch transitions. Legs are deliberately absent, so they take the slide.
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
GRAPH = 'AnimGraph'
LAYER = 'C34CDA6C496FABF30DF1F99E4E503921'
TOOLS = 'editor_toolset.toolsets.blueprint.BlueprintTools'
pkg = unreal.load_asset(AB).get_outermost()
L = []


def log(m):
    L.append(str(m))
    print(m)


r = unreal.ToolsetRegistry.execute_tool('EditorToolset.EditorAppToolset', 'StopPIE', '{"options": {}}')
log('StopPIE: %s' % r.is_complete)


def find(name):
    for n in unreal.ObjectIterator(unreal.EdGraphNode):
        if n.get_outermost() == pkg and n.get_outer() and n.get_outer().get_name() == GRAPH and n.get_name() == name:
            return n
    return None


layer = find('AnimGraphNode_LayeredBoneBlend_6')
log('layer: %s' % (layer is not None))
if layer:
    mi = layer.get_editor_property('node')
    log('mask before: %s' % mi.get_editor_property('layer_setup')[0].export_text())
    pose = unreal.InputBlendPose()
    pose.import_text('(BranchFilters=((BoneName="root"),(BoneName="spine_01",BlendDepth=-1),(BoneName="ik_hand_root",BlendDepth=-1)))')
    mi.set_editor_property('layer_setup', [pose])
    layer.set_editor_property('node', mi)
    log('mask after : %s' % layer.get_editor_property('node').get_editor_property('layer_setup')[0].export_text())

r = unreal.ToolsetRegistry.execute_tool(TOOLS, 'compile_blueprint', json.dumps({'blueprint': {'refPath': AB + '.ABP_AR02_LPSP_Test'}}))
log('compile: %s %s' % (r.is_complete, r.error))
log('world: %s' % (unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None))
log('save: %s' % unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
open(unreal.Paths.project_saved_dir() + 'LPSP_AR02/mask_fix.log', 'w', encoding='utf-8').write('\n'.join(L))
