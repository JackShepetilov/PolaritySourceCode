import unreal
import json
from pathlib import Path

BASE = '/Game/Variant_Shooter/Tests/LPSP_AR02'
AB = BASE + '/ABP_AR02_LPSP_Test'
BS = unreal.BlueprintService
OUT = Path(unreal.Paths.project_saved_dir()) / 'LPSP_AR02'
OUT.mkdir(parents=True, exist_ok=True)

def build(graph, nodes, connections=(), defaults=()):
    r = BS.build_graph(AB, graph, nodes, list(connections), list(defaults), False, False)
    assert r and not r.errors and r.nodes_failed == 0 and r.connections_failed == 0, str(r)
    print('MODIFIED:', AB, graph, 'nodes', r.nodes_created, 'connections', r.connections_made)
    return dict(r.ref_to_node_id)

def node(ref, kind, **params):
    return dict(ref=ref, type=kind, params=params)

def wire(a, b):
    return {'from_': a, 'to': b}

def find(graph, title):
    matches = [n for n in BS.get_nodes_in_graph(AB, graph) if n.node_title.replace('\\n', '\n').splitlines()[0] == title]
    assert len(matches) == 1, (graph, title, len(matches))
    return matches[0].node_id

def redirect_output(graph, old, new, new_pin, old_pin='ReturnValue'):
    edges = [c for c in BS.get_connections(AB, graph) if c.source_node_id == old and c.source_pin_name == old_pin]
    assert edges, (graph, old, old_pin)
    BS.disconnect_pin(AB, graph, old, old_pin)
    for e in edges:
        assert BS.connect_nodes(AB, graph, new, new_pin, e.target_node_id, e.target_pin_name)

def retain(graph, message, variable):
    old = find(graph, message)
    outputs = [p.pin_name for p in BS.get_node_pins(AB, graph, old) if not p.is_input and p.pin_type != 'exec' and p.is_connected]
    if not outputs:
        print('ALREADY ADAPTED:', graph, message)
        return
    assert len(outputs) == 1, (graph, message, outputs)
    ids = build(graph, [node('PolarityValue', 'variable_get', variable=variable)])
    redirect_output(graph, old, ids['PolarityValue'], variable, outputs[0])

def prepare_defaults(refsettings):
    cd = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(AB))
    for dst, src in [('Settings Animation', 'Settings Animation'), ('Settings Scope', 'Scope Settings'), ('Settings Grip', 'Grip Settings')]:
        cd.set_editor_property(dst, refsettings[src])
        print('MODIFIED:', AB, dst)
    text = refsettings['Weapon Settings'].export_text()
    start = text.index('=(', text.index('RecoilStateStanding_')) + 1
    depth = 0
    end = start
    for end in range(start, len(text)):
        depth += (text[end] == '(') - (text[end] == ')')
        if depth == 0:
            break
    r = cd.get_editor_property('Recoil State Weapon')
    r.import_text(text[start:end+1])
    cd.set_editor_property('Recoil State Weapon', r)
    cd.set_editor_property('Data Table Animation Poses', unreal.load_asset('/Game/InfimaGames/LowPolyShooterPack/Data/Weapons/Animations/DT_LPSP_CH_AR_02_Poses'))
    cd.set_editor_property('Third Person', True)
    cd.set_editor_property('Aiming', True)
    # Snapshot the author's resolved settings, including their original spring values.
    (OUT / 'ar02_reference_settings.json').write_text(json.dumps({k:v.export_text() for k,v in refsettings.items()}, indent=2), encoding='utf-8')

def constant_inputs():
    for args in [
        ('EventGraph', 'LPSP - Get Character Animation Poses', 'Data Table Animation Poses'),
        ('EventGraph', 'LPSP - Get Settings Animation', 'Settings Animation'),
        ('EventGraph', 'LPSP - Get Leaning Alpha', 'Lean Alpha'),
        ('CGraph Cache Values Weapon', 'LPSP - Get Equipped Item', 'Actor Weapon'),
        ('CGraph Cache Values Pawn', 'LPSP - Is Third Person', 'Third Person'),
        ('CGraph Cache Values Pawn', 'LPSP - Is Aiming', 'Aiming'),
        ('Update Basics', 'LPSP - Get Settings Grip', 'Settings Grip'),
        ('Update Recoil Values', 'LPSP - Get Recoil State Weapon Standing', 'Recoil State Weapon'),
        ('Update Recoil Values', 'LPSP - Get Shot Count', 'Shot Count'),
    ]:
        retain(*args)

