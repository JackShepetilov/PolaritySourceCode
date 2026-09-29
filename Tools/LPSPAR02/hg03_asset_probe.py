import json
import re
import unreal

OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/hg03_asset_probe.json'
BASE = '/Game/InfimaGames/LowPolyShooterPack/Data/Weapons/Animations'
ANIM = '/Game/InfimaGames/AnimatedLowPolyWeapons/Art/Characters/Animations/Handguns/'
AB = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
DONOR = '/Game/InfimaGames/LowPolyShooterPack/Usable/Weapons/BP_LPSP_WEP_Handgun_03'
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()

assets = unreal.EditorAssetLibrary.list_assets(BASE, recursive=False, include_folder=False)
hg = [x for x in assets if 'Handgun' in x]
donor = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(DONOR))
anim = donor.get_editor_property('Settings Animation').export_text()
weapon = donor.get_editor_property('Weapon Settings').export_text()
referenced = sorted(set(re.findall(r"/Game/[^'\"]+", anim + weapon)))

montages = {}
for name in ('AM_TP_CH_Handgun_Fire', 'AM_TP_CH_Handgun_Reload',
             'AM_TP_CH_Handgun_Reload_Empty'):
    path = ANIM + name
    asset = unreal.EditorAssetLibrary.load_asset(path)
    montages[name] = None if not asset else {
        'length': asset.get_editor_property('sequence_length'),
        'slots': [str(t.get_editor_property('slot_name'))
                  for t in asset.get_editor_property('slot_anim_tracks')],
    }

bp = unreal.EditorAssetLibrary.load_asset(AB)
graphs = ('EventGraph', 'CGraph Cache Values Weapon', 'Update Basics',
          'Update Recoil Values')
nodes = {}
for graph in graphs:
    try:
        found = unreal.BlueprintService.get_nodes_in_graph(AB, graph)
        nodes[graph] = [dict(title=n.node_title, id=n.node_id) for n in found
                        if any(s in n.node_title.lower() for s in
                               ('pose', 'table', 'sequence', 'look offset', 'shot', 'recoil', 'weapon'))]
    except Exception as exc:
        nodes[graph] = str(exc)

result = dict(tables=hg, donor_references=referenced, montages=montages, nodes=nodes)
with open(OUT, 'w', encoding='utf-8') as f:
    json.dump(result, f, ensure_ascii=False, indent=2)
print('ASSET PROBE', OUT, 'tables', len(hg), 'references', len(referenced))
