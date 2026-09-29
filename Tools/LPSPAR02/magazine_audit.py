import json
from pathlib import Path

import unreal

# Read-only audit of the AR02 magazine work in BP_AR02_Integration_Test.
#
# Answers three questions after the 2026-09-28 editor assertion:
#   1. which node ids from magazine_nodes.json still exist in the graph and in which graph,
#   2. what the UserConstructionScript / EventGraph actually contain right now (nodes, connections),
#   3. what the Infima donor offers, so the next build step copies shapes from it instead of guessing.
#
# Nothing is written. Run through Tools/mcp.sh:
#    bash Tools/mcp.sh py Source/Tools/LPSPAR02/magazine_audit.py
# NOTE: the first statement must be an import. The engine runs a Python file in ExecuteFile mode and
# treats a leading docstring as a path, which fails with "Could not load Python file".

BP = '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_AR02_Integration_Test'
DONOR = '/Game/InfimaGames/LowPolyShooterPack/Usable/Weapons/BP_LPSP_WEP_AR_02'
BS = unreal.BlueprintService
JSON = Path(unreal.Paths.project_saved_dir()) / 'LPSP_AR02/magazine_nodes.json'



def fields(obj):
    out = {}
    for name in dir(obj):
        if name.startswith('_'):
            continue
        try:
            value = getattr(obj, name)
        except Exception as exc:  # a property may be unreadable on this node kind
            out[name] = 'ERR:%s' % exc
            continue
        if callable(value):
            continue
        out[name] = str(value)
    return out


ids = json.loads(JSON.read_text()) if JSON.exists() else {}
by_id = {v: k for k, v in ids.items()}
print('JSON_IDS', len(ids), sorted(ids))

print('VARIABLES', [v.variable_name if hasattr(v, 'variable_name') else str(v)
                    for v in BS.list_variables(BP)])
cls = unreal.EditorAssetLibrary.load_blueprint_class(BP)
cdo = unreal.get_default_object(cls)
for name in ('InfimaMagazineRow', 'InfimaTPMagazine'):
    try:
        print('DEFAULT', name, cdo.get_editor_property(name))
    except Exception as exc:
        print('DEFAULT', name, 'ERR', exc)

for graph in ('UserConstructionScript', 'EventGraph', 'ALPW: Get Droppable Magazine Mesh'):
    nodes = BS.get_nodes_in_graph(BP, graph)
    print('=== GRAPH', graph, 'nodes=%d' % len(nodes))
    known = []
    for node in nodes:
        data = fields(node)
        node_id = data.get('node_id', '')
        ref = by_id.get(node_id, '')
        if ref:
            known.append(ref)
        print('NODE', json.dumps({'ref': ref, **data}, default=str))
    missing = [k for k, v in ids.items() if v not in {fields(n).get('node_id') for n in nodes}]
    print('KNOWN_IDS_IN_GRAPH', sorted(known))
    print('CONNECTIONS', json.dumps([fields(c) for c in BS.get_connections(BP, graph)], default=str))

print('DONOR_GRAPHS', json.dumps([str(g) for g in BS.list_graphs(DONOR)], default=str))
print('DIRTY', [str(p) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()])
print('PIE', bool(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()))

# --- Donor kit: which meshes it mounts and where the magazine lives on them -------------
print('=== DONOR COMPONENTS')
try:
    for comp in BS.list_components(DONOR):
        print('C', json.dumps(fields(comp), default=str))
except Exception as exc:
    print('C ERR', exc)

donor_cdo = unreal.get_default_object(unreal.EditorAssetLibrary.load_blueprint_class(DONOR))
mesh_paths = set()
for name in dir(donor_cdo):
    if name.startswith('_'):
        continue
    try:
        value = getattr(donor_cdo, name)
    except Exception:
        continue
    text = str(value)
    if 'Mesh' in name or 'Mesh' in type(value).__name__:
        print('CDO', name, type(value).__name__, text[:160])
        if '/' in text:
            mesh_paths.add(text.split("'")[1])

for mesh_path in sorted(mesh_paths):
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    if not mesh:
        print('MESH MISSING', mesh_path)
        continue
    print('MESH', mesh_path, type(mesh).__name__)
    for prop in ('sockets', 'skeleton', 'physics_asset', 'material_slots'):
        try:
            value = mesh.get_editor_property(prop)
        except Exception as exc:
            print('   ', prop, 'ERR', exc)
            continue
        if prop == 'sockets' and value:
            for socket in value:
                print('    SOCKET', json.dumps(fields(socket), default=str))
        else:
            print('   ', prop, str(value)[:120])

INTERFACE = '/Game/InfimaGames/AnimatedLowPolyWeapons/Core/BPI_ALPW_Droppable_Magazine_Target'
print('IFACE_GRAPHS', json.dumps([str(g) for g in BS.list_graphs(INTERFACE)], default=str))

