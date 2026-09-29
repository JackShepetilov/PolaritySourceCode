import unreal
import json
from pathlib import Path
AB='/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
BS=unreal.BlueprintService
OUT=Path(unreal.Paths.project_saved_dir())/'LPSP_AR02'

def make_event(name, inputs, nodes, wires):
    matches=[n.node_id for n in BS.get_nodes_in_graph(AB,'EventGraph') if n.node_title.replace('\\n','\n').splitlines()[0]==name]
    if matches:
        print('EXISTS event',name)
        return matches[0]
    event=BS.add_custom_event_node(AB,'EventGraph',name)
    assert event
    print('CREATED event',name,event)
    for key,typ in inputs:
        assert BS.add_custom_event_input(AB,'EventGraph',event,key,typ)
    r=BS.build_graph(AB,'EventGraph',nodes,[],[],False,False)
    assert r and not r.errors and not r.nodes_failed,str(r)
    ids=dict(r.ref_to_node_id);ids['Event']=event
    (OUT/(name+'_nodes.json')).write_text(json.dumps(ids,indent=2),encoding='utf-8')
    for a,ap,b,bp in wires:
        assert BS.connect_nodes(AB,'EventGraph',ids[a],ap,ids[b],bp),(a,ap,b,bp)
    print('MODIFIED event connections',name,len(wires))
    return event

make_event('PolarityRegisterWeapon',[('Weapon','Actor'),('Firing','bool')],[
    {'ref':'SetWeapon','type':'variable_set','params':{'variable':'Actor Weapon'}},
    {'ref':'Branch','type':'branch','params':{}},
    {'ref':'ResetShots','type':'variable_set','params':{'variable':'Shot Count'}},
],[('Event','then','SetWeapon','execute'),('Event','Weapon','SetWeapon','Actor Weapon'),
   ('SetWeapon','then','Branch','execute'),('Event','Firing','Branch','Condition'),
   ('Branch','else','ResetShots','execute')])
make_event('PolarityWeaponShot',[],[
    {'ref':'Count','type':'variable_get','params':{'variable':'Shot Count'}},
    {'ref':'Increment','type':'function_call','params':{'class':'KismetMathLibrary','function':'Add_IntInt'}},
    {'ref':'SetCount','type':'variable_set','params':{'variable':'Shot Count'}},
],[('Event','then','SetCount','execute'),('Count','Shot Count','Increment','A'),
   ('Increment','ReturnValue','SetCount','Shot Count')])
# Add_IntInt defaults B=1.
assert unreal.BlueprintEditorLibrary.compile_blueprint(unreal.load_asset(AB))
assert unreal.EditorAssetLibrary.save_asset(AB,False)
print('VERIFIED adapter events compiled and saved')
