# Side profile of a gun in armature-local cm: for each 2 cm slice along Y, the lowest and highest Z,
# split into "mag" (the Mag/magazine group) and the rest. FLIP=1 turns the gun 180 about Z first, x turns
# it 180 about X (forward +Y and upside down in this frame).
import bpy, sys
from collections import defaultdict
args = sys.argv[sys.argv.index('--') + 1:]
path, flip, xflip = args[0], args[1] in ('1', 'x'), args[1] == 'x'
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=path, ignore_leaf_bones=False)
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
inv = arm.matrix_world.inverted()
rows = defaultdict(lambda: [1e9, -1e9, 1e9, -1e9])
for o in [o for o in bpy.data.objects if o.type == 'MESH']:
    M = inv @ o.matrix_world
    groups = {g.index: g.name for g in o.vertex_groups}
    for v in o.data.vertices:
        w = M @ v.co
        y, z = (-w.y if flip else w.y), (-w.z if xflip else w.z)
        g = groups[max(v.groups, key=lambda x: x.weight).group] if v.groups else ''
        k = int((y + 100) // 2) * 2 - 100
        r = rows[k]
        if g.lower().startswith('mag'):
            r[2] = min(r[2], z); r[3] = max(r[3], z)
        else:
            r[0] = min(r[0], z); r[1] = max(r[1], z)
for k in sorted(rows):
    lo, hi, mlo, mhi = rows[k]
    print('Y %4d  body z %6.1f..%6.1f   mag %s' % (k, lo, hi, '%6.1f..%6.1f' % (mlo, mhi) if mlo < 1e8 else '-'))
