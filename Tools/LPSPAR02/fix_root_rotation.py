import unreal, json

# Fix for "the body never turns": AnimGraphNode_ModifyBone_27 (bone 'root') has rotation_mode = REPLACE
# in WORLD space with alpha 1, and its Rotation pin is driven by a BINDING whose source (the pack's
# BPSC_LPSP_TP_Looks component) does not exist on our NPC. The binding therefore yields 0 and the root
# bone is pinned to world identity in every frame (measured: root yaw = 0 across 449 PIE frames while
# the actor turned). Setting rotation_mode to IGNORE lets the root follow the mesh component again.
# Idempotent: reports if the mode already differs from REPLACE.
# Run: Tools/mcp.sh py Source/Tools/LPSPAR02/run.py

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
NODE = 'AnimGraphNode_ModifyBone_27'
TOOLS = 'editor_toolset.toolsets.blueprint.BlueprintTools'

asset = unreal.load_asset(AB)
graph = unreal.find_object(None, AB + '.' + AB.split('/')[-1] + ':AnimGraph')
print('graph:', graph)
n = unreal.find_object(graph, NODE)
print('node:', n)
inner = n.get_editor_property('node')
print('before: rotation_mode=%s space=%s alpha=%s' % (inner.get_editor_property('rotation_mode'),
                                                      inner.get_editor_property('rotation_space'),
                                                      inner.get_editor_property('alpha')))
mode = inner.get_editor_property('rotation_mode')
if mode == unreal.BoneModificationMode.BMM_IGNORE:
    print('ALREADY IGNORE, nothing to do')
else:
    inner.set_editor_property('rotation_mode', unreal.BoneModificationMode.BMM_IGNORE)
    n.set_editor_property('node', inner)
    after = n.get_editor_property('node')
    print('after: rotation_mode=%s space=%s alpha=%s' % (after.get_editor_property('rotation_mode'),
                                                         after.get_editor_property('rotation_space'),
                                                         after.get_editor_property('alpha')))
    r = unreal.ToolsetRegistry.execute_tool(TOOLS, 'compile_blueprint', json.dumps({'blueprint': {'refPath': AB + '.' + AB.split('/')[-1]}}))
    print('compile:', r.is_complete, r.error)
    print('status:', unreal.load_asset(AB).get_editor_property('status'))
    print('saved:', unreal.EditorAssetLibrary.save_asset(AB, only_if_is_dirty=False))
