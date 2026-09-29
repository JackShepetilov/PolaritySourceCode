import unreal,json
from pathlib import Path
BP='/Game/Variant_Shooter/Tests/LPSP_AR02/BP_AR02_Integration_Test'
AB='/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
BS=unreal.BlueprintService
OUT=Path(unreal.Paths.project_saved_dir())/'LPSP_AR02'
ids=json.loads((OUT/'BP_AR02_Integration_Test_bridge_nodes.json').read_text())
assert not (OUT/'weapon_events_done.json').exists(),'Already applied'
def node(ref,kind,**params): return dict(ref=ref,type=kind,params=params)
r=BS.build_graph(BP,'EventGraph',[
 node('Register','function_call',**{'class':AB,'function':'PolarityRegisterWeapon'}),
 node('Firing','function_call',**{'class':'ShooterWeapon','function':'IsFiring'}),
 node('ShotCharacter','cast',target_class='/Script/Engine.Character'),
 node('ShotBody','member_get',member='Mesh',**{'class':'Character'}),
 node('ShotAnimation','function_call',**{'class':'SkeletalMeshComponent','function':'GetAnimInstance'}),
 node('ShotAdapter','cast',target_class=AB+'.ABP_AR02_LPSP_Test_C'),
 node('ShotNotify','function_call',**{'class':AB,'function':'PolarityWeaponShot'}),
],[],[],False,False)
assert r and not r.errors and not r.nodes_failed,str(r)
new=dict(r.ref_to_node_id);ids.update(new)
(OUT/'weapon_event_nodes.json').write_text(json.dumps(ids,indent=2),encoding='utf-8')
print('CREATED event call nodes',new)
def castpin(ref): return next(p.pin_name for p in BS.get_node_pins(BP,'EventGraph',ids[ref]) if not p.is_input and p.pin_type=='object')
def con(a,ap,b,bp): assert BS.connect_nodes(BP,'EventGraph',ids.get(a,a),ap,ids.get(b,b),bp),(a,ap,b,bp)
con('WeaponSelf','self','Register','Weapon')
con('Firing','ReturnValue','Register','Firing')
con('Adapter',castpin('Adapter'),'Register','self')
BS.disconnect_pin(BP,'EventGraph',ids['Adapter'],'then')
con('Adapter','then','Register','execute')
assert BS.delete_node(BP,'EventGraph',ids['WeaponReference'])
shot=[n.node_id for n in BS.get_nodes_in_graph(BP,'EventGraph') if n.node_title.replace('\\n','\n').splitlines()[0]=='OnShotFired_Event']
assert len(shot)==1,shot
con(shot[0],'then','ShotCharacter','execute')
con('Holder','ReturnValue','ShotCharacter','Object')
con('ShotCharacter',castpin('ShotCharacter'),'ShotBody','self')
con('ShotBody','Mesh','ShotAnimation','self')
con('ShotAnimation','ReturnValue','ShotAdapter','Object')
con('ShotCharacter','then','ShotAdapter','execute')
con('ShotAdapter',castpin('ShotAdapter'),'ShotNotify','self')
con('ShotAdapter','then','ShotNotify','execute')
assert unreal.BlueprintEditorLibrary.compile_blueprint(unreal.load_asset(BP))
assert unreal.EditorAssetLibrary.save_asset(BP,False)
(OUT/'weapon_events_done.json').write_text(json.dumps({'asset':BP}),encoding='utf-8')
print('VERIFIED weapon events compiled and saved')
