"""Author vertex-colored cellular meshes and tissue backgrounds with local Blender.

blender --background --factory-startup -noaudio --python scripts/make_biology.py -- assets/models
Editable profiles/.blend stay in models/source; only .fmesh exports are bundled.
"""
import bpy
import json
import math
import os
from mathutils import Matrix,Vector
from pathlib import Path
import random
import struct
import sys

out=Path(sys.argv[sys.argv.index('--')+1]).resolve()
profiles=json.loads((out/'source/biology_profiles.json').read_text())
rng=random.Random(profiles['seed'])
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
parts=[];models=[]

def finish(obj,color,roughness=.82,ior=1.35,smooth=True,material=0):
    obj.data.transform(Matrix.LocRotScale(obj.location,obj.rotation_euler.to_quaternion(),obj.scale))
    obj.matrix_world=Matrix.Identity(4);mesh=obj.data
    colors=mesh.color_attributes.new(name='Color',type='FLOAT_COLOR',domain='POINT')
    for v in mesh.vertices:
        rgb=color(v.co) if callable(color) else color
        colors.data[v.index].color=(*rgb,1)
    for name,value in [('roughness',roughness),('ior',ior)]:
        a=mesh.attributes.new(name=name,type='FLOAT',domain='POINT')
        for entry in a.data:entry.value=value
    tag=mesh.attributes.new(name="surface",type="INT",domain="FACE")
    for entry in tag.data:entry.value=material
    for poly in mesh.polygons:poly.use_smooth=smooth
    mesh.update();parts.append(obj);return obj

def sphere(center,size,color,sub=2,roughness=.82,ior=1.35):
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=sub,radius=1,location=center)
    obj=bpy.context.object;obj.scale=size
    return finish(obj,color,roughness,ior)

def rod(a,b,radius,color):
    a,b=Vector(a),Vector(b);delta=b-a
    bpy.ops.mesh.primitive_cone_add(vertices=8,radius1=radius,radius2=radius*.55,depth=delta.length,location=(a+b)/2)
    obj=bpy.context.object;obj.rotation_euler=delta.to_track_quat('Z','Y').to_euler()
    return finish(obj,color)

def export(name):
    global parts
    bpy.ops.object.select_all(action='DESELECT')
    for obj in parts:obj.select_set(True)
    bpy.context.view_layer.objects.active=parts[0];bpy.ops.object.join()
    obj=bpy.context.object;obj.name=name;mesh=obj.data;mesh.calc_loop_triangles()
    colors=mesh.color_attributes['Color'].data;rough=mesh.attributes['roughness'].data;ior=mesh.attributes['ior'].data
    records=[];indices=[];lookup={}
    for triangle in mesh.loop_triangles:
        row=[]
        for vi,li in zip(triangle.vertices,triangle.loops):
            normal=mesh.corner_normals[li].vector;key=(vi,*normal)
            if key not in lookup:
                lookup[key]=len(records)
                records.append((*mesh.vertices[vi].co,ior[vi].value,*normal,0,*colors[vi].color[:3],rough[vi].value))
            row.append(lookup[key])
        indices.append((*row,mesh.attributes["surface"].data[triangle.polygon_index].value))
    if any(abs(x)>1.49 for row in records for x in row[:3]):raise RuntimeError(name+' exceeds local mesh bounds')
    with (out/(name+'.fmesh')).open('wb') as f:
        f.write(struct.pack('<8sII',b'FLMESH01',len(records),len(indices)))
        for row in records:f.write(struct.pack('<12f',*row))
        for row in indices:f.write(struct.pack('<4I',*row))
    obj.location=((len(models)%4)*3.5,0,(len(models)//4)*3.5)
    models.append(obj);parts=[];print(name,len(records),'vertices',len(indices),'triangles',flush=True)

def cell(name,key):
    p=profiles[key]
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=5,radius=1)
    obj=bpy.context.object
    for v in obj.data.vertices:
        n=v.co.normalized();x,y,z=n
        noise=.035*math.sin(8*x+3*y)*math.sin(7*z-2*y)+.018*math.sin(17*x-5*z)
        v.co=n*(1+noise);v.co.y*=.93
    def pigment(v):
        n=v.normalized();wave=.5+.5*math.sin(5*n.x+3*n.y)*math.sin(5*n.z-2*n.y)
        return tuple(p['membrane'][i]*(1-.23*wave)+p['rim'][i]*(.23*wave) for i in range(3))
    finish(obj,pigment,.38,ior=1.38,material=3)
    inner=profiles['interior']
    sphere(inner['nucleus_center'],inner['nucleus_size'],p['nucleus'],3,.42)
    sphere(inner['nucleolus_center'],inner['nucleolus_size'],p['vesicle'],2,.4)
    for index,center in enumerate(inner['mitochondria']):
        sphere(center,inner['mitochondrion_size'],inner['mitochondrion_color'],2,.52)
        # Mitochondrial cristae are geometry rather than painted lines.
        for j in range(3):
            c=Vector(center)+Vector(((j-1)*.07,0,.085))
            sphere(c,(.018,.063,.018),p['nucleus'],1,.6)
    for index,center in enumerate(inner['vesicle_centers']):
        sphere(center,(.065,.055,.06),p['vesicle'],2,.44)
    # Curved endoplasmic reticulum surrounding the nucleus, inside the shell.
    for ring in range(3):
        previous=None
        for j in range(25):
            angle=j*math.tau/24;radius=inner['reticulum_radius']+ring*.06
            point=(radius*math.cos(angle),radius*.7*math.sin(angle),-.15+ring*.095+.04*math.sin(angle*3))
            if previous:rod(previous,point,inner['reticulum_tube'],inner['reticulum_color'])
            previous=point
    export(name)

def virus(large):
    p=profiles['virus']
    if not large:
        sphere((0,0,0),(.58,.58,.58),p['body'],2,.8)
        # Detachable spikes are instances of virus_spike.fmesh at runtime.
    else:
        sphere((0,.32,0),(.48,.50,.48),p['body'],2)
        sphere((0,.36,.40),(.23,.24,.12),p['core'],2)
        rod((0,-.28,0),(0,.1,0),.105,p['spikes'])
        sphere((0,-.30,0),(.20,.10,.20),p['spikes'],1)
        for i in range(6):
            a=i*math.tau/6;mid=(.5*math.cos(a),-.4,.5*math.sin(a));tip=(.72*math.cos(a),-.78,.72*math.sin(a))
            rod((0,-.27,0),mid,.035,p['spikes']);rod(mid,tip,.025,p['spikes'])
    export('virus_'+('infector' if large else 'virion'))

def immune(large):
    p=profiles['immune'];radius=.70 if large else .59
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=3,radius=1)
    obj=bpy.context.object
    for v in obj.data.vertices:
        n=v.co.normalized();x,y,z=n;v.co=n*radius*(1+.09*math.sin(7*x+2*y)*math.sin(6*z-y))
        if large:v.co.x*=1.12
    finish(obj,p['body'],.86)
    for i in range(3 if not large else 2):
        sphere(((i-1)*.18,.10,.51),(.19,.22,.12),p['nucleus'],2)
    for i in range(7 if large else 4):
        a=i*math.tau/(7 if large else 4)
        sphere((math.cos(a)*radius*.8,-.2,math.sin(a)*radius*.8),(.20,.17,.22),p['body'],2)
    for i in range(10):
        a=i*2.39996323;sphere((.38*math.cos(a),.32*math.sin(a),.49),(.04,.04,.04),p['granules'],1)
    export('immune_'+('macrophage' if large else 'leukocyte'))

