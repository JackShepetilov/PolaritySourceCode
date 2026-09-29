import unreal
import json
from pathlib import Path

BP = globals().get('target_blueprint', '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_AR02_Integration_Test')
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
BS = unreal.BlueprintService
OUT = Path(unreal.Paths.project_saved_dir()) / 'LPSP_AR02'
MARK = OUT / (BP.rsplit('/',1)[-1] + '_bridge_nodes.json')

def main():
    if MARK.exists():
        print('EXISTING JOURNAL: inspect before resuming', MARK)
        return
    cls = unreal.EditorAssetLibrary.load_blueprint_class(AB)
    def n(ref, kind, **params):
        return dict(ref=ref, type=kind, params=params)
    nodes = [
        n('WeaponSelf','spawner_key',key='NODE K2Node_Self'),
        n('Holder','function_call',**{'class':'Actor','function':'GetOwner'}),
        n('Character','cast',target_class='/Script/Engine.Character'),
        n('Body','member_get',member='Mesh',**{'class':'Character'}),
        n('Animation','function_call',**{'class':'SkeletalMeshComponent','function':'GetAnimInstance'}),
        n('Adapter','cast',target_class=AB+'.ABP_AR02_LPSP_Test_C'),
    ]
    existing = [x.node_id for x in BS.get_nodes_in_graph(BP,'EventGraph') if x.node_title == 'Set Actor Weapon']
    if not existing:
        nodes.append(n('WeaponReference','member_set',member='Actor Weapon',**{'class':cls.get_name()}))
    r = BS.build_graph(BP,'EventGraph',nodes,[],[],False,False)
    assert r and not r.errors and r.nodes_failed == 0, str(r)
    ids = dict(r.ref_to_node_id)
    if existing:
        assert len(existing) == 1
        ids['WeaponReference'] = existing[0]
    MARK.write_text(json.dumps(ids,indent=2),encoding='utf-8')
    print('CREATED bridge nodes:',ids)
    assert not r.errors and r.nodes_failed == 0, str(r)
    def cast_pin(ref):
        return next(p.pin_name for p in BS.get_node_pins(BP,'EventGraph',ids[ref])
                    if not p.is_input and p.pin_type == 'object')
    def connect(a,ap,b,bp):
        assert BS.connect_nodes(BP,'EventGraph',ids.get(a,a),ap,ids.get(b,b),bp),(a,ap,b,bp)
    connect('Holder','ReturnValue','Character','Object')
    connect('Character',cast_pin('Character'),'Body','self')
    connect('Body','Mesh','Animation','self')
    connect('Animation','ReturnValue','Adapter','Object')
    connect('Adapter',cast_pin('Adapter'),'WeaponReference','self')
    connect('WeaponSelf','self','WeaponReference','Actor Weapon')
    tick = [x.node_id for x in BS.get_nodes_in_graph(BP,'EventGraph') if x.node_title == 'Parent: Tick']
    assert len(tick) == 1, tick
    connect(tick[0],'then','Character','execute')
    connect('Character','then','Adapter','execute')
    connect('Adapter','then','WeaponReference','execute')
    assert unreal.BlueprintEditorLibrary.compile_blueprint(unreal.load_asset(BP))
    assert unreal.EditorAssetLibrary.save_asset(BP,False)
    print('VERIFIED: BP_AR owner bridge compiled and saved')

main()
