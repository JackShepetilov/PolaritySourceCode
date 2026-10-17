# Blender 5.0, headless. Fit of a first person gun onto its Infima donor, in the donor's ARMATURE-local cm
# (the frame measure.py / profile.py print), and an overlay picture to judge it by eye.
#
#   blender -b -P fit.py -- <GunDir> <kind: rifle|pistol> [turn z|x|0] [dx dy dz]
#
# turn: z = 180 about Z (Kinemation forward +Y, upright; the usual case), x = 180 about X (forward +Y
# and upside down in this frame, e.g. MGX5, Striker-V), 0 = none, or steps in order such as x90,z180
# (PDW90 lies along Z in its FBX).
#
# Without dx dy dz it estimates them: bore line (front 2 cm of the gun vs the donor's SOCKET_Muzzle) and
# pistol grip (centre of a horizontal band through the grip, behind the magazine). With them it uses them.
# Writes <GunDir>/fit_overlay.png: donor red, our gun blue, markers: magazine bone (green), root /
# OptionalGrip (yellow), SOCKET_Grip (cyan), SOCKET_Muzzle (magenta).
import bpy, sys, math, os
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
GUN, KIND = args[0], args[1]
TURN = args[2] if len(args) > 2 else 'z'
MANUAL = Vector([float(a) for a in args[3:6]]) if len(args) >= 6 else None


def load(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path, ignore_leaf_bones=False)
    objs = set(bpy.data.objects) - before
    arm = next(o for o in objs if o.type == 'ARMATURE')
    mesh = next(o for o in objs if o.type == 'MESH')
    M = arm.matrix_world.inverted() @ mesh.matrix_world
    groups = {g.index: g.name for g in mesh.vertex_groups}
    pts, grp = [], []
    for v in mesh.data.vertices:
        pts.append(M @ v.co)
        grp.append(groups[max(v.groups, key=lambda x: x.weight).group] if v.groups else '')
    edges = [tuple(e.vertices) for e in mesh.data.edges]
    return objs, arm, mesh, M, pts, grp, edges


bpy.ops.wm.read_factory_settings(use_empty=True)
d_objs, d_arm, d_mesh, d_M, d_pts, d_grp, d_edges = load(os.path.join(GUN, 'DONOR.fbx'))
bones = {b.name: Vector(b.head_local) for b in d_arm.data.bones}
f_objs, f_arm, f_mesh, f_M, f_pts, f_grp, f_edges = load(os.path.join(GUN, 'FP.fbx'))
turn = parse_turn(TURN)
f_pts = [turn @ p for p in f_pts]
is_mag = [g.lower().startswith('mag') for g in f_grp]


def bore_fp():
    ymin = min(p.y for p in f_pts)
    zs = [p.z for p in f_pts if p.y < ymin + 2.0]
    return (min(zs) + max(zs)) / 2


def grip_y(pts, edges, skip, bore, mag_y):
    # Cross-section of the surface at one height: where edges cross the plane, not where vertices are
    # (a low poly grip has none in the middle).
    h = bore - 12 if KIND == 'rifle' else bore - 8
    ys = []
    for a, b in edges:
        if skip[a] or skip[b]:
            continue
        pa, pb = pts[a], pts[b]
        if (pa.z - h) * (pb.z - h) > 0 or pa.z == pb.z:
            continue
        t = (h - pa.z) / (pb.z - pa.z)
        y = pa.y + (pb.y - pa.y) * t
        if KIND != 'rifle' or y > mag_y + 2:
            ys.append(y)
    ys.sort()
    if not ys:
        return None, []
    # First contiguous run (gaps over 6 cm split runs: inside a grip there are no crossings) from the front: the grip, not the stock.
    runs, start = [], ys[0]
    for a, b in zip(ys, ys[1:]):
        if b - a > 6.0:
            runs.append((start, a)); start = b
    runs.append((start, ys[-1]))
    r = runs[0]
    return (r[0] + r[1]) / 2, runs


