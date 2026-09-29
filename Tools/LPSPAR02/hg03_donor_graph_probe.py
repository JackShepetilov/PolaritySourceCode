import json
import unreal

OUT = unreal.Paths.project_saved_dir() + 'LPSP_AR02/hg03_donor_graph_probe.json'
PATHS = {
    'base': '/Game/InfimaGames/LowPolyShooterPack/Core/Weapons/BP_LPSP_WEP',
    'ar': '/Game/InfimaGames/LowPolyShooterPack/Usable/Weapons/BP_LPSP_WEP_AR_02',
    'hg': '/Game/InfimaGames/LowPolyShooterPack/Usable/Weapons/BP_LPSP_WEP_Handgun_03',
    'adapter': '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test',
}
result = {}
for label, path in PATHS.items():
    row = {'path': path, 'graphs': []}
    for item in unreal.BlueprintService.list_graphs(path):
        name = str(item)
        row['graphs'].append(name)
    for name in ('LPSP - Get Character Animation Poses', 'EventGraph'):
        try:
            nodes = unreal.BlueprintService.get_nodes_in_graph(path, name)
            row[name] = [dict(id=n.node_id, title=n.node_title)
                         for n in nodes if any(s in n.node_title.lower() for s in
                             ('pose', 'table', 'fire', 'shot', 'montage', 'setting', 'weapon'))]
        except Exception as exc:
            row[name] = str(exc)
    result[label] = row
with open(OUT, 'w', encoding='utf-8') as handle:
    json.dump(result, handle, ensure_ascii=False, indent=2)
print('DONOR GRAPH PROBE', OUT)
