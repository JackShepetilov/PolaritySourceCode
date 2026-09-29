import unreal

# Read-only: compare every Transform (Modify) Bone node of the AR_02 copy against the pack AnimBP.
# Fields the pin dump cannot show: modes, spaces, alpha, constants, binding object, wired pins.
# Run: Tools/mcp.sh py Source/Tools/LPSPAR02/run.py

COPY = '/Game/Variant_Shooter/Tests/LPSP_AR02/ABP_AR02_LPSP_Test'
ORIG_NAME = 'ABP_LPSP_TP_PCH'
FIELDS = ['bone_name', 'translation_mode', 'rotation_mode', 'scale_mode',
          'translation_space', 'rotation_space', 'scale_space', 'alpha',
          'translation', 'rotation', 'scale']
PINS = ('Rotation', 'Translation', 'Scale', 'Alpha')


def matches():
    return [a.split('.')[0] for a in unreal.EditorAssetLibrary.list_assets('/Game/InfimaGames', recursive=True, include_folder=False)
            if a.split('/')[-1].split('.')[0] == ORIG_NAME]


def collect(path):
    asset = unreal.load_asset(path)
    if not asset:
        return None
    pkg = asset.get_outermost()
    out = {}
    for n in unreal.ObjectIterator(unreal.EdGraphNode):
        if n.get_outermost() != pkg or not hasattr(n, 'list_all_pins'):
            continue
        if 'ModifyBone' not in n.get_name():
            continue
        d = {'graph': n.get_outer().get_name()}
        try:
            inner = n.get_editor_property('node')
        except Exception as e:
            d['inner'] = 'ERR ' + str(e)
            out[n.get_name()] = d
            continue
        for f in FIELDS:
            try:
                d[f] = str(inner.get_editor_property(f))
            except Exception:
                pass
        try:
            d['binding'] = bool(unreal.find_object(None, n.get_path_name() + '.AnimGraphNodeBinding_Base_0'))
        except Exception:
            pass
        for p in n.list_all_pins():
            if str(p.get_pin_name()) in PINS:
                d['pin ' + str(p.get_pin_name())] = ','.join(sorted(q.get_owning_node().get_name() for q in p.list_connected_pins()))
        out[n.get_name()] = d
    return out


def dump(tag, d):
    return ['=== %s (%d nodes)' % (tag, len(d))] + ['  %s %s' % (k, d[k]) for k in sorted(d)]


cands = matches()
print('ORIGINAL CANDIDATES:', cands)
copy = collect(COPY)
orig = collect(cands[0]) if cands else None
lines = dump('COPY ' + COPY, copy)
if cands:
    lines += dump('ORIG ' + cands[0], orig)

diff = []
for k in sorted(set(copy) | set(orig or {})):
    a, b = copy.get(k), (orig or {}).get(k)
    if a is None or b is None:
        diff.append('%s exists only in %s' % (k, 'copy' if a else 'original'))
        continue
    for f in sorted(set(a) | set(b)):
        if a.get(f) != b.get(f):
            diff.append('%s %s:\n     copy %s\n     orig %s' % (k, f, a.get(f), b.get(f)))
out = unreal.Paths.project_saved_dir() + 'LPSP_AR02/modifybone_compare.txt'
open(out, 'w', encoding='utf-8').write('\n'.join(lines))
print('WROTE', out, 'nodes copy/orig', len(copy), len(orig or {}))
print('--- DIFFS (copy vs original) ---')
for l in diff:
    print(l)
