# Blender 5.0, headless: a third person double of a gun's first person mesh on its Infima donor
# skeleton, so Infima's third person grip and reload animations drive it [author, 2026-10-17].
#
#   blender -b --python-exit-code 1 -P build_double.py -- <Gun> <SavedTPRemodelDir>
#
# Reads jobs.json (next to this file) for the gun: turn, offset, group map. Inputs <dir>/<Gun>/DONOR.fbx
# and FP.fbx (ue_export_all.py), outputs <dir>/<Gun>/DOUBLE.fbx and DOUBLE.blend.
# Both FBX come from the editor's exporter, so both carry the same mirror; all placement is done in
# ARMATURE-local centimetres (measure.py / profile.py / fit.py print the same frame).
#
# Pilot: AKX203 on AR_01 (turn z, offset 0 -9 6.7, Mag -> magazine, Charge -> bolt), see the handoff.
import bpy, bmesh, sys, os, json, math
from mathutils import Matrix, Vector


def parse_turn(t):
    # 'z' / 'x': 180 about that axis; '0': none; or steps applied in order, e.g. 'x90,z180'.
    if t in ('z', 'x'):
        return Matrix.Rotation(math.pi, 4, t.upper())
    m = Matrix.Identity(4)
    for part in t.split(','):
        if part not in ('', '0'):
            m = Matrix.Rotation(math.radians(float(part[1:])), 4, part[0].upper()) @ m
    return m

args = sys.argv[sys.argv.index('--') + 1:]
GUN, ROOT = args[0], args[1]
job = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'jobs.json'), encoding='utf-8'))['guns'][GUN]
D = os.path.join(ROOT, GUN)
SRC_DONOR, SRC_FP = os.path.join(D, 'DONOR.fbx'), os.path.join(D, 'FP.fbx')
OUT_FBX, OUT_BLEND = os.path.join(D, 'DOUBLE.fbx'), os.path.join(D, 'DOUBLE.blend')
OFFSET = Vector(job['offset'])
GROUP_MAP = job['map']


def target_of(group):
    for k, bone in GROUP_MAP.items():
        if group == k or (k.endswith('*') and group.startswith(k[:-1])):
            return bone
    return 'root'


bpy.ops.wm.read_factory_settings(use_empty=True)

# --- The target: the donor's armature and its mesh object (which carries the armature modifier) ---
bpy.ops.import_scene.fbx(filepath=SRC_DONOR, ignore_leaf_bones=False)
d_objs = set(bpy.data.objects)
d_arm = next(o for o in d_objs if o.type == 'ARMATURE')
d_mesh = next(o for o in d_objs if o.type == 'MESH')
bone_names = [b.name for b in d_arm.data.bones]
print('DONOR', d_arm.name, d_mesh.name, 'bones', bone_names)
for bone in set(GROUP_MAP.values()) | {'root'}:
    assert bone in bone_names, 'donor has no bone %s' % bone

# --- The source: our first person mesh ---
bpy.ops.import_scene.fbx(filepath=SRC_FP, ignore_leaf_bones=False)
f_objs = set(bpy.data.objects) - d_objs
f_arm = next(o for o in f_objs if o.type == 'ARMATURE')
f_meshes = [o for o in f_objs if o.type == 'MESH']
assert len(f_meshes) == 1, 'expected one FP mesh object, got %s' % [o.name for o in f_meshes]
f_mesh = f_meshes[0]
f_groups = {g.index: g.name for g in f_mesh.vertex_groups}
target = []
for v in f_mesh.data.vertices:
    name = f_groups[max(v.groups, key=lambda x: x.weight).group] if v.groups else ''
    target.append(target_of(name))
print('FP', f_mesh.name, 'verts', len(target), {g: target.count(g) for g in set(target)})

# FP mesh-local -> FP armature-local cm -> turned and placed -> donor armature-local -> donor mesh-local.
turn = parse_turn(job['turn'])
fit = Matrix.Translation(OFFSET) @ turn
to_f_arm = f_arm.matrix_world.inverted() @ f_mesh.matrix_world
to_d_mesh = d_mesh.matrix_world.inverted() @ d_arm.matrix_world
total = to_d_mesh @ fit @ to_f_arm

me = f_mesh.data.copy()
me.name = 'SK_' + GUN + '_TP'

# Custom normals have to turn with the geometry; keep them aside, turn them, put them back.
had_custom = me.has_custom_normals
corner_normals = [Vector(n.vector) for n in me.corner_normals] if had_custom else None

# Drop the FP deform layer: its indices point at the FP object's groups, not ours.
bm = bmesh.new(); bm.from_mesh(me)
if bm.verts.layers.deform.active:
    bm.verts.layers.deform.remove(bm.verts.layers.deform.active)
bm.to_mesh(me); bm.free()

me.transform(total)
if had_custom:
    rot = total.to_3x3().normalized()
    me.normals_split_custom_set([(rot @ n).normalized() for n in corner_normals])
print('CUSTOM NORMALS', had_custom)

# --- Swap the geometry into the donor's mesh object: it keeps its parent and armature modifier ---
old = d_mesh.data
d_mesh.data = me
bpy.data.meshes.remove(old)
d_mesh.vertex_groups.clear()
used = sorted(set(target))
groups = {n: d_mesh.vertex_groups.new(name=n) for n in used}
for name, vg in groups.items():
    vg.add([i for i, g in enumerate(target) if g == name], 1.0, 'REPLACE')
print('GROUPS', {n: target.count(n) for n in used})

inv = d_arm.matrix_world.inverted() @ d_mesh.matrix_world
for name in used:
    pts = [inv @ me.vertices[i].co for i, g in enumerate(target) if g == name]
    lo = Vector([min(p[k] for p in pts) for k in range(3)]); hi = Vector([max(p[k] for p in pts) for k in range(3)])
    print('BOX', name, tuple(round(x, 1) for x in lo), tuple(round(x, 1) for x in hi),
          'bone head', tuple(round(x, 1) for x in d_arm.data.bones[name].head_local))

# --- Clean up: the FP objects go, and the importer's parent empties are lifted off (they would
# become an extra root bone in UE), as remodel_ar01_mgx5.py does ---
for o in f_objs:
    bpy.data.objects.remove(o, do_unlink=True)
for empty in [o for o in bpy.data.objects if o.type == 'EMPTY']:
    for child in list(empty.children):
        mw = child.matrix_world.copy()
        child.parent = None
        child.matrix_world = mw
    bpy.data.objects.remove(empty)
print('ROOTS', [(o.name, o.type) for o in bpy.data.objects if o.parent is None])
print('MATS', [m.name for m in d_mesh.data.materials])

bpy.ops.wm.save_as_mainfile(filepath=OUT_BLEND)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.export_scene.fbx(filepath=OUT_FBX, use_selection=False, object_types={'EMPTY', 'ARMATURE', 'MESH'},
                         add_leaf_bones=False, bake_anim=False, mesh_smooth_type='FACE',
                         use_armature_deform_only=False, apply_unit_scale=True, global_scale=1.0)
print('EXPORTED', OUT_FBX)
