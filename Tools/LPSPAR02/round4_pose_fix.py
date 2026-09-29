import unreal
import json
from pathlib import Path

AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world(), 'PIE must be off'
bp = unreal.load_asset(AB)
pkg = bp.get_outermost()
nodes = {n.get_name(): n for n in unreal.ObjectIterator(unreal.EdGraphNode)
         if n.get_outermost() == pkg and n.get_outer().get_name() == 'AnimGraph'}

def pin(name, key, incoming):
    return next(p for p in nodes[name].list_all_pins() if str(p.get_pin_name()) == key
                and (p.get_pin_direction() == unreal.EdGraphPinDirection.EGPD_INPUT) == incoming)

def wire(src, sp, dst, dp):
    target = pin(dst, dp, True)
    old = list(target.list_connected_pins())
    target.break_pin_links()
    if not target.try_create_connection(pin(src, sp, False)):
        for p in old:
            target.try_create_connection(p)
        raise RuntimeError('Connection rejected: ' + dst + '.' + dp)
    print('MODIFIED connection', src, sp, dst, dp)

def replace_inner(name, old, new):
    inner = nodes[name].get_editor_property('node')
    value = inner.export_text()
    assert old in value or new in value, (name, old)
    inner.import_text(value.replace(old, new))
    nodes[name].set_editor_property('node', inner)
    assert new in nodes[name].get_editor_property('node').export_text()
    print('MODIFIED', name, new)

# Preserve the base upper-body orientation in mesh space, then solve hands on the final pelvis.
wire('AnimGraphNode_UseCachedPose_19', 'Pose', 'AnimGraphNode_LayeredBoneBlend_6', 'BasePose')
wire('AnimGraphNode_LayeredBoneBlend_6', 'Pose', 'AnimGraphNode_LocalToComponentSpace_5', 'LocalPose')
reference=unreal.load_asset('/Game/InfimaGames/LowPolyShooterPack/Core/Characters/ABP_LPSP_TP_PCH')
reference_nodes={n.get_name():n for n in unreal.ObjectIterator(unreal.EdGraphNode)
    if n.get_outermost()==reference.get_outermost() and n.get_outer().get_name()=='AnimGraph'}
chain_edges=[]
def capture_chain(n,seen):
    if n in seen or n.get_name()=='AnimGraphNode_LocalToComponentSpace_5':return
    seen.add(n)
    for p in n.list_all_pins():
        if p.get_pin_direction()==unreal.EdGraphPinDirection.EGPD_INPUT and str(p.get_pin_name()) in ['ComponentPose','LocalPose','InputPin']:
            for q in p.list_connected_pins():
                chain_edges.append((q.get_owning_node().get_name(),str(q.get_pin_name()),n.get_name(),str(p.get_pin_name())))
                capture_chain(q.get_owning_node(),seen)
capture_chain(reference_nodes['AnimGraphNode_ComponentToLocalSpace_5'],set())
for edge in reversed(chain_edges):wire(*edge)
replace_inner('AnimGraphNode_LayeredBoneBlend_6', 'bMeshSpaceRotationBlend=False', 'bMeshSpaceRotationBlend=True')
# Use the actual state, not an asymptotically decaying alpha, to enter the exit clip.
wire('K2Node_CallFunction_2', 'ReturnValue', 'AnimGraphNode_BlendListByBool_4', 'bActiveValue')
for name in ['AnimGraphNode_BlendListByBool_3', 'AnimGraphNode_BlendListByBool_4', 'AnimGraphNode_BlendListByBool_6']:
    replace_inner(name, 'ChildUpateMode=Default', 'ChildUpateMode=ResetChildOnActivate')
replace_inner('AnimGraphNode_SequencePlayer_5', 'StartPosition=0.500000', 'StartPosition=0.000000')
replace_inner('AnimGraphNode_SequencePlayer_5', 'PlayRate=1.500000', 'PlayRate=1.000000')

# The pack's look blendspace uses pitch in degrees. Feed the existing network-aware getter.
marker = Path(unreal.Paths.project_saved_dir()) / 'LPSP_AR02/round4_aim_nodes.json'
if not marker.exists():
    result = unreal.BlueprintService.build_graph(AB, 'AnimGraph', [
        {'ref': 'AimPitch', 'type': 'function_call', 'params': {
            'class': 'PolarityCharacter', 'function': 'GetAimPitchForAnimation'}}
    ], [], [], False, False)
    assert result and not result.errors and not result.nodes_failed, str(result)
    ids = dict(result.ref_to_node_id)
    marker.write_text(json.dumps(ids), encoding='utf-8')
    print('CREATED aim getter', ids)
else:
    ids = json.loads(marker.read_text(encoding='utf-8'))
