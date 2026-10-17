# Blender 5.0: per material / vertex group boxes and bone heads in ARMATURE-local cm (= UE mesh space,
# Y mirrored by the FBX round trip). Usage: blender -b -P measure.py -- a.fbx b.fbx
import bpy, sys
from collections import defaultdict
from mathutils import Vector

def r(v): return tuple(round(x, 1) for x in v)

def load(path):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=path, ignore_leaf_bones=False)
    arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
    inv = arm.matrix_world.inverted()
    print('ARMATURE', arm.name, 'world scale', r(arm.matrix_world.to_scale()))
    for b in arm.data.bones:
        print('  BONE', b.name, 'parent', b.parent.name if b.parent else None, 'head', r(b.head_local), 'tail', r(b.tail_local))
    for o in [o for o in bpy.data.objects if o.type == 'MESH']:
        me = o.data
        M = inv @ o.matrix_world
        groups = {g.index: g.name for g in o.vertex_groups}
        gb = defaultdict(lambda: [Vector((1e9,)*3), Vector((-1e9,)*3), 0])
        allb = [Vector((1e9,)*3), Vector((-1e9,)*3)]
        for v in me.vertices:
            w = M @ v.co
            for i in range(3):
                allb[0][i] = min(allb[0][i], w[i]); allb[1][i] = max(allb[1][i], w[i])
            name = groups[max(v.groups, key=lambda x: x.weight).group] if v.groups else '(none)'
            b = gb[name]
            for i in range(3):
                b[0][i] = min(b[0][i], w[i]); b[1][i] = max(b[1][i], w[i])
            b[2] += 1
        print('MESH', o.name, 'verts', len(me.vertices), 'box', r(allb[0]), r(allb[1]))
        for n, (lo, hi, c) in sorted(gb.items()):
            print('  GROUP', n, c, r(lo), r(hi))
        mb = defaultdict(lambda: [Vector((1e9,)*3), Vector((-1e9,)*3)])
        for p in me.polygons:
            for vi in p.vertices:
                w = M @ me.vertices[vi].co
                b = mb[p.material_index]
                for i in range(3):
                    b[0][i] = min(b[0][i], w[i]); b[1][i] = max(b[1][i], w[i])
        for mi, (lo, hi) in sorted(mb.items()):
            print('  MAT', me.materials[mi].name, r(lo), r(hi))

for p in sys.argv[sys.argv.index('--') + 1:]:
    print('=====', p)
    load(p)
