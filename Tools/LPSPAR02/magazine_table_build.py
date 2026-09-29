import unreal, json
from pathlib import Path

BP = '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_AR02_Integration_Test'
BS = unreal.BlueprintService
OUT = Path(unreal.Paths.project_saved_dir()) / 'LPSP_AR02/magazine_nodes.json'
ids = json.loads(OUT.read_text()) if OUT.exists() else {}

def node(ref, kind, **params):
    return dict(ref=ref, type=kind, params=params)

def add(graph, descs):
    pending = [d for d in descs if d['ref'] not in ids]
    for d in list(pending):
        if d['type']=='spawner_key' and d['params']['key'].startswith('SPAWN '):
            key=d['params']['key']
            BS.discover_nodes(BP,key.split('|')[-1],'',15)
            ids[d['ref']]=BS.create_node_by_key(BP,graph,key,0,0)
            assert ids[d['ref']], d
            OUT.write_text(json.dumps(ids,indent=2))
            pending.remove(d)
    if pending:
        r = BS.build_graph(BP, graph, pending, [], [], False, False)
        ids.update(dict(r.ref_to_node_id))
        OUT.write_text(json.dumps(ids, indent=2))
        assert not r.errors and not r.nodes_failed, str(r)
        print('ADDED',graph,list(r.ref_to_node_id))

def connect(graph,a,ap,b,bp):
    assert BS.connect_nodes(BP,graph,ids.get(a,a),ap,ids.get(b,b),bp),(a,ap,b,bp)

def default(graph,a,p,v):
    assert BS.set_node_pin_value(BP,graph,ids[a],p,v),(a,p,v)

donor=unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class('/Game/InfimaGames/LowPolyShooterPack/Usable/Weapons/BP_LPSP_WEP_AR_02'))
handle=donor.get_editor_property('Row Handle Settings Magazine')
assert BS.set_variable_default_value(BP,'InfimaMagazineRow',handle.export_text())
g='UserConstructionScript'
add(g,[
 node('RowHandle','variable_get',variable='InfimaMagazineRow'),
 node('BreakHandle','spawner_key',key='SPAWN K2Node_BreakStruct|Break Data Table Row Handle'),
 node('ReadRow','spawner_key',key='SPAWN K2Node_GetDataTableRow|Get Data Table Row'),
 node('BreakMagazine','spawner_key',key='SPAWN K2Node_BreakStruct|Break SMagazine'),
 node('Magazine','variable_get',variable='InfimaTPMagazine'),
 node('Weapon','variable_get',variable='ThirdPersonMesh'),
 node('Mount','function_call',**{'class':'SceneComponent','function':'K2_AttachToComponent'}),
 node('SetMesh','function_call',**{'class':'StaticMeshComponent','function':'SetStaticMesh'}),
])
connect(g,'RowHandle','InfimaMagazineRow','BreakHandle','DataTableRowHandle')
connect(g,'ReadRow','ReturnValue','BreakMagazine','SMagazine')
connect(g,'BreakHandle','DataTable','ReadRow','DataTable')
connect(g,'BreakHandle','RowName','ReadRow','RowName')
connect(g,'Magazine','InfimaTPMagazine','Mount','self')
connect(g,'Weapon','ThirdPersonMesh','Mount','Parent')
connect(g,'Magazine','InfimaTPMagazine','SetMesh','self')
connect(g,'BreakMagazine','Mesh_23_E3ECA4BA4931B1849B8670849EF85707','SetMesh','NewMesh')
connect(g,'B09249BF4E922B226D49B8B87186F696','then','Mount','execute')
connect(g,'Mount','then','ReadRow','execute')
connect(g,'ReadRow','then','SetMesh','execute')
for pin,value in [('SocketName','SOCKET_Magazine'),('LocationRule','SnapToTarget'),('RotationRule','SnapToTarget'),('ScaleRule','KeepRelative'),('bWeldSimulatedBodies','False')]:default(g,'Mount',pin,value)
add(g,[node('ClearMagazine','function_call',**{'class':'StaticMeshComponent','function':'SetStaticMesh'}),node('MissingRow','print_string')])
connect(g,'Magazine','InfimaTPMagazine','ClearMagazine','self')
connect(g,'ReadRow','RowNotFound','ClearMagazine','execute')
connect(g,'ClearMagazine','then','MissingRow','execute')
default(g,'MissingRow','InString','[InfimaTPMagazine] Missing magazine table row. Check InfimaMagazineRow.')
default(g,'MissingRow','bPrintToScreen','False')

g='EventGraph'
add(g,[
 node('VisibleMagazine','variable_get',variable='InfimaTPMagazine'),
 node('SetVisible','function_call',**{'class':'SceneComponent','function':'SetVisibility'}),
 node('DropMagazine','variable_get',variable='InfimaTPMagazine'),
 node('DropTransform','function_call',**{'class':'SceneComponent','function':'K2_GetComponentToWorld'}),
 node('DropMesh','member_get',member='StaticMesh',**{'class':'StaticMeshComponent'}),
 node('SpawnDrop','spawner_key',key='NODE K2Node_SpawnActorFromClass'),
 node('PropMesh','member_get',member='Mesh Magazine',**{'class':'/Game/InfimaGames/AnimatedLowPolyWeapons/Core/BP_ALPW_Magazine.BP_ALPW_Magazine_C'}),
 node('AssignDropMesh','function_call',**{'class':'StaticMeshComponent','function':'SetStaticMesh'}),
])
connect(g,'VisibilityEvent','then','SetVisible','execute')
connect(g,'VisibilityEvent','Visibility','SetVisible','bNewVisibility')
connect(g,'VisibleMagazine','InfimaTPMagazine','SetVisible','self')
default(g,'SpawnDrop','Class','/Game/InfimaGames/AnimatedLowPolyWeapons/Core/BP_ALPW_Magazine.BP_ALPW_Magazine_C')
default(g,'SpawnDrop','CollisionHandlingOverride','AlwaysSpawn')
connect(g,'DropMagazine','InfimaTPMagazine','DropTransform','self')
connect(g,'DropMagazine','InfimaTPMagazine','DropMesh','self')
connect(g,'DropTransform','ReturnValue','SpawnDrop','SpawnTransform')
connect(g,'DropEvent','then','SpawnDrop','execute')
connect(g,'SpawnDrop','ReturnValue','PropMesh','self')
connect(g,'SpawnDrop','then','AssignDropMesh','execute')
connect(g,'PropMesh','Mesh Magazine','AssignDropMesh','self')
connect(g,'DropMesh','StaticMesh','AssignDropMesh','NewMesh')
g='ALPW: Get Droppable Magazine Mesh'
add(g,[node('ReturnedMagazine','variable_get',variable='InfimaTPMagazine')])
connect(g,'ReturnedMagazine','InfimaTPMagazine','0979A79144BAB95F0619C280EFCEFC05','Mesh')
print('MODIFIED table-driven magazine and original Infima notify handlers',BP)
