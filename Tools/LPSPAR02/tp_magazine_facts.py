import json
import re

import unreal

# Facts for the TP magazine work: which meshes the weapon has, which sockets/bones they carry,
# and how the Infima donor mounts its magazine. Read-only.
#
# NOTE: the first statement must be an import. The engine runs this file in ExecuteFile mode and
# treats a leading docstring as a path ("Could not load Python file").

BS = unreal.BlueprintService
BP = '/Game/Variant_Shooter/Tests/LPSP_AR02/BP_AR02_Integration_Test'
DONOR = '/Game/InfimaGames/LowPolyShooterPack/Usable/Weapons/BP_LPSP_WEP_AR_02'
MESH_RE = re.compile(r"'(/Game/[^']+)'")


def dump(obj, limit=300):
    out = {}
    for name in dir(obj):
        if name.startswith('_'):
            continue
        try:
            value = getattr(obj, name)
        except Exception:
            continue
        if callable(value):
            continue
        out[name] = str(value)[:limit]
    return out


paths = set()
for label, path in (('TEST', BP), ('DONOR', DONOR)):
    print('=== COMPONENTS', label)
    try:
        comps = BS.list_components(path)
        print('  count', len(comps))
        for comp in comps:
            data = dump(comp)
            print('  C', json.dumps(data, default=str)[:520])
            for value in data.values():
                paths.update(MESH_RE.findall(value))
    except Exception as exc:
        print('  ERR', exc)
    try:
        print('  HIER', str(BS.get_component_hierarchy(path))[:1500])
    except Exception as exc:
        print('  HIER ERR', exc)

for mesh_path in sorted(paths):
    mesh = unreal.EditorAssetLibrary.load_asset(mesh_path)
    print('MESH', mesh_path, type(mesh).__name__, bool(mesh))
    if not mesh:
        continue
    print('  API', [n for n in dir(mesh) if 'socket' in n.lower() or 'bone' in n.lower()])
    try:
        sockets = mesh.get_editor_property('sockets')
    except Exception as exc:
        sockets = None
        print('  sockets ERR', exc)
    if sockets:
        for socket in sockets:
            print('  SOCKET', json.dumps(dump(socket), default=str)[:250])
