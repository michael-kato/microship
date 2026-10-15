"""Blender CPU authoring: one vertex-colored collector/manufacturing stalk."""
import bpy
import json
import math
from mathutils import Matrix
from pathlib import Path
import struct
import sys

out=Path(sys.argv[sys.argv.index('--')+1]).resolve()
p=json.loads((out/'source/stalk_profile.json').read_text())
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
parts=[]
def finish(obj,color):
    obj.data.transform(Matrix.LocRotScale(obj.location,obj.rotation_euler.to_quaternion(),obj.scale))
    obj.matrix_world=Matrix.Identity(4)
    colors=obj.data.color_attributes.new(name='Color',type='FLOAT_COLOR',domain='POINT')
    for item in colors.data:item.color=(*color,1)
    for poly in obj.data.polygons:poly.use_smooth=True
    parts.append(obj)

def bulb(center,size,color):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=12,ring_count=8,location=center)
    obj=bpy.context.object;obj.scale=size;finish(obj,color)

bpy.ops.mesh.primitive_cylinder_add(vertices=12,radius=p['stem_radius'],depth=p['stem_height'],
    location=(0,p['stem_height']/2,0),rotation=(math.pi/2,0,0))
finish(bpy.context.object,p['stem_color'])
bulb((0,.12,0),(.14,.15,.14),p['stem_color'])
for i in range(p['leaf_count']):
    a=i*math.tau/p['leaf_count']
    bpy.ops.mesh.primitive_uv_sphere_add(segments=12,ring_count=8,
        location=(math.cos(a)*p['leaf_radius']/2,.76,math.sin(a)*p['leaf_radius']/2))
    obj=bpy.context.object;obj.scale=(p['leaf_radius'],.035,p['leaf_width']);obj.rotation_euler.y=-a
    finish(obj,p['leaf_color'])
for i in range(p['leaf_count']):
    a=i*math.tau/p['leaf_count']
    bulb((math.cos(a)*.10,.90,math.sin(a)*.10),(.10,.07,.10),p['petal_color'])
bulb((0,.94,0),(.09,.06,.09),p['core_color'])
bpy.ops.object.select_all(action='DESELECT')
for obj in parts:obj.select_set(True)
bpy.context.view_layer.objects.active=parts[0];bpy.ops.object.join()
obj=bpy.context.object;obj.name='Collector stalk';mesh=obj.data;mesh.calc_loop_triangles()
records=[];triangles=[]
for triangle in mesh.loop_triangles:
    indices=[]
    for vertex_index,loop_index in zip(triangle.vertices,triangle.loops):
        vertex=mesh.vertices[vertex_index];normal=mesh.corner_normals[loop_index].vector
        color=mesh.color_attributes['Color'].data[vertex_index].color[:3]
        indices.append(len(records));records.append((*vertex.co,1.45,*normal,0,*color,p['roughness']))
    triangles.append((*indices,0))
with (out/'collector_stalk.fmesh').open('wb') as f:
    f.write(struct.pack('<8sII',b'FLMESH01',len(records),len(triangles)))
    for row in records:f.write(struct.pack('<12f',*row))
    for row in triangles:f.write(struct.pack('<4I',*row))
bpy.ops.wm.save_as_mainfile(filepath=str(out/'source/collector_stalk.blend'))
print('Collector stalk:',len(records),'vertices',len(triangles),'triangles')