def movement_inputs():
    graph = 'CGraph Cache Values Pawn'
    old = find(graph, 'LPSP - Is Running')
    if not any(c.source_node_id == old and c.source_pin_name == 'ReturnValue'
               for c in BS.get_connections(AB, graph)):
        print('ALREADY ADAPTED:', graph, 'LPSP - Is Running')
        movement_axes()
        return
    upstream = [c for c in BS.get_connections(AB, graph) if c.target_node_id == old and c.target_pin_name == 'execute']
    assert len(upstream) == 1
    ids = build(graph, [
        node('OwnerForSprint', 'variable_get', variable='Owner'),
        node('MovementForSprint', 'member_get', member='CharacterMovement', **{'class':'Character'}),
        node('ApexForSprint', 'cast', target_class='/Script/Polarity.ApexMovementComponent'),
        node('NativeSprint', 'function_call', **{'class':'ApexMovementComponent', 'function':'IsSprinting'}),
    ], [wire('OwnerForSprint.Owner','MovementForSprint.self'), wire('MovementForSprint.CharacterMovement','ApexForSprint.Object'), wire('ApexForSprint.AsApex Movement Component','NativeSprint.self')])
    BS.disconnect_pin(AB, graph, old, 'execute')
    e = upstream[0]
    assert BS.connect_nodes(AB,graph,e.source_node_id,e.source_pin_name,ids['ApexForSprint'],'execute')
    assert BS.connect_nodes(AB,graph,ids['ApexForSprint'],'then',old,'execute')
    redirect_output(graph,old,ids['NativeSprint'],'ReturnValue')
    movement_axes()

def movement_axes():
    # LPSP's input axes are X=right, Y=forward. AI has velocity, not local player input.
    for graph in ['CGraph Update Input Values','CGraph Get Input Movement']:
        old = find(graph,'LPSP - Get Input Movement')
        if not any(c.source_node_id == old and c.source_pin_name == 'ReturnValue'
                   for c in BS.get_connections(AB, graph)):
            print('ALREADY ADAPTED:', graph, 'LPSP - Get Input Movement')
            continue
        ids = build(graph,[
            node('MotionOwner','variable_get',variable='Owner'),
            node('MotionVelocity','function_call',**{'class':'Actor','function':'GetVelocity'}),
            node('MotionTransform','function_call',**{'class':'Actor','function':'GetTransform'}),
            node('LocalVelocity','function_call',**{'class':'KismetMathLibrary','function':'InverseTransformDirection'}),
            node('NormalVelocity','function_call',**{'class':'KismetMathLibrary','function':'Vector_Normal2D'}),
            node('BreakLocal','function_call',**{'class':'KismetMathLibrary','function':'BreakVector'}),
            node('MotionInput','function_call',**{'class':'KismetMathLibrary','function':'MakeVector2D'}),
        ],[
            wire('MotionOwner.Owner','MotionVelocity.self'),wire('MotionOwner.Owner','MotionTransform.self'),
            wire('MotionVelocity.ReturnValue','LocalVelocity.Direction'),wire('MotionTransform.ReturnValue','LocalVelocity.T'),
            wire('LocalVelocity.ReturnValue','NormalVelocity.A'),wire('NormalVelocity.ReturnValue','BreakLocal.InVec'),
            wire('BreakLocal.Y','MotionInput.X'),wire('BreakLocal.X','MotionInput.Y'),
        ])
        redirect_output(graph,old,ids['MotionInput'],'ReturnValue')

def main(refsettings):
    assert not (OUT/'adapter_done.json').exists(), 'Adapter already configured; do not add nodes twice.'
    prepare_defaults(refsettings)
    constant_inputs()
    movement_inputs()
    assert unreal.BlueprintEditorLibrary.compile_blueprint(unreal.load_asset(AB)), 'Adapter compilation failed'
    assert unreal.EditorAssetLibrary.save_asset(AB,False)
    (OUT/'adapter_done.json').write_text(json.dumps({'asset':AB}),encoding='utf-8')
    print('VERIFIED: adapter compiled and saved')
