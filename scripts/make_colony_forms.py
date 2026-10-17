"""Build the vertex-colored factory leaf in Blender."""
import bpy
import json
import math
import os
import re
from pathlib import Path
import struct
import sys

root=Path(__file__).resolve().parents[1]
profile=root/'assets/models/source/colony_forms.json'
def unique_fields(pairs):
    value={}
    for key,item in pairs:
        if key in value:raise ValueError('Duplicate form field: '+key)
        value[key]=item
    return value

data=json.loads(profile.read_text(),object_pairs_hook=unique_fields)
if set(data)!={'$schema','forms'}:raise ValueError('Unknown form document fields')
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
seen=set()
for form in data['forms']:
    if set(form)!={'name','shape','color','roughness','ior','subsurface'}:raise ValueError('Unknown form fields')
    if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]{0,30}',form['name']) or form['name'] in seen:raise ValueError('Invalid or duplicate form name')
    seen.add(form['name'])
    if not all(math.isfinite(v) and 0<=v<=1 for v in form['color']) or len(form['color'])!=3:raise ValueError('Invalid pigment')
    if not .02<=form['roughness']<=1 or not 1<=form['ior']<=3:raise ValueError('Invalid optical properties')
    sss=form['subsurface']
    if set(sss)!={'weight','radius'} or not 0<=sss['weight']<=1 or len(sss['radius'])!=3 or not all(math.isfinite(v) and .0001<=v<=10 for v in sss['radius']):raise ValueError('Invalid subsurface material')
    if form['shape']=='leaf':
        vertices=[(0,1,0),(0,-.35,0),(-.55,.25,0),(.55,.25,0),(0,.25,.18),(0,.25,-.08)]
        faces=[(1,2,4),(2,0,4),(0,3,4),(3,1,4),(2,1,5),(0,2,5),(3,0,5),(1,3,5)]
        mesh=bpy.data.meshes.new(form['name']);mesh.from_pydata(vertices,[],faces);mesh.update()
        obj=bpy.data.objects.new(form['name'],mesh);bpy.context.collection.objects.link(obj)
    else:raise ValueError('Unknown form shape')
    obj.name=form['name'];mesh=obj.data;mesh.update();mesh.calc_loop_triangles()
    color=mesh.color_attributes.new(name='Color',type='FLOAT_COLOR',domain='POINT')
    for value in color.data:value.color=(*form['color'],1)
    roughness=mesh.attributes.new(name='roughness',type='FLOAT',domain='POINT')
    ior=mesh.attributes.new(name='ior',type='FLOAT',domain='POINT')
    for value in roughness.data:value.value=form['roughness']
    for value in ior.data:value.value=form['ior']
    records=[];triangles=[]
    for triangle in mesh.loop_triangles:
        row=[]
        for index in triangle.vertices:
            vertex=mesh.vertices[index];normal=vertex.normal if mesh.polygons[triangle.polygon_index].use_smooth else triangle.normal
            row.append(len(records));records.append((*vertex.co,form['ior'],*normal,0,*form['color'],form['roughness'],*sss['radius'],sss['weight']))
        triangles.append((*row,0))
    target=root/'assets/models'/(form['name']+'.fmesh')
    with target.open('wb') as stream:
        stream.write(struct.pack('<8sII',b'FLMESH02',len(records),len(triangles)))
        for row in records:stream.write(struct.pack('<16f',*row))
        for row in triangles:stream.write(struct.pack('<4I',*row))
    print(target.name,len(triangles),'triangles')
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(profile.with_suffix('.blend')))

# Outputs are closed before exit. Headless authoring does not need device teardown.
sys.stdout.flush()
os._exit(0)