d_bore = bones['SOCKET_Muzzle'].z
d_mag = bones.get('magazine', bones.get('rocket', Vector((0, -1e9, 0))))
f_bore = bore_fp()
f_mag_pts = [p for p, m in zip(f_pts, is_mag) if m]
f_mag_y = sum(p.y for p in f_mag_pts) / len(f_mag_pts) if f_mag_pts else None
d_gy, d_runs = grip_y(d_pts, d_edges, [False] * len(d_pts), d_bore, d_mag.y)
f_gy, f_runs = grip_y(f_pts, f_edges, is_mag, f_bore, f_mag_y if f_mag_y is not None else -1e9)
print('DONOR bore z %.1f  mag bone %s  grip y %s runs %s' % (d_bore, tuple(round(x, 1) for x in d_mag), d_gy, [(round(a, 1), round(b, 1)) for a, b in d_runs]))
print('FP    bore z %.1f  mag centre y %s  grip y %s runs %s' % (f_bore, f_mag_y, f_gy, [(round(a, 1), round(b, 1)) for a, b in f_runs]))
auto = Vector((0.0, (d_gy - f_gy) if (d_gy is not None and f_gy is not None) else 0.0, d_bore - f_bore))
off = MANUAL if MANUAL is not None else auto
print('AUTO OFFSET', tuple(round(x, 1) for x in auto), 'USED', tuple(round(x, 1) for x in off))
if f_mag_y is not None:
    print('MAG after fit: ours centre y %.1f, z %.1f..%.1f ; donor magazine bone y %.1f z %.1f' % (
        f_mag_y + off.y, min(p.z for p in f_mag_pts) + off.z, max(p.z for p in f_mag_pts) + off.z, d_mag.y, d_mag.z))
fy = [p.y + off.y for p in f_pts]
print('FP box y %.1f..%.1f  donor box y %.1f..%.1f' % (min(fy), max(fy), min(p.y for p in d_pts), max(p.y for p in d_pts)))

# --- overlay: rebuild both as plain meshes in donor armature cm ---
d_data, f_data = d_mesh.data, f_mesh.data
for o in list(bpy.data.objects):
    bpy.data.objects.remove(o, do_unlink=True)


def plain(name, me_src, pts, color):
    me = me_src.copy()
    for v, p in zip(me.vertices, pts):
        v.co = p
    me.materials.clear()
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    o.color = color
    return o


plain('donor', d_data, d_pts, (0.9, 0.2, 0.2, 1))
plain('ours', f_data, [p + off for p in f_pts], (0.2, 0.4, 0.95, 1))
for name, col in (('magazine', (0.1, 0.9, 0.1, 1)), ('root', (1, 0.9, 0, 1)), ('SOCKET_Grip', (0, 0.9, 0.9, 1)),
                  ('SOCKET_Muzzle', (0.9, 0, 0.9, 1))):
    if name in bones:
        bpy.ops.mesh.primitive_uv_sphere_add(radius=0.9, location=bones[name] + Vector((8, 0, 0)))
        bpy.context.object.color = col
# 10 cm ticks along Y at the donor bore, and a line for the bore
for y in range(-80, 81, 10):
    bpy.ops.mesh.primitive_cube_add(size=1, location=(8, y, d_bore - 25))
    bpy.context.object.scale = (0.2, 0.3, 3 if y % 50 == 0 else 1.5)
    bpy.context.object.color = (0, 0, 0, 1)

sc = bpy.context.scene
sc.render.engine = 'BLENDER_WORKBENCH'
sc.display.shading.color_type = 'OBJECT'
sc.display.shading.light = 'FLAT'
sc.display.shading.show_xray = True
sc.display.shading.xray_alpha = 0.55
sc.render.resolution_x, sc.render.resolution_y = 1800, 800
w = bpy.data.worlds.new('w'); sc.world = w
cam = bpy.data.objects.new('cam', bpy.data.cameras.new('cam'))
cam.data.type = 'ORTHO'
ys = [p.y for p in d_pts] + fy
cy = (min(ys) + max(ys)) / 2
zs = [p.z for p in d_pts] + [p.z + off.z for p in f_pts]
cam.data.ortho_scale = max(max(ys) - min(ys) + 20, (max(zs) - min(zs) + 50) * 1800 / 800)
cam.location = (200, cy, (max(zs) + min(zs)) / 2 - 10)
cam.rotation_euler = (math.pi / 2, 0, math.pi / 2)
sc.collection.objects.link(cam); sc.camera = cam
cam.data.clip_end = 1000
sc.render.filepath = os.path.abspath(os.path.join(GUN, 'fit_overlay.png'))
bpy.ops.render.render(write_still=True)
print('RENDERED', sc.render.filepath)