getter = next(n for n in unreal.ObjectIterator(unreal.EdGraphNode)
              if n.get_outermost() == pkg and n.get_outer().get_name() == 'AnimGraph'
              and hasattr(n, 'get_node_title') and 'GetAimPitchForAnimation' in str(n.get_node_title()))
nodes[getter.get_name()] = getter
wire('K2Node_DynamicCast_0', 'AsPolarity Character', getter.get_name(), 'self')
binding_class=unreal.load_class(None,'/Script/AnimGraph.AnimGraphNodeBinding_Base')
if not nodes['AnimGraphNode_BlendSpaceEvaluator_0'].get_editor_property('binding'):
    nodes['AnimGraphNode_BlendSpaceEvaluator_0'].set_editor_property('binding',unreal.new_object(binding_class,outer=nodes['AnimGraphNode_BlendSpaceEvaluator_0']))
wire(getter.get_name(), 'ReturnValue', 'AnimGraphNode_BlendSpaceEvaluator_0', 'X')
# This additive branch was only feeding a disconnected unarmed blend. Both armed and unarmed
# poses require the look adjustment, before the pack's turning/hand solvers.
wire('AnimGraphNode_UseCachedPose_2', 'Pose', 'AnimGraphNode_LinkedAnimGraph_2', 'Pose')
wire('AnimGraphNode_UseCachedPose_12', 'Pose', 'AnimGraphNode_TwoWayBlend_7', 'B')
look_marker=marker.with_name('round4_infima_look_nodes.json')
if not look_marker.exists():
    result=unreal.BlueprintService.build_graph(AB,'AnimGraph',[
        {'ref':'NegatePitch','type':'function_call','params':{'class':'KismetMathLibrary','function':'Multiply_DoubleDouble'}},
        {'ref':'Look','type':'function_call','params':{'class':'KismetMathLibrary','function':'MakeRotator'}}],[],[],False,False)
    assert result and not result.errors and not result.nodes_failed,str(result)
    look_marker.write_text(json.dumps(dict(result.ref_to_node_id)),encoding='utf-8')
    print('CREATED Infima look driver',dict(result.ref_to_node_id))
nodes.update({n.get_name():n for n in unreal.ObjectIterator(unreal.EdGraphNode)
    if n.get_outermost()==pkg and n.get_outer().get_name()=='AnimGraph'})
newids=json.loads(look_marker.read_text(encoding='utf-8'))
# GetNode IDs are stable GUIDs; use the service for these newly-created, saved nodes.
def connect_id(a,ap,b,bp):
    assert unreal.BlueprintService.connect_nodes(AB,'AnimGraph',a,ap,b,bp)
connect_id(ids['AimPitch'],'ReturnValue',newids['NegatePitch'],'A')
assert unreal.BlueprintService.set_node_pin_value(AB,'AnimGraph',newids['NegatePitch'],'B','-1')
connect_id(newids['NegatePitch'],'ReturnValue',newids['Look'],'Roll')
look_node=next(n for n in nodes.values() if hasattr(n,'get_node_title') and str(n.get_node_title())=='MakeRotator')
wire(look_node.get_name(),'ReturnValue','AnimGraphNode_LinkedAnimGraph_2','Look')
# The unarmed asset stores yaw across normalized time; center it for the retained unarmed branch.
assert pin('AnimGraphNode_BlendSpaceEvaluator_0','NormalizedTime',True).set_pin_value('0.5')
# The integration has no pack look component to drive the legacy bypass binding.
# Select the corrected look path explicitly instead of the untouched solved-state pose.
bypass = nodes['AnimGraphNode_TwoWayBlend_7']
if not bypass.get_editor_property('binding'):
    bypass.set_editor_property('binding',unreal.new_object(binding_class,outer=bypass))
inner = bypass.get_editor_property('blend_node')
inner.import_text(inner.export_text().replace('bAlphaBoolEnabled=True', 'bAlphaBoolEnabled=False'))
bypass.set_editor_property('blend_node', inner)
assert pin('AnimGraphNode_TwoWayBlend_7','bAlphaBoolEnabled',True).set_pin_value('False')
print('MODIFIED Infima armed look branch: Roll = -view pitch, yaw centered')
# Null bindings suppress compilation of connected value pins too. Restore missing handlers
# on the three aiming gates cleaned by phase1, preserving every existing non-null binding.
for n in unreal.ObjectIterator(unreal.AnimGraphNode_BlendListByBool):
    if n.get_outermost()==pkg and not n.get_editor_property('binding'):
        n.set_editor_property('binding',unreal.new_object(binding_class,outer=n))
        print('RESTORED pin evaluation handler',n.get_path_name())
unreal.BlueprintEditorLibrary.compile_blueprint(bp)
assert unreal.EditorAssetLibrary.save_asset(AB, False)
print('SAVED round4 pose changes')