def node():
    p=profiles['node'];sphere((0,.12,0),(.24,.16,.24),p['stem'],2)
    for i in range(4):
        y=.25+i*.16;r=.18+.05*math.sin(i)
        sphere((.04*math.sin(i*2),y,0),(r,.17,r),p['membrane'],2)
    for i in range(5):
        a=i*math.tau/5;sphere((.25*math.cos(a),.82,.25*math.sin(a)),(.14,.14,.14),p['buds'],2)
    sphere((0,.87,0),(.20,.17,.20),p['membrane'],2)
    export('cell_replication_node')

def background(name,key):
    p=profiles[key];vertices=[];faces=[];nx,ny=64,40
    for j in range(ny+1):
        y=(j/ny*2-1)*1.46
        for i in range(nx+1):
            x=(i/nx*2-1)*1.48
            z=.018*math.sin(11*x+2*math.sin(5*y))+.013*math.cos(13*y-3*x)+.006*math.sin(29*x+17*y)
            vertices.append((x,y,z))
    for j in range(ny):
        for i in range(nx):
            a=j*(nx+1)+i;faces.extend([(a,a+1,a+nx+2),(a,a+nx+2,a+nx+1)])
    mesh=bpy.data.meshes.new(name);mesh.from_pydata(vertices,[],faces);mesh.update()
    obj=bpy.data.objects.new(name,mesh);bpy.context.collection.objects.link(obj)
    def pigment(v):
        # Broad cytoplasm gradients and membrane/fiber folds, all baked to vertices.
        x,y,z=v;f=.5+.5*math.sin(11*x+2*math.sin(5*y))*math.cos(13*y-3*x)
        return tuple(p['base'][i]*(1-.55*f)+p['ridge'][i]*.55*f for i in range(3))
    finish(obj,pigment,1,1)
    for j in range(7):
        y=-.72+j*.23
        last=None
        for i in range(20):
            x=-1.38+i*2.76/19;point=(x,y+.025*math.sin(x*8+j),.025+.015*math.cos(x*7+j))
            if last:rod(last,point,.007,p['fiber'])
            last=point
    # Blood cells and small extracellular vesicles occupy depth behind the play field.
    for i in range(18):
        x=rng.uniform(-1.2,1.2);y=rng.uniform(-.7,.7);z=rng.uniform(.08,.38)
        if abs(x)<.46 and abs(y)<.22:continue
        radius=rng.uniform(.025,.05)
        sphere((x,y,z),(radius,radius*.72,radius*.32),p['blood'],2,1,1)
    for i in range(26):
        x=rng.uniform(-1.2,1.2);y=rng.uniform(-.73,.73);z=rng.uniform(.02,.28)
        sphere((x,y,z),(.006,.006,.006),p['ridge'],1,1,1)
    export(name)

cell('cell_infected','infected_cell');cell('cell_healthy','healthy_cell');cell('cell_neutral','neutral_cell')
virus(False);virus(True);immune(False);immune(True);node()
# One reusable shaft and head, local +Z; attached, flying and embedded use this mesh.
rod((0,0,0),(0,0,.87),.075,(1,1,1));sphere((0,0,.91),(.14,.14,.09),(1,1,1),1,.45)
export('virus_spike')
background('tissue_background','tissue');background('bloodstream_background','bloodstream')
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(out/'source/biology.blend'),compress=True)
print('Saved source/biology.blend',flush=True)
# All exports are closed. Avoid the sandboxed Blender audio teardown hang.
os._exit(0)
