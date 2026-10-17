import bpy, sys, math
from mathutils import Vector
args = sys.argv[sys.argv.index('--')+1:]
src, out = args[0], args[1]
yaw = float(args[2]) if len(args) > 2 else 0.0
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=src, ignore_leaf_bones=False)
meshes = [o for o in bpy.data.objects if o.type == 'MESH']
for o in bpy.data.objects:
    if o.type == 'ARMATURE':
        o.hide_render = True
lo = Vector((1e9,)*3); hi = Vector((-1e9,)*3)
for o in meshes:
    for v in o.data.vertices:
        w = o.matrix_world @ v.co
        for i in range(3):
            lo[i] = min(lo[i], w[i]); hi[i] = max(hi[i], w[i])
c = (lo + hi) / 2
print('BOUNDS_CM', tuple(round(x*100,1) for x in lo), tuple(round(x*100,1) for x in hi))
scene = bpy.context.scene
scene.render.engine = 'BLENDER_WORKBENCH'
scene.display.shading.light = 'STUDIO'
scene.display.shading.color_type = 'MATERIAL'
scene.display.shading.show_cavity = True
scene.render.resolution_x = 1600; scene.render.resolution_y = 700
scene.render.film_transparent = False
world = bpy.data.worlds.new('w'); scene.world = world
import random
random.seed(3)
for m in bpy.data.materials:
    m.diffuse_color = (random.uniform(.25,.9), random.uniform(.25,.9), random.uniform(.25,.9), 1)
cam_data = bpy.data.cameras.new('cam'); cam_data.type = 'ORTHO'; cam_data.ortho_scale = float(args[3]) if len(args) > 3 else 1.3
cam = bpy.data.objects.new('cam', cam_data); scene.collection.objects.link(cam); scene.camera = cam
ang = math.radians(yaw)
d = Vector((math.cos(ang), math.sin(ang), 0))
cam.location = c + d * 3
cam.rotation_euler = (math.radians(90), 0, ang + math.radians(90))
scene.render.filepath = out
bpy.ops.render.render(write_still=True)
